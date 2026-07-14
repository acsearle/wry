# The garbage collector

The collector lives in [`garbage_collected.hpp`](../garbage_collected.hpp) /
[`garbage_collected.cpp`](../garbage_collected.cpp), on the epoch service in
[`epoch.hpp`](../epoch.hpp).  It is a DLG-family concurrent tracing collector
(tricolor, Yuasa deletion barrier) with Pizlo-style ragged phase transitions
driven by the epoch service.  Up to 16 collections overlap, one per bit of
the 16-bit gray/black words.  Section 6 locates the design in the
literature.

Design posture, in order:

- **Mutators never block on the collector.**  Load-bearing; every mechanism
  below is priced against it.
- **Minimize mutator burden.**  The mutator pays one idempotent relaxed
  `fetch_or` per barrier and a report push per quiescence, and nothing
  else.  Costs move to the collector thread wherever a choice exists.
- **Diagnose violations, don't tolerate them.**  Stalled threads are bugs
  (see the non-goal in section 9); per-object invariant checks trap
  corruption rather than working around it.

Reading order: sections 1-2 are the memory-model and epoch foundations;
section 3 states the goals, primitives, and per-bit state machine; section
4 argues each phase transition, with 4.8-4.11 recording the 2026-07
list-driven redesign; section 5 is the measured throughput model; section 6
is the literature comparison.  Sections 7-9 hold unfinished sketches, open
questions, and future work.

---

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

---

## 2. The epoch system

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

Theorem 2.1 applies to both atomic and non-atomic variables.  For a non-
atomic variable, any write that does not happen-before the read is a data race
and undefined behavior.
Therefore the only writes that may legally have produced the value that $F$
reads are writes that happen-before $F$'s read; that is, writes in
epochs $\le E$.

However, if the variable is atomic, $F$ may load values written in any of the
epochs in the range $[E, F+1]$.  While the reading thread is in epoch
$F$, other threads might also be in $F-1$, $F$ or even $F+1$, and the reading
thread might encounter writes made by any of them.

It is intuitive, but not yet proven, that since $F$ happens-before $F+2$,
any load in $F$ happens-before any store in $F+2$, and thus the load cannot
take its value from epochs $\ge F+2$.  The C++ standard explicitly addresses
this.

> If a value computation $A$ of an atomic object $M$ happens before an operation $B$
> that modifies $M$, then $A$ shall take its value from a side effect $X$ on $M$, where
> $X$ precedes $B$ in the modification order of $M$. [ Note: This requirement is
> known as read-write coherence.  -- end note ]
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

Since the stage-3 revision (4.8) the report channel is release/acquire and no
longer relies on this embargo.  The embargo argument remains load-bearing
where there is no natural release point: bump-slab rotation in the epoch
allocator, and the raggedness bounds on color publication (mutators load the
published colors relaxed and rely on the epoch for visibility).

### 2.3 Who advances the epoch

If a thread has pinned epoch $E$, then when it unpins it store-releases
either $E$ (redundantly) or $E+1$.  In the former case, some other thread has
$E-1$ pinned; in the latter case, some other thread has $E$ pinned.  But if
$E$ is the prior epoch (the epoch advanced while we had it pinned), and we
were the last thread pinning it, we may correctly write $E+2$ when we unpin.

This does not break anything: we rely on a load-acquire of $E+2$ being enough
to synchronize, and it is in this case too, precisely because ours is the
first write of $E+2$.  If the epoch is entirely unpinned, it can in principle
advance by any amount.

The collector repins periodically while it works.  This keeps the 
epoch ticking at mutator
("frame") cadence during long scans -- which keeps the epoch allocator's bump
slabs sized to one frame's worth of temporaries rather than $N$ frames' --
and lets the phase gates of section 4 make progress mid-pass.

### 2.4 The width of the epoch

Stored epochs are compared cyclically.  The counter is 32 bits (`cyc32_t`,
comparison window $\pm 2^{30}$), packed with the pin counts into the single
64-bit `Service::State` word (epoch:32, pins_current:15, waiting:1,
pins_prior:16; pin-count trap at 0x7FFF).  Sixteen bits were tried first and
failed empirically (2026-07-12): a loaded 100k-entity cycle spanned 8000+
epochs -- epochs tick at frame rate while collector iterations stretch to
seconds -- pushing stored-epoch comparisons (cohort min_epoch, sweep gates,
quiet timestamps) past the $\pm$0x4000 window and firing the locality assert.

---

## 3. The tracing collector

### 3.1 Life of one collection

The argued version of this narrative is section 4; this is the shape of one
cycle on a single bit k, from bit allocation to bit recycle.  Up to 16 such
cycles overlap, staggered, each on its own bit.

**Quiescent: bit k is unused.**
- All mutators are k-white; all objects are k-white.
- New objects are allocated k-white; no mutator shades k.

**The collector publishes k-gray.**
- Some mutators are k-white, some k-gray; each adopts the color at its own
  next quiescence.
- New objects are allocated k-white or k-gray, by their allocator's color.
- k-gray mutators' write barriers shade displaced objects
  k-white -> k-gray.
