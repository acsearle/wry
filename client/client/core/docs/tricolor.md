# Concurrent collection correctness

## 1. Atomic preliminaries

https://en.cppreference.com/cpp/atomic/memory_order

**Modification order consistency:** all threads agree on a single, total order
of writes to each specific atomic object.

**No single total modification order:** threads do not agree on the ordering of
writes to different atomic objects.

**Inter-thread happens-before:** if one thread performs an atomic store-release
to some variable, and another thread performs an atomic
load-acquire from that variable that takes its value from the release operation 
itself or from a subsequent atomic read-modify-write operation in the release 
sequence, then all writes before the release on the first thread 
happen-before all reads after the acquire on the second thread.  Note that it is
the _identity_ of the value that matters, which is stronger than requiring
the values are equal. 

## 2. Epoch system

Consider an atomic counter.  Every write is a transition from x -> x + 1.  The
value of the counter is therefore a reification of the modification order,
sequentially labeling the modifications.  Threads can then associate writes and
reads with counter values, and reason about if a happens-before relationship
has been established.

The _epoch service_ draws on this idea to provide a global partial modification
order.  It maintains a single atomic variable that includes
- the number $E$ of the current epoch
- the number $m$ of threads "in" the current epoch
- the number $n$ of threads "in" the previous epoch
We speak of epoch $E$ or the full state $(E, m, n)$.  $m$ and $n$ can be thought
of as reference counts keeping epochs $E$ and $E-1$ alive.  Unlike the strictly
increasing atomic counter above, the epoch number is only non-decreasing, and
the information its values provide about modification order is coarser, with
runs of the same value being indistinguishable. 

Fundamental operations on the epoch state are:

**pin:** A thread increases the number of threads in the current epoch, and 
remembers the current epoch number.

**unpin** A thread compares its remembered epoch number against the current
epoch number, and decreases the number of threads in either the current or
previous epoch.  The remembered epoch number
must correspond to either the current or previous epochs, and that epoch must
be greater than zero.  Violation of these preconditions indicates an
implementation bug.  

**try-advance** A thread loads the number of threads in the previous epoch, and
if zero, it increases the epoch number and swaps the numbers of threads
in the current and previous epoch.  The thread
executing may have either the current or prior epoch pinned, or not be pinned
at all.  If it is pinning the prior epoch, the operation will not accomplish
anything.

The system as a whole
cannot advance unless every pin is eventually unpinned.  Note that **pin** 
increments $m$ but never $n$, **unpin** decreases either $m$ or $n$, and 
**try-advance** only increases $n$ when it increases $E$.

In practice, threads will also atomically execute combinations of these
operations, specifically **unpin-try-advance** and **unpin-try-advance-pin**.
Every modification of the epoch atomic is performed by a read-modify-write
operation, so the release sequence headed by any release on it is unbroken.

### 2.1 Inter-thread happens-before

- Thread $A$ *pins* the current epoch $E$
- Thread $A$ is in epoch $E$.
- Thread $A$ *unpin-try-advances* $E' \in \{E, E+1, E+2\}$.

When $A$ *pins*, it performs the transition 
1. $(E, m, n) \rightarrow (E, m+1, n)$

When $A$ *unpins*, it may discover that some other thread has already
advanced the epoch to $E+1$, and that the pinning state of other threads may
allow it to advance the epoch itself.  It performs one of the transitions
1. $(E, m, n) \rightarrow (E, m-1, n)$
2. $(E, m, 0) \rightarrow (E+1, 0, m-1)$
3. $(E+1, m, n) \rightarrow (E+1, m, n-1)$
4. $(E+1, m, 1) \rightarrow (E+2, 0, m)$

These transitions are accomplished via an atomic compare-exchange operation,
retried in a loop until it succeeds.  The operation may fail due to another
thread succeeding in its own transition, or spuriously.  A successful *pin*
operation has memory order acquire; a successful *unpin* has memory order release.

Now consider another thread
- Thread $B$ pins the current epoch $F$.
- Thread $B$ is in epoch $F$.

The global epoch number never decreases, so if $F > E+2$, then every store 
of value $E+2$ precedes every store of value $F$ in the modification order,
and precedes every load of value $F$.

We may further improve the inequality.  In cases 1, 2 and 3, $A$ writes $E$ or
$E+1$.  In case 4, $A$ writes $E+2$ and this is the first write of this value.
In all cases, every write of $E+2$ is in the release-sequence headed by $A$'s 
unpin.  So we can say that any store in thread $A$ before it unpins the epoch
$E$  happens before thread $B$ pins the epoch $F \ge E+2$ and performs any loads.

More concisely, we can say that epoch $E$ happens-before epoch $F$ if
$F\ge E+2$.  This is a _partial ordering_.  Neighbouring epochs are 
_incomparable_, but sufficiently different epochs are in numeric order.

If $B$ instead reads $E+1$, we cannot prove a happens-before relationship,
though one exists in cases 1 and 2. In case 4, $A$ writes $E+2$ 
which obviously cannot precede $E+1$.  In case 3, we can't know if the $E+1$ that
$B$ read was before or after the $E+1$ that $A$ wrote.

The above arguments hold if we replace thread $A$ with any other thread that
pins epoch $E$, and if we replace thread $B$ with any other thread that pins
epoch $F$.  Thus we obtain the result: 

**Theorem 2.1:** Stores made by any thread in epoch $E$ happen-before
loads made by any thread in epoch $F \ge E+2$.

### 2.2 Embargoed reads

In theorem 2.1 applies to both atomic and non-atomic variables.  For a non-
atomic variable, any write that does not happen-before the read is a data race
and undefined behavior.
Therefore the only writes that may legally have produced the value that $F$
reads are writes that happen-before $F$'s read; that is, writes in 
epochs $\le E$.

However, if the variable is atomic, $F$ may load values written in any of the
epochs in the range $\[E,F+1\]$.  While the reading thread is in epoch
$F$, other threads might also be in $F-1$, $F$ or even $F+1$, and the reading
thread might encounter writes made by any of them.

It is intuitive, but not yet proven, that since $F$ happens-before $F+2$,
any load in $F$ happens-before any store in $F+2$, and thus the load cannot 
take its value from epochs $\ge F+2$.  The C++ standard explicity addreses this.  

> If a value computation $A$ of an atomic object $M$ happens before an operation $B$ 
> that modifies $M$, then $A$ shall take its value from a side effect $X$ on $M$, where 
> $X$ precedes $B$ in the modification order of $M$. [ Note: This requirement is 
> known as read-write coherence.  — end note ]
> - [intro.races/16]

Thus any atomic load made in epoch $F$ will originate from an epoch $\le F+1$.

This causes a particular problem when the value is a pointer.
The load-acquire relationship we established permits us to follow any pointer
written in epoch $D \le E$, but the pointer we loaded may have
been written after that value.  Thus we can't dereference the pointer
that we read in $F$, potentially written as late as $F+1$, until a later
epoch.

Corollary 2.2.1 (dereference embargo). A pointer loaded by a thread in epoch $F$ 
may have been written by a writer in any epoch up to $F+1$. To dereference it 
safely, the reader must wait until it pins some later epoch $G$ where 
$G \ge (F+1) + 2 = F + 3$. The release that establishes synchronization is the 
writer's unpin of epoch $F+1$.

This problem only arises when the pointer is stored and loaded with relaxed
memory ordering; without the epoch system, it would not be legal to
dereference it at all.  Conventionally, pointers are stored and loaded with
release acquire memory order, immediately establishing their own happens-before
relationship (independent of an epoch system).

On the x86-64 platform, hardware provides a total store order, but the C++
memory model is platform-independent, and the compiler is permitted to reorder
relaxed loads and stores.  The embargo is thus required at the language level,
even if the resulting machine happened to be correct without it.  On weakly
ordered architectures like AArch64, the embargo is also required at the
hardware level.


  














## (Older)

This document derives the safety properties of the tricolor scheme implemented
in [`garbage_collected.cpp`](../garbage_collected.cpp) and
[`garbage_collected.hpp`](../garbage_collected.hpp), in the same style as the
embargo argument inlined into `Collector::collector_takes_reports`. The goal is
to make every cross-thread dependency explicit and discharge it from a small
set of primitives.

The reasoning structure is:

1. State the invariants we want to preserve.
2. Enumerate the synchronization primitives we have.
3. Describe the per-bit state machine and which thread writes each transition.
4. For each phase transition at the collector, argue why the trigger
   condition implies the necessary cross-thread visibility.
5. Walk through worst-case interleavings.
6. Track open questions.

Sections 1–3 are filled in. Sections 4–6 are scaffolds for further work.

---

## 1. What we are proving

Each independent collection is identified by a single bit `k` in the 16-bit
gray/black words. Up to 16 collections coexist; the per-bit reasoning below
generalizes by AND-masking with `k`.

The **safety** properties we need:

- **S1 — No live object is freed.** If, at the moment the collector executes
  `delete object` for some bit `k`, the object is reachable from a root or from
  another reachable object, that is a bug.
- **S2 — Every k-white→k-gray transition performed by a mutator is reflected
  in the collector's view before k is declared stable.** Otherwise the
  collector might delete an object that a mutator is in the middle of marking
  reachable.
- **S3 — `_black` writes are race-free.** The collector writes `_black` with
  plain stores; we must show no other thread ever writes it concurrently and
  that mutators that read derived `_thread_local_black_for_allocation` see the
  correct values via the epoch system.
- **S4 — Concurrent collections do not interfere.** Bits k₀ and k₁ progress
  through their phases independently; the per-object `_gray`/`_black` words
  contain non-overlapping bit-patterns from each.