- The collector receives reports of k-white allocations, k-gray
  allocations, and k-gray shadings.  It cannot trace yet: without k-black
  it cannot tell visited from unvisited, so it parks the k-gray work
  (4.10's warm-up deferral).

**All mutators see k-gray.**
- Every object reachable at this point will survive the k-sweep: k-white
  allocation and un-k-barriered overwrites all happen-before this point,
  and every overwrite after it shades its displaced pointer for k.  This
  is the snapshot.
- All k-white allocations have been reported; no more will occur.
- All mutators now shade k-white -> k-gray on overwrite.

**The collector publishes k-black.**
- Mutators are k-gray or k-black; objects are k-white, k-gray, or k-black;
  allocation is k-gray or k-black.
- The collector now traces: reported gray arrivals, the parked warm-up
  work, and the standing roots (the root registry), depth-first to
  fixpoint, blackening as it goes.

**The collector finishes tracing, and waits out the quiet window.**
- Any k-gray object it has not traced was either allocated gray before
  some mutator saw k-black, or shaded by a barrier -- and in both cases it
  is in a report that will arrive.  Fresh work re-opens tracing; graph
  mutation can prolong this, but each round costs O(new work), and in
  practice it converges quickly.
- The window closes when no k-work has arrived for two epochs and a trace
  has run since the last work: now no k-gray objects exist, no mutator can
  produce one (nothing k-white is reachable), and every reachable object
  is k-black.

**Weak decisions.**
- With the marks stable, registered weak objects (the weak registry)
  decide their fate before the sweep can free them.

**The collector sweeps.**
- One walk over the eligible cohorts deletes every object that is white
  for any currently-sweeping bit (asserting it is unrooted), and certifies
  the survivors marked for every swept bit.  A k-gray object found here is
  a bug and asserts.

**The collector publishes k-white.**
- Mutators progressively drop k from their masks; allocation and barriers
  stop carrying k.  After the window, no report can ever carry fresh
  k-work.

**Clearing rides the sweep.**
- Cohorts old enough to carry stale k-marks are flagged; subsequent sweep
  walks strip k from survivors; late reports from mutators that loaded
  pre-white colors are caught by the strip horizon (4.10).

**The bit recycles.**
- No cohort is flagged for k; all objects and all mutators are k-white;
  bit k returns to the unused pool, and we are back at the beginning.

### 3.2 What we are proving

Each independent collection is identified by a single bit `k` in the 16-bit
gray/black words. Up to 16 collections coexist; the per-bit reasoning below
generalizes by AND-masking with `k`.

The **safety** properties we need:

- **S1 -- No live object is freed.** If, at the moment the collector executes
  `delete object` for some bit `k`, the object is reachable from a root or from
  another reachable object, that is a bug.
- **S2 -- Every k-white -> k-gray transition performed by a mutator is
  reflected in the collector's view before k is declared stable.** Otherwise
  the collector might delete an object that a mutator is in the middle of
  marking reachable.
- **S3 -- `_black` writes are race-free.** The collector writes `_black` with
  plain stores; we must show no other thread ever writes it concurrently and
  that mutators that read derived `_thread_local_black_for_allocation` see the
  correct values via the epoch system.
- **S4 -- Concurrent collections do not interfere.** Bits k0 and k1 progress
  through their phases independently; the per-object `_gray`/`_black` words
  contain non-overlapping bit-patterns from each.

The **liveness** properties we want (not the focus of this document, but worth
naming):

- **L1 -- Every unreachable object is freed within a bounded number of
  collection cycles.** The bound is governed by the number of epochs each
  phase transition waits.
- **L2 -- Phase transitions eventually fire.** Practical, not adversarial.
  Termination of tracing requires a quiet window (4.8), and every k-flip a
  mutator performs re-opens it, so an adversarial mutator that keeps
  unlinking not-yet-traced objects can extend a cycle a number of times bounded
  by the number of k-white objects; no new k-white objects are being created, and
  at least one must be shaded k-gray each unlink to prevent the transition.  What
  the stage-3 shadelists changed is the price: a late shade is consumed as
  a targeted trace of one subtree on a pass the collector was making
  anyway, where the original design re-ran a full O(heap) validation scan
  per late shade (an adversarial O(N^2)).  We accept the long
  adversarial tail; real mutators quiesce, and cycles converge in a round
  or two.

### 3.3 Primitives

#### 3.3.1 Atomic operations on object headers

| Field | Type | Mutator access | Collector access |
|-------|------|----------------|------------------|
| `_gray` | `Atomic<uint16_t>` | `fetch_or` (RELAXED) in `_garbage_collected_shade`; constructor `=` while object is thread-private | `load` (RELAXED), `compare_exchange_weak` (RELAXED, RELAXED) during the registry walk and trace |
| `_black` | plain `mutable uint16_t` | constructor `=` while object is thread-private; otherwise no access | plain read/write during trace and sweep |
| `_count` | `Atomic<int32_t>` | `fetch_add`, `fetch_sub` (RELAXED) via `Root` | `load` (RELAXED) during the registry walk and sweep |

The asymmetry is the key: `_gray` is contended (mutator shades, collector
processes), so it must be atomic. `_black` is only contended for one window --
between construction and registration -- and that window is closed by the
report mechanism, so post-registration the collector has exclusive access.
This is the basis for **S3**.

A non-zero `_count` indicates membership in the reference-counted root set.
Unlike shared_ptr, we don't delete the object on zero count, and thus we don't
need release ordered decrements or a final acquire.  Instead on zero we shade
the object with the write barrier to indicate it _was_ reachable, and hand over
responsibility to the tracing collector.   

#### 3.3.2 Debug-only header fields

`NDEBUG`-gated fields on the header (`_debug_allocation_gray`,
`_debug_allocation_black`, `_debug_allocation_epoch`) record the
allocation-time colors and epoch for the per-object invariant checks and
crash forensics.  They are written once at construction, are never
synchronized, and carry no correctness role.  Note that no shipped
configuration defines NDEBUG (verified 2026-07-14), so every build we
run -- Release included -- carries the fields and runs the checks; an
NDEBUG build compiles (kept honest by syntax checks) but has never been
exercised.  Do not generalize from these fields' access patterns to
production memory-ordering choices.

#### 3.3.3 Global atomics

| Variable | Type | Producer ordering | Consumer ordering |
|----------|------|-------------------|-------------------|
| `_global_atomic_color_for_allocation` | `Atomic<Color>` | collector store (RELAXED) | mutator load (RELAXED) on pin/repin |
| `_global_atomic_reports_head` | `Atomic<Report*>` | mutator CAS (RELEASE on success) | collector exchange (ACQUIRE) |

The color word is relaxed at both ends; its cross-thread visibility rides the
epoch system alone, with the raggedness bounds used throughout section 4.

The report head is an ordinary release/acquire channel (since "stage 3", 4.8).
The successful publish CAS is the last write the mutator makes to the report,
so the collector's acquiring exchange makes the report contents -- and
everything the mutator wrote before publishing, including the `_gray` words
of the objects it shaded and the headers of the objects it allocated --
immediately readable.  Because the exchange is a read-modify-write, it takes
the head's latest modification-order value: one exchange receives every
report published so far.

#### 3.3.4 The mutator at quiescence

At each quiescence boundary a mutator publishes a `Report` -- its
allocations, shaded objects, root-ups, weak registrations, and did-shade
summary bits since the last boundary -- with a release CAS, then repins,
loading the published colors (relaxed) for the next period.  The push is
sequenced before the repin; the completeness lemma (4.8) counts on exactly
this ordering.

A mutator's view of the published colors is "the colors stored before its
last acquire": pinned at $F$ it is guaranteed to see every color the
collector stored while pinned at $E \le F - 2$, and may additionally see
stores as late as $F + 1$.  Raggedness is therefore bounded at about two
epochs, and the collector's gates wait accordingly.

#### 3.3.5 The collector receives reports

Each loop iteration begins by exchanging out the report list (acquire) and
merging it: the batch's allocations become a new cohort (one per receive,
4.10), flagged against the strip horizons of any clearing bits; shaded
objects drain into the gray wavefront; root-ups and weak registrations file
into their registries (4.9); and the `gray_did_shade` bits feed the
quiet-window accounting.  Receive runs first in every iteration; several
phase arguments (the strip horizon, the termination gates) lean on this
receive-before-advance ordering.