The **liveness** properties we want (not the focus of this document, but worth
naming):

- **L1 — Every unreachable object is freed within a bounded number of
  collection cycles.** The bound is governed by the number of epochs each
  phase transition waits.
- **L2 — Phase transitions eventually fire.** This depends on mutators
  repinning frequently enough; not analyzed here.

This design may be theoretically unable to guarantee L1 or L2, but should
achieve timely collection in practice.

The counterexample is this: 
- Start with a singly linked list of objects, with mutable next pointers.
- The head is grey, the rest of the list is white.
- Suppose the collector will scan the list in reverse order (head last).
- The collector scans all the tail nodes, finds them to be white, and takes no
action.
- The mutator concurrently unlinks the tail from the head, writing zero into
the head's next pointer.  The mutator executes the write barrier, shading the
first node of the tail, but the collector has already inspected it.
- The collector marks the head node black, enumerates its single child pointer,
and sees the new null value, so it has nothing to trace.
- We're now back at the original situation; we have a singly linked list of
white objects with a new gray head.  We have to perform a number of scans equal
to the list length to get to the end, which is obviously O$(N^2)$.  Likewise,
we can't transition phase because the mutator makes one new gray object every
scan.

In practice, to repeatedly touch objects in the right order and to see exactly
the right atomic values would require a pathological scheduler, so we can expect
to settle down rapidly in practice.

If we didn't eagerly depth-first trace objects the collector shaded, but just
relied on the scan encountering them, this problem would manifest even for
immutable singly linked lists that we happened to scan in reverse order.

Subsequent sections derive S1–S4 from primitives.

---

## 2. Primitives

### 2.1 Atomic operations on object headers

| Field | Type | Mutator access | Collector access |
|-------|------|----------------|------------------|
| `_gray` | `Atomic<uint16_t>` | `fetch_or` (RELAXED) in `_garbage_collected_shade`; constructor `=` while object is thread-private | `load` (RELAXED), `compare_exchange_weak` (RELAXED, RELAXED) during scan |
| `_black` | plain `mutable uint16_t` | constructor `=` while object is thread-private; otherwise no access | plain read/write during scan |
| `_count` | `Atomic<int32_t>` | `fetch_add`, `fetch_sub` (RELAXED) via `Root` | `load` (RELAXED) during scan |

The asymmetry is the key: `_gray` is contended (mutator shades, collector
processes), so it must be atomic. `_black` is only contended for one window —
between construction and registration — and that window is closed by the
report mechanism, so post-registration the collector has exclusive access.
This is the basis for **S3**.

### 2.2 Global atomics

| Variable | Type | Producer ordering | Consumer ordering |
|----------|------|-------------------|-------------------|
| `_global_atomic_color_for_allocation` | `Atomic<Color>` | collector store (RELAXED) | mutator load (RELAXED) on pin/repin |
| `_global_atomic_reports_head` | `Atomic<Report*>` | mutator CAS (RELAXED, RELAXED) | collector exchange (RELAXED) |

Both are RELAXED on both ends. Their cross-thread visibility is established
*not* through release/acquire on these atomics themselves, but through the
epoch system — exactly as for the report contents (the existing embargo
argument).

### 2.3 Epoch ordering (recap)

From the [embargo argument in
`Collector::collector_takes_reports`](../garbage_collected.cpp#L173-L199):

> If thread A unpins or repins from epoch E (release barrier), and thread B
> later pins or repins to epoch F where F > E + 1, then every write A made
> before its release happens-before every read B makes after its acquire.

We will write this as **HB(E, F) iff F > E + 1** when discussing inter-thread
visibility. The collector's loop deliberately waits for the epoch to advance
by ≥ 3 since its last phase change so that any per-mutator state changes
happening up to that change are visible.

Is **iff** the right choice here?  happens-before _might_ happen under a looser
constraint, and it's even observable if we additionally know that when we
unpinned the epoch, the epoch had not yet advanced.

### 2.4 Mutator pin/repin/unpin

Each mutator at quiescence boundaries publishes a `Report` and then repins.
The report contents are RELAXED-written but synchronized with the collector
via the epoch (the embargo). Mutators load the current `Color` on each
pin/repin, also RELAXED, also synchronized via the epoch.

**Consequence.** A mutator's view of the published colors is "the colors
stored before the mutator's last acquire." More precisely: if the mutator
pins/repins to epoch F, it sees every collector color-store that was followed
by a collector release at epoch E < F − 1.

It may, but is not guaranteed, to also see collector color-stores from as late
as F + 1.

### 2.5 Collector quiescence

The collector pins, exchanges out the report list, places `(E + 3, head)` in
`_embargoed_until`, and only dereferences `head` once its current pinned
epoch ≥ E + 3. This is the embargo. Throughout this document we treat the
embargo as established and use it as a black box.

---

## 3. Per-bit state machine

Pick a single bit `k`. Its meaning at any object header is encoded by the
pair (`gray`, `black`) projected onto bit k:

| gray bit | black bit | name | "this object is…" |
|----------|-----------|------|--------------------|
| 0 | 0 | **k-white** | a candidate for collection in cycle k |
| 1 | 0 | **k-gray** | reachable in cycle k, children not yet traced |
| 1 | 1 | **k-black** | reachable in cycle k, children traced |
| 0 | 1 | (forbidden) | not produced in steady state; would require black-without-gray |

### 3.1 Transitions

Each transition is annotated with **(writer, ordering, reader-side
expectation)**. The reader-side expectation is what must already be true for
the read on the other thread to be correct.

Table is a bad format for this because (a) cells are too long and (b) pipe is
used both to delimit cells and mean bitwise 'or', and the display breaks because
it doesn't respect the code quoting.

| From → to | Writer(s) | Ordering | Reader expectation |
|-----------|-----------|----------|--------------------|
| k-white → k-gray (mutator shading) | mutator | `_gray.fetch_or(k)` RELAXED; record in `_thread_local_gray_did_shade`; reported | collector reads via `_gray.load` and via `Report::gray_did_shade`; both relaxed, both gated by embargo |
| k-white → k-gray (collector marking, e.g. roots) | collector | `_gray.compare_exchange_weak(_, _, RELAXED, RELAXED)` | only collector reads its own writes within the same scan |
| k-gray → k-black | collector | plain `_black PIPE= k` | only collector reads `_black`; `_gray` already has k set, no change |
| k-black → k-white (clearing) | collector | `_gray.compare_exchange_weak` clearing k; plain `_black &= ~k` | collector internal; mutators only read derived `_thread_local_*_for_allocation`, never `_black` directly |
| k-white → k-black | does not occur | — | — |

### 3.2 Where each transition lives in code

- Mutator shading: `_garbage_collected_shade` at
  [garbage_collected.cpp:58–63](../garbage_collected.cpp#L58-L63), and
  through `Edge::operator=` calling `garbage_collected_shade` (the
  Yuasa-style snapshot-at-the-beginning / deletion barrier — shades the
  displaced pointer on overwrite) and through `Root` reaching multiplicity
  zero ([garbage_collected.hpp:256–258](../garbage_collected.hpp#L256-L258)).
  We use unconditional `fetch_or` on the gray bits rather than Yuasa's
  classic "if white, shade gray"; equivalent because OR is idempotent on
  any already-non-white object's gray bit.
- Collector marking and clearing: `Collector::collector_scans`
  ([garbage_collected.cpp:351–554](../garbage_collected.cpp#L351-L554)),
  inside the per-object CAS loops on `_gray` and the plain writes to `_black`.
- Allocation: `GarbageCollected::GarbageCollected`
  ([garbage_collected.cpp:42–56](../garbage_collected.cpp#L42-L56)) stamps
  `_gray` and `_black` from the thread-local snapshot of the published color.

### 3.3 Why `_black` writes are race-free (justifies **S3**)

After construction-and-registration, the only writer to `_black` is the
collector. The construction-time write happens while the object is reachable
only from `_thread_local_new_objects` — by definition, only the constructing
thread sees it. That thread publishes a report containing the object pointer,
and the collector takes ownership of the object via `_known_objects` after
the embargo. Once the object is in `_known_objects`, the constructing thread
never reads or writes `_black` again. So:

- Construction-time `_black =` happens-before the report publish (program
  order on the constructing thread).
- Report publish happens-before the collector dereferencing the report
  (embargo + epoch HB).
- Therefore construction-time `_black =` happens-before any subsequent
  collector access.

After that, the collector is the sole writer/reader of `_black`. ✓ **S3**.

### 3.4 What `_thread_local_gray_did_shade` records

For each quiescence period and each k, indicates the mutator produced new work
for the collector by (a) allocating new objects k-gray or (b) changing an
existing object from k-white to k-gray.

This summary is what feeds `_shade_history` at the collector and drives the
"color is stable" decision (section 4.3).

### 3.5 Staggered collections

The basic tri-color collector has a scanning phase and a sweeping phase.  In
both cases, they visit all objects. 

Our collector visits all objects in a loop.  Bitmaps encode the state of
multiple different collectors, and the loop is able to shade, mark, sweep or
clear all of these simultaneously with a compare-exchange of the 
object's "color" word.



---

## 4. Phase transitions at the collector

> *Skeleton — to be filled in by the author.*

For each transition below, state:
- **Trigger condition** (the bit-mask expression on histories).
- **What the trigger implies** about cross-thread visibility (using HB and
  epoch counts).
- **What the collector does next** (writes to internal masks; effect on
  subsequent allocations and scans).
- **Why the wait length is sufficient** (and why a shorter wait would break
  S1 or S2).

Phases to cover:

### 4.1 Publish k-gray (start a new collection on bit k)

[`try_advance_collection_phases` lines 342–346](../garbage_collected.cpp#L342-L346).

Trigger: `~_color_in_use`

`_color_in_use` tracks which bits are in use, and applies to both the gray and
black words.

If there are unused bits, we may start another collection by transitioning that
bit to k.  We can pick any bits; currently we pick the least significant unused
bit.

`new_bit = (_color_in_use + 1) & ~_color_in_use`.

A bit being unused implies that the bit is zero on all existing objects.  The
scan asserts this on every object.  Before a used bit retires to unused state,
the scan (acting as a sweep on k) clears that bit on all surving objects. 

States we pass through:

All mutators are k-white.
All objects are k-white.

Transition: The collector publishes k-grey in epoch E.

Mutators are k-white or k-grey.
Objects are k-white or k-grey.
Objects are allocated k-white or k-grey.
Objects may be shaded k-white to k-grey by:
- k-grey mutator's write barriers
- collector is permitted to shade gray if they are roots or reachable from
  other gray objects, but this is just an optimization 
There are no k-black objects.

NOTE: if k-black was allowed here, we could have a k-black object with a field
overwritten by a k-white mutator whose k-write barrier is not yet in effect;
this is why we have a gray warm-up phase, and why the collector can't
recursively trace yet.  Since the goal is to get things to stop becoming gray 
as quickly as possible, we should propagate gray from parent to child where we
can, but without the black bit we can't yet trace the graph without getting
stuck in loops.  This manifests as not adding the children of gray objects to
the depth-first trace, yet.

Eventually: The epoch advances to epoch E + 2.

All mutators are k-grey.
All objects are k-white or k-grey.
Objects are allocated k-grey.
Objects may be shaded k-white to k-gray.
No new k-white objects are made.
The number of k-white objects is non-increasing.
The number of k-grey objects is non-decreasing.

### 4.2 Publish k-black (k-gray acknowledged by all mutators)

Trigger: The collector pins an epoch F > E + 1 where E is the epoch the
collector wrote k-grey.

Transition: The collector publishes k-black in epoch F.

Mutators are k-gray or k-black.
Objects are k-white or k-gray or k-black.
Objects are allocated k-gray or k-black.
No new k-white objects are made.
Objects may be shaded k-white to k-gray by mutator write barriers (including root count)
The collector is now permitted to use k-black and trace recursively.
Objects may be shaded k-white to k-gray to k-black by the collector.
- If reachable from a gray object
- If root count nonzero
The number of k-white objects is non-increasing.
The number of k-black objects is non-decreasing.

NOTE: As soon as k-black is allowed we can start tracing.  The mutator's write
barrier doesn't care about gray vs black, and we have proved that no mutators
are k-white any more.  

Eventually: The epoch advances to F + 2.

Mutators are k-black.
Objects are k-white or k-gray or k-black.
Objects are allocated k-black.
No new k-white or k-gray objects are made.  (Existing k-white may become k-gray)
Objects may be shaded k-white to k-gray by mutator write barriers (including root count)
Objects may be shaded k-white to k-gray to k-black by the collector.
The number of k-white objects is non-increasing.
The number of k-black objects is non-decreasing.

### 4.3 K-black acknowledged and stable-color detection

The collector is now tracing the graph while k-black mutators mutate the graph
by allocating new black nodes (no problem) and overwriting pointers to nodes
that are still k-white (tricky).

We can stop only when
- we have finally received all Reports that might contain newly allocated 
  k-white and k-gray objects.  (All mutators are now k-black but Reports will
  arrive later; this is just another case of wait-for-a-later epoch).  
- we can prove that no object will ever turn k-gray again.

A write barrier can only turn a k-white object k-gray if it can reach that
k-white object.

By epoch G, the collector has received the Reports containing the final k-gray
allocated objects.  These Reports were made in the last epoch k-gray mutators
could exist: epoch F+1, so G > F+2.

After this point, all k-white and k-gray objects are known to the collector.
For reachable object A to remain k-white after this pass, it must be reachable
via an object B to which (all) pointer(s) were overwritten before the collector
traced them.  These overwrites shaded that B gray, but the collector must
have already seen that B was white, which means that the pointers must have
been overwritten during (loosely) the scan.

Note that at the end of each scan, the collector has visited every object
(except newly allocated black objects) to see if it is k-gray or a root, traced
(a snapshot of) their chidren (thus visiting these objects twice) and left 
every object in a not k-gray state.  So to maintain reachable white objects
requires the mutators to actively move things around during every scan, and
even so it turns at least one white object gray.

The termination condition:

Recall the embargo result:

The mutator is in epoch X
The mutator performs various stores
- nonatomic stores to `Report`s
- atomic store-relaxed to `_gray`
Finally
- atomic store-relaxed to `_global_atomic_reports`
- atomic store-release to `_global_epoch_state` of value X or X+1

The collector load-acquires epoch Y
Mutator epoch X's stores happen-before store of X+1 happens-before Y if X+1 < Y

Now, suppose the collector has pinned epoch C.
Other threads may be in C-1, C or C+1.
Thus the collector may see writes from a mutator in C+1.
When this mutator unpins C+1 it may write C+1 or C+2.

Thus the collector must be in Y > C+2 before it can follow a Report pointer.
Likewise a gray bit store-relaxed in epoch C+1 may load as soon as collector
epoch C but only happens-before, and is thus guaranteed to load in, collector
epoch C+3.

So:

The collector pins epoch E.
It takes the report pointer and places it in embargo until E+3.
Some older reports are still embargoed.
It reads reports for which the embargo just lifted:
- New black objects, which require no work.
- Did shade gray
The collector unpins.

The collector scans.

The collector pins epoch F >= E at the end of the scan.
The collector unpins.
Mutators may be in epochs up to F+1.
Such a mutator writes F+2 when it unpins.

The collector must be in G > F+2 to be sure of receiving that report.
The collector then can't open it until G+3.

In G+3, the collector has access to the reports of all mutators since E-3.
If any report shading gray, we start another scan.

If none of them did shade gray, we can terminate.

This is very fiddly.  The saving grace is that
(a) TSan is able to detect embargo errors
(b) If there are any k-grays when we terminate, the sweep will detect them.


Eventually:

Mutators are k-black.
Objects are k-white or k-black.
New objects are allocated k-black.
No objects are k-gray.
Mutators only see k-black objects and perform no shades.

### 4.4 Delete k-whites

The hard part is the termination above.

Collector scans all known objects
- deletes k-white
- retains k-black
- asserts if k-gray (bug)
New objects that have not yet been reported are all k-black.
- asserts if did_shade (bug)

[scan loop, lines 521–538](../garbage_collected.cpp#L521-L538). Trigger:
`_mask_for_deleting != 0` and `(before_gray & _mask_for_deleting) == 0`.

### 4.5 Unpublish k-gray and k-black

Once we have deleted all known k-whites:

All mutators are k-black.
All objects are k-black.
Mutators are making new black-objects.
Mutators perform no shades.

At this point the k-bits have served their purpose and the collector no longer
assigns meaning to them.

The collector publishes new values for gray and black, clearing the k-bits.

Some mutators are k-black.
Some mutators are k-white.
New objects are k-black or k-white.
k-black mutators shade k-white objects newly made by k-white mutators.
Objects of all k-colors exist.

Eventually: the epoch advances by 2.

All mutators are k-white.
No mutators are shading k-white to k-gray.
New objects are k-white.
Objects of all k-colors exist.

We still need to wait slightly longer until we can open the embargoed reports
that contain the last k-gray objects.

### 4.6 Clear k from all surviving objects

All new embargoed objects are k-white.
All k-gray and k-black objects are in the known set.

Sweep the known set and set all objects to k-white.

All objects, known and embargoed, are k-white.
All mutators are k-white.
No mutators are shading objects k-gray.

### 4.7 Recycle k

We can now remove bit k from the color_in_use set.

With bit k unused and all objects k-white and all mutators k-white, we are back
at the beginning.

---

## 5. Worst-case interleavings

> *Skeleton — to be filled in by the author. For each scenario, draw the
> per-thread timeline with epoch labels and bit values, and pinpoint the
> rule from §4 that prevents the bug.*

### 5.1 Leading mutator allocates k-white, trailing mutator shades k-gray

The scenario alluded to in
[the comment at lines 332–338](../garbage_collected.cpp#L332-L338).

> TODO.

### 5.2 Mutator shading races collector marking on `_gray`

Both threads RMW `_gray`. The mutator's `fetch_or(k)` and the collector's
`compare_exchange_weak` must compose correctly.

> TODO: trivial because OR is commutative and idempotent — but write it out
> to confirm no bit-loss is possible.

### 5.3 Object's count drops to zero between root-removal and scan

`Root::~Root` decrements count and shades on count==1
([garbage_collected.hpp:246–263](../garbage_collected.hpp#L246-L263)). Mutator
might re-root in between.

> TODO: argue that the shade-on-zero plus the epoch-pinned root protocol
> gives the same guarantees as a write barrier on the implicit roots set.

### 5.4 K-bit reuse: cycle on bit 0 ends, new cycle on bit 0 begins

> TODO: the recycle ordering (4.6 must complete before 4.1 picks up the bit).
> Confirm that the `_color_in_use` mask is the gating mechanism and that
> picking up a freshly-cleared bit can't see stale set-state in any object.

### 5.5 Who advances the epoch?

If a thread has pinned epoch E, we have said above that when it unpins the
epoch it store-releases either E (redundantly) or E+1.  In the former case,
some other thread has E-1 pinned; in the latter case, some other thread has
E pinned.  But if E is the prior epoch (the epoch advanced while we had it
pinned), and we were the last thread pinning it, we could correctly write
E+2 when we unpin.

This doesn't actually break anything because we rely on a load-acquire of E+2
being enough to synchronize, and it is in this case also precisely because it
is the first write of E+2.

If the epoch is entirely unpinned, it could theoretically advance by any
amount.

We've assumed above that the collector unpins while scanning.  This lets the
epoch system keep ticking over at "frame rate", keeps the bump allocation size
proportional to one frame's worth of temporary memory, not N frames where
N scales with the collection graph size.

---

## 6. Open questions

- **`mark = before_gray` vs `mark = after_gray`** in
  `collector_scans`
  ([lines 483–491](../garbage_collected.cpp#L483-L491)). The two
  semantics differ when a root was added between the previous scan and this
  one. Resolution should fall out of §4.4 — capture the decision here once
  made.
- **The 3-cycle wait in §4.3.** Confirm that exactly three cycles of
  `_shade_history` are needed; document a counter-example for two cycles
  (presumably one where a leading mutator stamps k-gray on allocation but
  the trailing mutator's report hasn't been embargoed-out yet).
- **`mutator_pin` does not re-prime `_thread_local_gray_did_shade`** (only
  `mutator_repin` does — and as of recent edits, neither does). Decide
  whether a fresh pin should re-prime, and what the invariant on
  `_thread_local_gray_did_shade` is at pin time.
- **Liveness of the wait branch.** §4 currently assumes the collector
  eventually advances; the busy-spin (or pending sleep) at
  [lines 256–262](../garbage_collected.cpp#L256-L262) is the controlling
  factor. Out of scope for this document, but cross-link if a separate
  liveness analysis is written.
- **Uint16 wrap on epoch differences.** The wrap-safe comparison pattern
  `(int16_t)(uint16_t)(E.raw - F.raw) < N` is used in two places
  ([:215](../garbage_collected.cpp#L215),
  [:256](../garbage_collected.cpp#L256)). Confirm this is sufficient given
  the maximum cycle latency (between any two adjacent epoch reads,
  fewer than 32768 epochs can have elapsed).
- **Is there any benefit to the k-collections?** If so, how many?  If four are 
  enough be just need a single byte.  I assert that it's not actually the source
  of the complexity, but it doesn't help.  Sure is cool though.
- **Is the reasoning about the range of time the atomic gray bits can show up 
  over actually valid?**  Somehow it feels different from acquire-release of
  nonatomic writes, but... maybe it isn't.
- **Tests and testability**


---

## 7. Literature comparison

A pass at locating this design in the published GC literature.  Names and
claims are best-effort; cross-check before quoting.

### What this system mechanically is

- Concurrent mark-sweep, non-moving, non-generational.
- Tricolor (white/gray/black), with the twist that gray and black are
  encoded as *separate bits per "k-collection"* — supporting up to 16
  overlapping concurrent collections distinguished by bit position.  Most
  schemes have one collection at a time.
- **Yuasa-style deletion barrier** — shades the displaced pointer on
  overwrite, via unconditional `fetch_or` on the gray bit (idempotent on
  already-non-white objects, so equivalent to Yuasa's classic conditional
  "if white, shade gray").
- Per-mutator-thread allocation bags (`_thread_local_new_objects`),
  reported to the collector at quiescence boundaries.
- Phase transitions are **ragged**: each mutator observes a phase change
  at its own next pin/repin boundary, not at a synchronized handshake.
  The collector waits *enough epochs* before relying on a phase change
  being globally visible.
- An "embargo" (3-epoch delay) on reading mutator reports gives the
  per-mutator stores time to be globally visible before the collector
  dereferences them.

### DLG (Doligez-Leroy-Gonthier, 1993/1994)

The closest classical match.  They have:

- Concurrent mark-sweep tricolor.
- Snapshot-at-the-beginning (Yuasa) deletion barrier.
- Phase structure CLEAR / TRACING / SWEEPING / RESTING, very close to our
  UNUSED / GRAY_PUBLISHED / BLACK_PUBLISHED / SWEEPING / WHITE_PUBLISHED /
  CLEARING.
- Per-thread allocation regions, shared heap, distinction between "young"
  private and shared globally-visible state.

What we share: barrier choice, tricolor, phase shape, per-thread
allocation.

What we differ on:

- **Phase synchronization mechanism.**  DLG uses synchronous handshakes —
  every mutator must acknowledge a phase change before the collector
  advances.  Each thread blocks at the handshake until released.  Our
  scheme is **ragged**: the phase publication is a single atomic store,
  mutators observe it at their own quiescence, the collector waits N
  epochs to be sure.  Threads never block waiting for each other.
- **K-collection overlap.**  DLG runs one collection at a time.  Our
  16-bit gray/black words let multiple collections coexist with non-
  overlapping bit roles.  This specific design pattern doesn't appear in
  mainstream literature in this form; it's the natural generalization but
  no canonical reference is known to the author.
- **Generational / private-region separation.**  DLG distinguishes
  "private young" from "shared old" and runs a special-case allocator +
  write barrier for the latter.  We have no generational separation.

### Pizlo's lineage (Schism 2010, FUGC, Fiji CMR)

The "ragged safepoint" terminology is closest to this family.  Filip
Pizlo coined "ragged safepoints" in *Schism* (PLDI 2010) for an
asynchronous handshake mechanism: the collector requests work at each
thread's next polling point, threads do it on their own time, the
collector proceeds when all have responded.  Same idea as our epoch
advances, different implementation (Pizlo uses callbacks at safepoints;
we use a counted atomic state that mutators read at pin/repin).

Pizlo's most current work, FUGC ("Fil's Unbelievable Garbage Collector"),
describes itself as a "grey-stack Dijkstra accurate non-moving"
collector using "soft handshakes (ragged safepoints)".  FUGC explicitly
cites DLG as antecedent and Schism/Fiji CMR as Pizlo's prior work.  It
uses:

- **Dijkstra insertion barrier**, not Yuasa.  (Stores newly-pointed-at
  object onto worklist; we shade displaced.)
- Ragged safepoints for phase transitions.
- No load barrier.
- Non-moving.

What we share with FUGC: ragged phase transitions, non-moving, no load
barrier, tricolor.

What we differ on: barrier choice (Yuasa vs Dijkstra), and the specific
safepoint-vs-epoch implementation.

### Crossbeam-style epoch-based reclamation

Our epoch service has the same shape as Fraser's epoch-based reclamation
(EBR), popularized by Aaron Turon's *Lock-freedom without garbage
collection* (2015) and the Rust `crossbeam-epoch` crate: pin/unpin,
bounded retirement, advance-when-quiescent.  We use it for both the
embargo on mutator reports and the synchronization mechanism for phase
transitions.

This is what fuses the DLG-style tricolor with the Pizlo-style ragged
safepoints:

- **From EBR**: the bounded-retirement pin/unpin contract, with the
  embargo as the retirement delay.
- **From Pizlo's ragged safepoints**: phase transitions don't require
  synchronous handshakes; mutators observe phase changes at their own
  quiescence.

### One-line characterization

**A DLG-family concurrent tracing collector** (tricolor + Yuasa deletion
barrier + per-thread allocation reports + phased mark/sweep) **with
Pizlo-style ragged phase transitions** (no synchronous handshakes;
phases publish as atomic colors and mutators observe at their own
quiescence) **driven by a Fraser-style epoch service** (pin/unpin with
N-epoch embargo replacing the usual hazard-pointer / quiescent-state
retirement).

The k-collection multi-bit overlap is a structural detail we can't trace
to a specific paper but feels like an unwritten generalization —
multiple instances of the otherwise-standard scheme, packed into the
same gray/black word at distinct bit positions.

### Bibliography of close cousins

- Doligez & Leroy, *A Concurrent, Generational Garbage Collector for a
  Multithreaded Implementation of ML*, POPL 1993 — primary DLG.
- Doligez & Gonthier, *Portable, Unobtrusive Garbage Collection for
  Multiprocessor Systems*, POPL 1994 — the "unobtrusive" handshake
  formulation closest in spirit to our ragged style (still synchronous
  though).
- Yuasa, *Real-time garbage collection on general-purpose machines*,
  Journal of Systems and Software, 1990 — the deletion barrier we use.
- Pizlo et al., *Schism: Fragmentation-Tolerant Real-Time Garbage
  Collection*, PLDI 2010 — first use of "ragged safepoints"; closest
  spirit to our phase-transition mechanism.
- Pizlo, *Fil's Unbelievable Garbage Collector* (https://fil-c.org/fugc) —
  modern descendant; explicit DLG comparison; uses Dijkstra not Yuasa.
- Vechev, Yahav & Bacon, *Derivation and Evaluation of Concurrent
  Collectors*, ECOOP 2005 — formal taxonomy that classifies our scheme as
  an instance of "incremental snapshot-at-the-beginning," close to but
  distinct from incremental update.
- Pirinen, *Barrier techniques for incremental tracing*, ISMM 1998 —
  classic survey separating "what is preserved" by a barrier from "how"
  it preserves it.
- Österlund, *Block-free concurrent GC: stack scanning and copying*,
  ISMM 2016 — modern asynchronous-handshake stack scanning; ZGC parts of
  the lineage.
- Turon, *Lock-freedom without garbage collection* (2015,
  http://aturon.github.io/tech/2015/08/27/epoch/) — accessible
  epoch-based reclamation explainer; close to our epoch service shape.
- Jones, Hosking & Moss, *The Garbage Collection Handbook*, 2nd ed.
  2023 — the design lineage by direct admission of the author: pick the
  options that place the least compute burden on the mutator.