### 3.4 Per-bit state machine

Pick a single bit `k`. Its meaning at any object header is encoded by the
pair (`gray`, `black`) projected onto bit k:

| gray bit | black bit | name | "this object is..." |
|----------|-----------|------|--------------------|
| 0 | 0 | **k-white** | a candidate for collection in cycle k |
| 1 | 0 | **k-gray** | reachable in cycle k, children not yet traced |
| 1 | 1 | **k-black** | reachable in cycle k, children traced |
| 0 | 1 | (forbidden) | not produced in steady state; would require black-without-gray |

#### 3.4.1 Transitions

Each transition, with its writer and ordering:

- **k-white -> k-gray, by a mutator (shading).**  `_gray.fetch_or`
  (relaxed) in `_garbage_collected_shade`; when the fetch_or flips a bit,
  the object is recorded once into the thread-local shadelist and the
  did-shade summary.  The collector observes the shade through the report
  (the shadelist entry), not by rediscovering it on the heap.
- **k-white -> k-gray, by the collector (root walk, receive-time
  promotion).**  `_gray.compare_exchange_weak` (relaxed, relaxed), subject
  to the gray-bit rule of 4.10: gray only what you can also blacken or
  park.
- **k-gray -> k-black, by the collector (tracing).**  Plain
  `_black |= k`; only the collector touches `_black` after registration
  (3.4.3).
- **k-black -> k-white, by the collector (strip, riding sweep).**  CAS
  clearing the gray bit and a plain clear of the black bit, applied to
  survivors during the sweep walk (4.10).  Mutators never read `_black`;
  they read only the published allocation masks.
- **k-white -> k-black directly: does not occur.**

#### 3.4.2 Where each transition lives in code

- Mutator shading: `garbage_collected_shade`, called by the mutable slot
  types' store barriers (`AtomicScanSlot`, `AtomicMarkedScanSlot` -- the
  Yuasa-style snapshot-at-the-beginning deletion barrier shades the
  displaced pointer on overwrite) and by the root count reaching zero in
  `garbage_collected_roots_subtract`.  The barrier is an unconditional
  `fetch_or` of the thread's gray mask rather than Yuasa's classic "if
  white, shade gray": equivalent because OR is idempotent on any
  already-non-white object's gray bit, and branch-free.
- Root-up shading and registry filing: `_garbage_collected_root_up`, from
  `garbage_collected_roots_add` on the 0 -> 1 count transition (4.9).
- Collector marking: `_promote` at receive, and the root-registry walk and
  graystack trace in `collector_trace`.
- Sweeping and stripping: `collector_sweep_walk`.
- Phase transitions: `try_advance_collection_phases`.
- Allocation: the `GarbageCollected` constructor stamps `_gray` and
  `_black` from the thread-local snapshot of the published color.

#### 3.4.3 Why `_black` writes are race-free (justifies S3)

After construction-and-registration, the only writer to `_black` is the
collector.  The construction-time write happens while the object is
reachable only from `_thread_local_new_objects` -- by definition, only the
constructing thread sees it.  That thread publishes a report containing the
object pointer (release CAS), and the collector's acquiring exchange takes
ownership of the object into a cohort.  Once the object is in a cohort, the
constructing thread never reads or writes `_black` again.  So:

- Construction-time `_black =` happens-before the report publish (program
  order on the constructing thread).
- The publish happens-before the collector dereferencing the report
  (release/acquire on the report head).
- Therefore construction-time `_black =` happens-before any subsequent
  collector access.

After that, the collector is the sole writer and reader of `_black`; the
trace's blackening, the sweep's read, and the strip's clear all inherit the
same reasoning.  **S3** holds.

---

## 4. Phase transitions at the collector

One collection, on bit k, argued phase by phase.  4.1-4.7 walk the life
cycle in order; 4.8-4.10 record the revisions, implemented 2026-07-12, that
replaced the original full-scan machinery (immediate reports, registries,
cohorts); 4.11 is a design note held in reserve.  Where an early section's
mechanism was superseded, it defers to the revision rather than repeating
it.

Each transition trigger asks one of three kinds of question (mirrored in the
comments of `try_advance_collection_phases`):

- *Has time passed?* -- have all mutators observed a color publish?
  Counted as epochs since the publish.  Used by GRAY_PUBLISHED and
  WHITE_PUBLISHED.
- *Has the work been done?* -- has a sweep walk run since the phase began?
  Used by SWEEPING and CLEARING.
- *What did the mutators actually do?* -- has k-work stopped arriving, and
  has what arrived been traced?  Used only by BLACK_PUBLISHED, because
  tracing termination depends on what the mutators wrote, not just on
  time.

### 4.1 Publish k-gray (start a new collection on bit k)

In `try_advance_collection_phases`: each call starts a collection on at most
one bit, the first UNUSED one found.  (Keeping in-flight collections spread
out -- an admission policy -- is the open lever of section 5.)

A bit being unused implies the bit is zero on every existing object: before
a used bit retires, the sweep strips it from all survivors (4.6), and the
per-object color checks trap any object still carrying it.

States we pass through:

All mutators are k-white.
All objects are k-white.

Transition: the collector publishes k-gray in epoch E.  It conservatively
records the cycle start as k-work, so the quiet window of 4.8 cannot open
during warm-up.

Mutators are k-white or k-gray.
Objects are k-white or k-gray.
Objects are allocated k-white or k-gray.
Objects may be shaded k-white to k-gray by:
- k-gray mutators' write barriers;
- the collector, which may legally shade gray early (roots, children of
  gray objects) -- the *optional early shade* -- but today defers that work
  instead, parking it in the warm-up bag (4.10): under record-once
  shadelists, an early gray that its agent can neither blacken nor park
  can be orphaned.
There are no k-black objects.

NOTE: if k-black were allowed here, we could have a k-black object with a
field overwritten by a k-white mutator whose k-write-barrier is not yet in
effect -- a hidden white object behind a black one.  This is the
*no-early-black rule*, and it is why there is a gray warm-up phase at all.
It also means the collector cannot recursively trace yet: without the black
bit it cannot tell visited from unvisited and would get stuck in cycles of
the object graph.

Eventually: the epoch advances to E + 2.

All mutators are k-gray.
All objects are k-white or k-gray.
Objects are allocated k-gray.
Objects may be shaded k-white to k-gray.
No new k-white objects are made.
The number of k-white objects is non-increasing.
The number of k-gray objects is non-decreasing.

### 4.2 Publish k-black (k-gray acknowledged by all mutators)

Trigger: the collector pins an epoch F >= E + 2, where E is the epoch of the
k-gray publish.

Transition: the collector publishes k-black in epoch F.  It records the
sweep gate `_k_sweep_gate[k] = F + 2` (every allocation from the gate on is
k-marked at birth; 4.10), and re-feeds the parked warm-up bag through the
arrivals drain, now that k may blacken.

Mutators are k-gray or k-black.
Objects are k-white or k-gray or k-black.
Objects are allocated k-gray or k-black.
No new k-white objects are made.
Objects may be shaded k-white to k-gray by mutator write barriers
(including the root count reaching zero).
The collector is now permitted to use k-black and trace recursively.
Objects may be shaded k-white to k-gray to k-black by the collector:
- if reachable from a gray object;
- if their root count is nonzero (the registry walk).
The number of k-white objects is non-increasing.
The number of k-black objects is non-decreasing.

NOTE: as soon as k-black is allowed we can start tracing.  The mutator's
write barrier doesn't care about gray vs black, and we have proved that no
mutators are k-white any more.

Eventually: the epoch advances to F + 2.

Mutators are k-black.
Objects are k-white or k-gray or k-black.
Objects are allocated k-black.
No new k-white or k-gray objects are made (existing k-white may become
k-gray).
Objects may be shaded k-white to k-gray by mutator write barriers.
Objects may be shaded k-white to k-gray to k-black by the collector.
The number of k-white objects is non-increasing.
The number of k-black objects is non-decreasing.

### 4.3 Tracing and termination

The collector traces while k-black mutators mutate the graph: new
allocations are born marked (no problem); pointer overwrites can hide
still-white objects behind already-traced ones (tricky).  We can stop only
when

- every report that might carry k-work -- k-gray warm-up allocations,
  barrier shades -- has been received; and
- we can prove that no object will ever turn k-gray again.

The second is the snapshot induction.  A write barrier can only shade a
k-white object that its mutator can reach.  For a reachable object to remain
k-white after a trace has run to fixpoint, every pointer to it must have
been overwritten before the trace read the containing slots -- and each such
overwrite shaded some displaced object, which is reported k-work.  So either
fresh k-work arrives, re-opening tracing at a cost proportional to that work
(not to the heap), or no reachable k-white object exists.

The gates that decide "no more work is coming" are stage 3's, argued in 4.8:
k-black acknowledged (since + 2), a full quiet window since the last
received k-work, and at least one trace pass after that work.  The original
version of this section derived a termination search over relaxed heap
observations and scan histories -- the subtlest code in the collector, where
an off-by-one lived -- and stage 3 deleted it in favor of report-stream
arithmetic.

The saving graces, in any version: TSan understands the release/acquire
report channel natively, and if any k-gray survives to the sweep, the
sweep's per-object check traps it.

Eventually:

Mutators are k-black.
Objects are k-white or k-black; none are k-gray.
New objects are allocated k-black.
Mutators reach only k-black objects and perform no k-shades.

### 4.4 Sweep: delete the k-whites

Trigger: k is SWEEPING and the shared sweep walk runs (one walk per
iteration serves every concurrently-sweeping bit; 4.10).  The walk visits
the mature cohort plus the nursery cohorts eligible for some sweeping bit,
and for each object:

- deletes it if it is white for ANY sweeping bit -- that bit is past its
  quiet gate, so its whiteness alone proves permanent unreachability --
  asserting the root count is zero (the standing S1 oracle);
- asserts it is not gray for any sweeping bit (tracing termination said
  none exist; one here is a bug);
- otherwise strips any flagged clearing bits and retains it, now certified
  marked for every bit the walk swept (the certificate 4.11 builds on).

Objects allocated after the sweep gate were born k-marked and are skipped
wholesale via their cohort's min_epoch; objects not yet reported are
untouchable by construction (they are in no cohort).

### 4.5 Publish k-white (unpublish k)

Once the sweep has deleted the k-whites:

All mutators are k-black.
All objects are k-black.
Mutators are making new k-black objects.
Mutators perform no k-shades.

At this point the k-bits have served their purpose and the collector no
longer assigns meaning to them.  The collector publishes new colors with
the k-bits clear.

Some mutators are k-black, some are k-white.
New objects are k-black or k-white.
k-black mutators shade (for k) objects newly made by k-white mutators;
this is harmless.
Objects of all k-colors exist.

Eventually: the epoch advances by 2.

All mutators are k-white.
No mutators are shading k-white to k-gray.
New objects are k-white.
Objects of all k-colors exist.

Reports from mutators that loaded their colors before the white publish can
still arrive carrying k-marked allocations.  The strip horizon (4.10) flags
their cohorts at receive, so those stale marks are stripped before the bit
can recycle.

### 4.6 Clear k from all surviving objects

Clearing rides the sweep (4.10).  At the WHITE_PUBLISHED -> CLEARING
transition -- a bare $E \ge F + 2$, sufficient because after it no mutator
can shade k, so no report can ever carry fresh k-work -- every cohort old
enough to carry k-marks is flagged `needs_strip`, and the transition records
the strip horizon against which late-arriving cohorts are flagged at
creation.  Each subsequent sweep walk strips k from the survivors it visits
and clears their cohorts' flags.  There is no dedicated clearing pass, no
meaning-flip of the bit sense, and the mutator barrier stays one idempotent
`fetch_or`.

### 4.7 Recycle k

CLEARING -> UNUSED fires when no cohort holds k in `needs_strip`: every
object is k-white again, all mutators are k-white, and bit k returns to the
unused pool -- we are back at the beginning of 4.1.  The per-object color
checks trap any object still carrying a supposedly-unused bit.

### 4.8 Stage-3 revision: immediate reports and exact termination

*(Implemented 2026-07-12.  Supersedes the report-embargo machinery and the
scan-history termination search that 4.3 originally derived; the phase
narrative above otherwise stands.)*

**Immediate reports.** The report push is now a release (a local `expected`,
so the successful CAS is the last write the mutator makes to the report),
and the collector's exchange is an acquire.  Consequences:

- Report contents -- and everything the mutator wrote before publishing,
  including the `_gray` words of the objects it shaded and the headers of
  the objects it allocated -- are readable immediately.  The embargo deque,
  `_finalized`, `_scan_history` and `_shade_most_recent` are deleted.
- Because the exchange is a read-modify-write, it always reads the head's
  latest modification-order value: one exchange takes every report
  published so far.  Every completeness argument reduces to the pattern
  *gate, then exchange, then decide*.

**Completeness lemma.** If X happened at epoch $Q$ (a color publish; the
last received k-work), then once the collector is pinned at $E \ge Q + 2$,
every mutator has repinned since $Q$ -- the epoch cannot reach $Q+2$ while
a $Q$-pinned thread remains -- and each mutator's report push is sequenced
before its repin.  The per-iteration exchange therefore already received
every report a mutator published before adopting the state change at $Q$.
The two atomics (epoch, report head) need no joint consistency: each
conclusion rides exactly one edge.

**Shadelists.** `garbage_collected_shade` records the object into a
thread-local bag exactly when its `fetch_or` flipped a bit (record-once;
no cross-thread duplicates).  Reports carry the bag; the collector drains
arrivals into the gray wavefront at the top of each scan, promoting
gray to black only for bits in blackening phases (4.1's no-early-black
rule), routing each entry by the object's *current* `_gray` word.

**Termination (replaces the original 4.3 search).** Bit k leaves
BLACK_PUBLISHED when:

1. $E \ge \mathrm{since} + 2$: every mutator allocates k-black, and (by the
   lemma) every k-gray warm-up allocation has been received;
2. $E \ge \mathrm{k\_last\_work}[k] + 2$: a full quiet window -- any
   mutator that flipped k has since reported, so an unreported k-flip
   cannot exist; a *future* flip requires reaching a k-white object, which
   contradicts trace completeness by the snapshot induction;
3. at least one scan completed after the last k-work was received, so that
   work is traced to fixpoint and rooted-but-unshaded objects were grayed
   by the scan's root check.

`k_last_work` is fed by `gray_did_shade`, whose initialization at color
load (`gray & ~black`) also flags the continued existence of k-gray
*allocating* mutators, so the quiet window cannot open during warm-up.
WHITE_PUBLISHED -> CLEARING likewise becomes a bare $E \ge F + 2$: after
it, no mutator can shade k, so no report can ever carry fresh k-work
(a flip requires a zero gray bit; post-sweep every object is k-black).

**Cost accounting (why release/acquire here is free).**  On x86-64 a
release CAS is the same instruction as a relaxed CAS; on AArch64 it is
nearly free.  The publish runs once per quiescence per thread, not per
heap operation, so it spends essentially none of the mutator-burden
currency -- the relaxed-everything discipline predated that exchange-rate
observation.  The shadelist push costs one predictable branch plus a push
charged per white-to-gray flip, at most once per object per bit per cycle;
at persistent-structure mutation rates it rounds to zero.  Bonus: the
report channel became ordinary release/acquire edges that ThreadSanitizer
understands natively, instead of epoch-theorem edges it cannot see.

### 4.9 Stage-4 revision: root and weak registries

*(Implemented 2026-07-12.  The full pass still ran at this stage; its
per-object count check was retained as the differential oracle for stage
5.)*

**Roots.**  The 0 -> 1 root-count transition now files the object, through the
report channel, into a collector-side root registry.  The registry answers a
question transitions cannot: what was already rooted when a cycle started.  It is
walked at the top of every scan: entries observed with count zero are
dropped (the 1 -> 0 drop shaded; a re-root files a fresh event), and live
entries are grayed for every active collection and promoted into the
wavefront.  In-cycle root-ups of already-marked objects need no registry
help; root-ups of white objects are shades.  The rescue counter -- the
pass's count check graying a rooted object the registries had not --
reads zero across the full suite (6,405 passes); stage 5 required that
before deleting the count check.  The sweep now asserts count == 0 on
every deleted object: rooting requires a reachable pointer, and nothing
reachable is white at sweep, so this is the direct S1 oracle.

**Weak.**  Objects with a nontrivial weak decision (WeakHolder) register
at construction, through the report channel, into a weak registry; the
weak-decision walk visits only that registry, replacing the per-object
virtual dispatch in the pass body.  An entry is dropped exactly when the
current pass's sweep is about to delete it (white for every sweeping bit
and unrooted, so the pass cannot rescue it), so the registry never holds
a dangling pointer.

Incidentally exposed: prompt reclamation makes malloc address reuse
routine, so any test asserting collected-ness by address inequality is
invalid (the interning test now asserts on emplaced-vs-upgraded instead;
ASan's free-quarantine had been masking this in Debug).

TODO: Manipulation of the weak table is the only operation requiring the
collector to create GC objects and thus requiring a pinned epoch.  We could
be unpinned during other operations, and trivially pin-unpin to establish
orderings.

### 4.10 Stage-5 revision: cohorts; the full pass retired

*(Implemented 2026-07-12.  The collector loop is now: receive, advance
phases, trace (O(new work)), and -- only when some bit is SWEEPING -- one
sweep walk.  The full pass, and with it the per-object count check, is
gone; the sweep's count == 0 delete assert is the standing S1 oracle.)*

**Cohorts.**  The known heap is a birth-ordered deque of cohorts, one per
receive that carried allocations, tagged min_epoch = min(report H) - 1 (a
report covers allocations since its publisher's previous quiescence).
Sweep gate: _k_sweep_gate[k] = black-publish + 2; a cohort with
min_epoch at or past the gate holds only objects born k-marked and is
skipped by k's sweep.  Visited cohorts fold into the single mature cohort
-- their birth order is older than every future gate, so the distinction
is spent -- and the deque stays a handful of entries long.

TODO: (4.11) The birth order is a less precise way of ordering cohorts than the
mutator's allocation color; the distinction that matters is "known to be
k-black" and "may not be k-black".  If the mutator is allocating k-black, during
a reporting period, all new allocations are k-black and don't need to be k-swept.   

**Sweep.**  One walk per cycle (shared by every concurrently-SWEEPING
bit) visits mature plus the eligible nursery prefix: deletes whites
(asserting count == 0), strips every cohort-flagged CLEARING bit from
survivors, and folds the survivors into the mature cohort.  
The delete predicate is any-bit: white for SOME
sweeping bit suffices, because that bit is past its quiet gate and its
whiteness alone proves permanent unreachability -- blackness for a
concurrently sweeping bit records reachability only at an older snapshot
and cannot resurrect.  (Requiring white-for-all, as the full pass did,
merely floated the k-black-j-white dead object one extra cycle.)  A
consequence used by 4.11: surviving a walk certifies the object marked
for every bit the walk swept.  SWEEPING -> WHITE requires the walk;
WEAK's per-iteration registry walk (whose doom test mirrors the any-bit
predicate) and the trace's quiet accounting are unchanged.

TODO: (4.11) If we k-sweep, and j-gray is published but we but are not yet j-black-quiescent,
k-black survivors may be observed to be j-gray or j-black.
(k-white and j-nonwhite object is forbidden when both bits are live and j is after k in publication order.)
j-nonwhiteness at this point guarantees surviving the j-sweep, but j-whiteness does not yet guarantee unreachability.
The notion of a mature cohort thus discards useful information that we don't
need to sweep such objects; instead we should maintain up to 16 cohorts that
group the objects by the oldest live bit that is not known to be non-white.
When doing a sweep of quiescent bits, we have one cohort of each bit, which we
sweep and distribute to the cohorts of non-quiesecent bits according to the
survivor's oldest live white bit.
 

**Clearing rides sweep.**  On WHITE -> CLEARING, k is flagged
(needs_strip) on every cohort old enough to carry it; the next sweep walk
strips it; k recycles (CLEARING -> UNUSED) when no cohort is flagged --
at most about one further cycle.  No meaning-flip, no dedicated pass, and
the mutator barrier stays one idempotent fetch_or.

The flagging is NOT one-shot: a mutator that loaded its colors before k's
white publish can deliver k-marked allocations in a report that arrives
after the transition's flagging pass ran (its cohort then carried stale
k-marks past the recycle, and the bit was reused under them -- caught by
the b-check as NONWHITE on a freshly recycled bit).  So the transition
records a strip horizon (_k_strip_before[k] = white publish + 2) and
cohort creation flags newborn cohorts against the horizons of every
currently-CLEARING bit.  This is airtight by ordering: a pre-ack pin
blocks the epoch, so the straggler's report is received -- and its cohort
flagged -- before the try_advance that could retire k, receive running
first in each iteration.

**Warm-up deferral.**  With no pass to re-discover them, objects grayed
for a bit still in gray warm-up (Yuasa shades of old objects, gray-born
allocations, weak revivals) park in _deferred_warmup at promotion, and
each GRAY -> BLACK transition re-feeds the bag through the arrivals
drain.  Whatever grayed them for the warm-up bit also grayed them for
every sweeping bit, so parked entries always survive intervening sweeps.
The WeakHolder revival path now routes through the standard shade channel
(it runs on the pinned collector thread) instead of a bare fetch_or, for
exactly this reason.

**Nobody else may gray a warm-up bit.**  The root registry walk
originally grayed entries with the full gray mask (4.1's optional early
shade).  That is a trap under record-once shadelists: the walk cannot
blacken a warm-up bit and does not defer, and if the entry leaves the
registry (count -> 0) before the bit blackens, the exit shade's fetch_or
finds the bit already gray and files nothing -- orphaning the object
gray-not-black (observed as a sweep-time GRAY violation on the per-frame
dropped World).  The walk now grays only bits it can also blacken; the
snapshot point is black-publish, so nothing is lost, and an early exit
routes through the ordinary shade channel.  The general rule: a gray bit
may be set only by (a) an agent that also blackens it, (b) an agent that
parks the object in _deferred_warmup, or (c) the mutator shade path,
whose record-once filter then files the object exactly once.  The
`gc_root_churn` test (root, hold across quiescences, drop, across many
cycles) is the tripwire.

Steady state observed in the suite: one sweep walk per cycle over the
whole small heap, cohorts = 1 (mature), stripped masks riding each walk,
traces proportional to reported work.

### 4.11 Design note: certificate-gated cohorts (not implemented)

*(Filed 2026-07-12; revisit after the 100k stress baseline, if sweep
bandwidth shows up hot.)*

Within a cycle, marks are set-only -- a bit's strip strictly follows its
own sweep -- so any mark OBSERVED during a sweep walk is stable through
that bit's sweep: "observed j-gray" certifies that j's visit could not
delete the object.  This generalizes the cohort key from birth epoch to a
**certificate mask**: bits for which every member is known marked.  Sweep
for j skips cohorts whose certificate contains j.  Two certificate
sources, one mechanism:

- **Birth**: born after gate(j) implies born j-marked; min_epoch is a
  compressed certificate issued without looking at the object.
- **Observation**: the sweep walk already loads every survivor's color
  word; evacuation writes the observed (stable) bits into the target
  cohort's certificate.

Skipping certified visits adds NO reclamation latency: a skipped visit
provably could not delete, and the first bit whose snapshot postdates a
death also postdates the certificate, so it visits and deletes on
today's schedule.  Expected win: mature is re-observed about once per
pipeline depth instead of once per cycle -- sweep bandwidth divided by
roughly the depth.  This is the sound form of the generational instinct:
survival cannot confer newness, but observation confers exactly the
certificate that licenses visit-avoidance, and it is free at sweep time.

Wrinkles:

1. Bits past their quiet gate certify uniformly (trace-complete: live
   implies marked); bits still tracing do not (an unreached live object
   is indistinguishable from garbage).  Start with post-quiet-only
   certificates and a single evacuation bucket; bucket by observed mask
   only if measurements ask for it.
2. Clearing rides sweep, and certified cohorts skip sweeps, so a retired
   bit's strip -- hence its recycle -- can wait about pipeline-depth
   cycles, spending the 16-bit headroom faster.  Mitigate with a
   strip-only walk of flagged-but-skipped cohorts when UNUSED bits run
   low, or with wider color words.
   
TODO:  We argue (somewhere?) that we want to limit the number of cycles in
flight, so this delayed recycling is less alarming.  It may even be helpful.

TODO: We need to route every survivor to a cohort based on
- subset of gray bits (live not quiet)
- first 0 bit in that subset under an arbitrary ordering
This needs to be somewhat efficient since it gets run per survivor.
Not an obvious bithack for it.  If we constrain the bits to live and die in strict
bit-position order--not obviously a problem, and it might be also good for our
sanity--it becomes a rotate and ffs

Prerequisite (landed with the any-bit delete predicate in 4.10): a
walk's survivors are uniformly certified for every bit the walk swept.

---

## 5. Throughput model

*(Stage-1 dashboard, 2026-07-12: per-pass `alloc+=`/`shaded+=` volumes and
per-cycle `passes=P in T` lines.)*

Variables: mutators quiesce every $T_m$ (one frame); each period allocates
$N$ objects and unlinks $N$; live set $L$; heap $M = L + F$ where $F$ is
floating garbage; scan rate $S$ objects/second (a decreasing function of
$M$ once the heap outgrows cache); pass time $T_p = M/S$; passes per cycle
$P$; allocation rate $A = N/T_m$.

Two regimes, split by whether a pass outlasts the mutator cadence:

**Idle ($T_p < T_m$).**  The loop's trailing `epoch::wait` blocks, so
iterations -- and passes -- run 1:1 with epoch advances, and the epoch
advances at mutator cadence.  A cycle therefore costs its *epoch budget*,
one (trivial) pass per epoch: measured 20-24 with the stage-2 embargoes
(three +3 finalization waits, plus the ack and quiet windows), 7-9 after
stage 3 removed the embargoes.  Collector CPU is $\sim P \cdot T_p$ per
$P \cdot T_m$ of wall clock -- negligible.  A high idle $P$ is an artifact,
not the loaded-regime multiplier.

**Loaded ($T_p > T_m$).**  Epoch gates hide inside passes (each pass spans
$T_p / T_m \ge 2$ epochs), so $P$ collapses to the *mandatory-scan floor*:
trace pass(es), weak, sweep, clear -- about 4 after stage 3, which also
removed the scan-history retry tail (each disqualified candidate scan was
a full pass).  Then:

$$T_c = P M / S, \qquad F \approx c A T_c, \; c \in [1, 2]$$

(garbage born during cycle $n$ is snapshot-protected and sweeps in a later
cycle), giving the equilibrium

$$M = \frac{L}{1 - cPA/S(M)}$$

which exists only while $S(M) > cPA$.  Because $S$ *decreases* in $M$,
there is a stable fixed point at small $M$ and a knee above it; a heap
transient (e.g. bulk world construction dumping millions of path-copy
nodes) can push $M$ past the knee permanently, after which there is no
equilibrium at any load -- the observed death spiral (100k stress: $S$
fell 11.5M/s -> 2.25M/s as $M$ grew 8M -> 35M).

Stages 4-5 (root/weak registries, cohort-segregated sweep, clearing folded
into sweep) remove the per-pass root scan and shrink the walked set to
(live + one short cycle's allocations), replacing the condition with
"streaming sweep rate > allocation rate", with no $M$-dependent knee.

**Post-stage-5 loaded baseline (100k stress, Debug+ASan, 2026-07-12):**
with $P = 1$ the binding term moved from sweep to TRACE.  Snapshot
collection marks each object about once per cycle it survives, and
floating garbage survives 1-2 cycles, so mark work per second is
$\approx (1..2) A$ plus live/cycle -- observed as marked $\approx$
alloc+= on every iteration.  Debug mark rate ($\approx 3$M/s) landed at
parity with the allocation rate ($\approx 2.8$M/s): $cA/S \approx 1$, no
equilibrium, heap 8M -> 80M over two cycles, cycle time 143 s.  Sweeps
stayed healthy (23M obj/s early, 8M/s at 20M heap) and the construction
burst was absorbed in one cycle (7.2M of 7.9M deleted in 0.4 s).  The
mutators held frame rate throughout -- never-block did its job; the
collector lost the race silently.  Release changes both sides ($A$ is
frame-capped, $S$ gains the optimizer); measure before optimizing.

**Release baseline (same world, 2026-07-12): STABLE.**  Trace 10.9M
marks/s, sweep 25M visits/s, allocation 0.7M/s; heap steady at 1.87M
objects = live 1.05M + one cycle's float ($c \approx 1$); cycle 1.69 s
at the 9-iteration phase floor.  One collector thread at 10x the design
target.  The residual: marked-per-cycle is ~6x the heap, because each
in-flight bit marks the reachable set independently and cycles start
eagerly (depth 5-6) -- collector duty ~65%, true headroom ~1.5x.  With
1.69 s cycles, pipelining only needs depth ~2 to hide handshake latency,
so a **cycle admission cap** (do not start a new bit while two are in
flight; subsumes idle throttling when alloc is zero) divides mark work
by ~3 for negligible float cost.  That is the next lever; 4.11's
certificate cohorts stay in reserve (sweep is ~4% of the cycle), and
parallel trace remains unneeded at this scale.

---

## 6. Literature comparison

A pass at locating this design in the published GC literature.  Names and
claims are best-effort; cross-check before quoting.

### What this system mechanically is

- Concurrent mark-sweep, non-moving, non-generational.
- Tricolor (white/gray/black), with the twist that gray and black are
  encoded as *separate bits per "k-collection"* -- supporting up to 16
  overlapping concurrent collections distinguished by bit position.  Most
  schemes have one collection at a time.
- **Yuasa-style deletion barrier** -- shades the displaced pointer on
  overwrite, via unconditional `fetch_or` on the gray bit (idempotent on
  already-non-white objects, so equivalent to Yuasa's classic conditional
  "if white, shade gray").
- Per-mutator-thread allocation bags (`_thread_local_new_objects`),
  reported to the collector at quiescence boundaries.
- Phase transitions are **ragged**: each mutator observes a phase change
  at its own next pin/repin boundary, not at a synchronized handshake.
  The collector waits *enough epochs* before relying on a phase change
  being globally visible.
- Report publication is an ordinary release/acquire channel (since stage
  3, 4.8); the epoch system's dereference embargo remains for bump-slab
  rotation and for bounding the raggedness of color publication.

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

- **Phase synchronization mechanism.**  DLG uses synchronous handshakes --
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
describes itself as a "gray-stack Dijkstra accurate non-moving"
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
bounded retirement, advance-when-quiescent.  We use it for the
phase-transition raggedness bounds and for the epoch allocator's slab
retirement; the mutator reports originally rode it too, before becoming
a plain release/acquire channel (4.8).

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
to a specific paper but feels like an unwritten generalization --
multiple instances of the otherwise-standard scheme, packed into the
same gray/black word at distinct bit positions.

### Bibliography of close cousins

- Doligez & Leroy, *A Concurrent, Generational Garbage Collector for a
  Multithreaded Implementation of ML*, POPL 1993 -- primary DLG.
- Doligez & Gonthier, *Portable, Unobtrusive Garbage Collection for
  Multiprocessor Systems*, POPL 1994 -- the "unobtrusive" handshake
  formulation closest in spirit to our ragged style (still synchronous
  though).
- Yuasa, *Real-time garbage collection on general-purpose machines*,
  Journal of Systems and Software, 1990 -- the deletion barrier we use.
- Pizlo et al., *Schism: Fragmentation-Tolerant Real-Time Garbage
  Collection*, PLDI 2010 -- first use of "ragged safepoints"; closest
  spirit to our phase-transition mechanism.
- Pizlo, *Fil's Unbelievable Garbage Collector* (https://fil-c.org/fugc) --
  modern descendant; explicit DLG comparison; uses Dijkstra not Yuasa.
- Vechev, Yahav & Bacon, *Derivation and Evaluation of Concurrent
  Collectors*, ECOOP 2005 -- formal taxonomy that classifies our scheme as
  an instance of "incremental snapshot-at-the-beginning," close to but
  distinct from incremental update.
- Pirinen, *Barrier techniques for incremental tracing*, ISMM 1998 --
  classic survey separating "what is preserved" by a barrier from "how"
  it preserves it.
- Osterlund, *Block-free concurrent GC: stack scanning and copying*,
  ISMM 2016 -- modern asynchronous-handshake stack scanning; ZGC parts of
  the lineage.
- Turon, *Lock-freedom without garbage collection* (2015,
  http://aturon.github.io/tech/2015/08/27/epoch/) -- accessible
  epoch-based reclamation explainer; close to our epoch service shape.
- Jones, Hosking & Moss, *The Garbage Collection Handbook*, 2nd ed.
  2023 -- the design lineage by direct admission of the author: pick the
  options that place the least compute burden on the mutator.

---

## 7. Worst-case interleavings (unfinished)

A scaffold that predates the stage 3-5 revisions, kept as exercises: for
each scenario, draw the per-thread timeline with epoch labels and bit
values, and pinpoint the rule from section 4 that prevents the bug.  Two
earlier scenarios here -- the root count dropping to zero between
root-removal and scan, and k-bit reuse seeing stale set-state -- were
answered outright by 4.9 (shade on both count edges + the registry) and
4.10 (the strip horizon) and have been retired.

### 7.1 Leading mutator allocates k-white, trailing mutator shades k-gray

> TODO.  The machinery that answers it: warm-up deferral (4.10) and the
> completeness lemma (4.8).

### 7.2 Mutator shading races collector marking on `_gray`

Both threads RMW `_gray`. The mutator's `fetch_or(k)` and the collector's
`compare_exchange_weak` must compose correctly.

> TODO: trivial because OR is commutative and idempotent, and the
> collector's CAS retries on interference -- but write it out to confirm
> no bit-loss is possible.

---

## 8. Open questions

- **Idle behavior.**  When there is nothing to collect, the loop still
  runs a cycle every few epochs (admission is eager: each pass starts the
  first unused bit).  The planned admission cap (section 9) subsumes idle
  throttling.  Liveness also assumes mutators keep repinning -- L2's
  practical caveat.
- **How many overlapping collections?**  k = 16 is pipeline depth.
  Measured depth under load is 5-6 with eager admission, while ~2
  suffices to hide the handshake latency (section 5); if four bits are
  enough, the color words shrink to a byte.  The overlap is not the
  source of the complexity, but it doesn't help.  Sure is cool though.
- **Is the reasoning about the range of epochs over which relaxed gray-bit
  stores can be observed actually valid?**  Somehow it feels different
  from acquire-release of nonatomic writes, but maybe it isn't.  (Less
  now rides on it: shades travel in the release/acquire reports, and the
  heap gray word is confirmation rather than the primary channel.)
- **Tests and testability.**  What exists: the per-object color checks
  (Debug) validate every visited object's word against its bits' phases;
  the sweep's count == 0 delete assert is the standing S1 oracle;
  `gc_root_churn` and the churn tests are tripwires for the registry and
  pin-continuity contracts; TSan sees the report channel natively
  (fences over release sequences still need the annotation rule).  What
  is missing: a stress that deliberately saturates all 16 bits, and
  adversarial-scheduler coverage of the ragged windows.

---

## 9. Future directions

- **Cycle admission cap** -- the next lever (section 5): do not start a
  new bit while about two are in flight.  Divides mark work by ~3 at the
  100k load for negligible float cost, and subsumes idle throttling when
  the allocation rate is zero.
- **Certificate-gated cohorts** -- 4.11, in reserve until sweep bandwidth
  shows up hot (it is ~4% of a cycle today).
- **Orphaned-slab handoff** -- a slab field on a thread's final `Report`
  (openable at $E \ge F + 3$, exactly the rotation bound), the crossbeam
  SealedBag analog; replaces leaking the last bump slabs at thread exit
  and would also yield a general schedule-after-epoch.
- **Parallel trace** -- the graystack is already the natural work queue;
  not needed at 10x the design target on one thread.
- **Partial scans** -- valued for making the epoch cadence
  heap-size-invariant; deferred while heaps are modest.

### 9.1 Non-goal: stalled-thread robustness

The post-Crossbeam reclamation literature (interval/era-based
reclamation, Hyaline and its descendants) makes a stalled reader retain
only the garbage born during its own stall window, instead of freezing
reclamation globally.  We record explicitly why we are NOT pursuing
that:

- A thread that stalls while doing work for the next frame is already a
  system failure at the game level.  The bounded thread pool is designed
  not to saturate the machine and to let its threads evict each other;
  stalls are treated as bugs, not tolerated states.  Era-robust memory
  would mask the symptom without saving the frame.
- More fundamentally, the write barrier is priced against non-stalled
  mutators.  Tracing concurrently with a genuinely stalled mutator
  requires the shade to happen-before the pointer overwrite at the
  granularity of each individual barrier execution (release on the
  `_gray` fetch_or, acquire on the collector's reads), not at the
  granularity of epoch-batched reports.  That converts a per-quiescence
  ordering cost into a per-write one, which is exactly the mutator
  burden this design exists to avoid.

The epoch is the collector's clock as well as its grace period; both
assume every participant keeps ticking.  Diagnosis of violations, not
tolerance of them, is the chosen posture (see `ThreadPublic` and the
drain-loop pin-continuity contract in `global_work_queue.cpp`).
