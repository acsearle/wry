# Transactional world step

## Purpose

`Transaction` is the conflict-resolution engine for a single simulation tick.
It is *not* a general software transactional memory with optimistic read-set
validation. It is narrower and cheaper: every ready entity proposes a set of
writes, all conflicts over a given location are decided by a fixed priority
order, and exactly one writer per location wins. The whole tick is a pure
function of the previous `World`, so the simulation stays deterministic
(see `game/docs` on determinism).

Read this alongside `World::step()` in `game/world.cpp`. `transaction.hpp`
defines the data structures; `World::step()` is the driver that gives them
meaning, including the barrier the memory-ordering argument depends on.

## Naming and prior art

"Transaction" is earned -- atomic all-or-none commit, explicit abort,
write-conflict detection -- but "software transactional memory" is a misleading
anchor, and the two concepts this system actually rests on describe it better.

Classic STM (Shavit and Touitou; TL2; etc.) resolves conflicts by *timing* --
first committer wins, lock acquisition order, version-clock validation -- and is
therefore nondeterministic. This system is the opposite: conflicts resolve by a
*static total order* (priority) fixed before resolution runs, and determinism is
the whole point (replicas must replay to identical state for multiplayer
lockstep). There is also no optimistic read-set validation, because every
transaction reads the *same immutable previous `World`* -- there is nothing to
revalidate. So the STM machinery people expect (per-thread read/write sets,
contention managers, retry-on-conflict within the attempt) is simply absent.

What it is instead:

1. **Multiversion concurrency control / snapshot isolation.** Each `World` is an
   immutable version N; `step()` produces version N+1; every transaction in tick
   N reads version N and the batch commits atomically at the version boundary.
   That is textbook snapshot isolation, with one substitution: the write-write
   conflict rule is "lowest priority wins" rather than "first committer wins."
2. **Deterministic transaction ordering (the Calvin lineage).** Deterministic
   databases agree an execution order *up front* so independent replicas reach
   identical state without coordination. The per-tick priority *is* that
   sequencer; peers replaying the same order is the late-join / lockstep story.
   This is a far better intellectual anchor than STM.

Layered on top, the producer side -- Entities as logical "threads" issuing
writes to shared world "memory" -- is essentially the actor model with a
transactional commit step. That framing is what makes "memory" a defensible word
for the World state, even though it is really a versioned key-value store.

### How the multiplayer plan maps onto Calvin

The planned multiplayer model lands on Calvin's two layers directly:

- Calvin's *sequencing layer* (agree a global order of transaction inputs,
  replicated to every node) is the authoritative server: clients send user
  actions, the server orders them and broadcasts an ordered action log. This is
  low volume -- human-rate input -- which is why a single sequencer is cheap.
- Calvin's *deterministic execution* (run in the agreed order, no further
  coordination) is the per-tick priority resolution. The order is
  `hash(EntityID, time)`, which every client recomputes identically from
  already-agreed state, so the high-volume entity-vs-entity conflicts need no
  network round trip at all.

Net: only the cheap ordering (user input) crosses the wire; the expensive part
-- thousands of entities contending per tick -- is a pure function every client
evaluates locally and identically.

This holds only if EntityID *assignment* is deterministic across clients, not
merely unique. Priority is a function of EntityID, so if two clients give the
"same" freshly spawned entity different IDs their priority orders diverge and
state desyncs -- and the collision panic in `resolve()` will not catch it,
because each client is internally consistent while disagreeing with its peer.
Uniqueness is the single-machine invariant; cross-client determinism of
assignment is the multiplayer one, and they are separate. Entity creation
therefore has to be a deterministic function of already-agreed state (in Calvin
terms, it flows through the same ordered log). `EntityID::oracle()` in
`entity_id.hpp` flags this as a deferred open problem.

Preferred framing: deterministic + transactional + versioned (MVCC). Treat
"STM" as a loose colloquial handle only, and if it is used in prose, qualify it:
resolution is by static priority over an immutable snapshot, not by timing.

References:

- Alexander Thomson, Thaddeus Diamond, Shu-Chun Weng, Kun Ren, Philip Shao,
  Daniel J. Abadi, *Calvin: Fast Distributed Transactions for Partitioned
  Database Systems*, SIGMOD 2012. The deterministic-ordering thesis this system
  is an instance of.
- Hal Berenson, Phil Bernstein, Jim Gray, Jim Melton, Elizabeth O'Neil,
  Patrick O'Neil, *A Critique of ANSI SQL Isolation Levels*, SIGMOD 1995. The
  canonical definition of snapshot isolation.

## The tick is two phases separated by a barrier

```
World::step(old_world):
  -- Phase 1: PROPOSE (parallel over ready entities) -----------------
  for each ready EntityID, in parallel:
      entity->notify(context)
          builds a Transaction, calls verbs (write_*/wait_on_*),
          each verb appends a Node onto a per-key lock-free list
  -- BARRIER (parallel_for_each join) -------------------------------
  -- Phase 2: RESOLVE + REBUILD (per contended key) -----------------
  for each key touched by some transaction:
      walk its conflict list, resolve() each node
      the single committed writer's value goes into the new map
      waiters / displaced occupants are queued into next_ready
  -- BARRIER (nursery join) -----------------------------------------
  return new World{ new_time, new maps..., new schedule }
```

Phase 1 only *mutates* the proposal graph. Phase 2 only *reads* it (plus the
per-transaction `_state` cache). Nothing in Phase 2 races with Phase 1, and
that separation is the entire basis for the relaxed atomics below.

## The proposal graph

Each verb call produces a `Transaction::Node`. Nodes are doubly indexed:

```
Transaction (one per ready entity)
  _entity, _context, _state
  _nodes[_size]  ------------------+   (this transaction's proposed actions)
                                   |
TransactionContext::Map<Key>       |   (one lock-free list per touched key)
  key --> Atomic<Node*> head       |
              |                     |
              v                     v
            Node._next chain  <-->  Node._parent
              Node._head -> &head (the atomic this node is linked into)
              Node._desired : ExternallyDiscriminatedVariant<8>
              Node._operation : int (WAIT_*/WRITE_ON_COMMIT bitset)
```

- Walk `_parent->_nodes` to see everything one transaction wants to do.
- Walk a key's `head -> _next -> ...` to see every transaction contending for
  that key.

There is one `Map` per (verb, key-type) pair in `TransactionContext`:
`_verb_value_for_coordinate`, `_verb_entity_id_for_coordinate`,
`_verb_entity_for_entity_id`, and `_wait_on_time`. The map is an
`EpochDiscipline` concurrent skiplist; its entries are address-stable, so a
node can cache `&head`.

### `ExternallyDiscriminatedVariant`

`_desired` is 8 raw bytes with no type tag. The discriminator is *which map the
node lives in*: a node in `_verb_value_for_coordinate` holds a `Term`, one in
`_verb_entity_for_entity_id` holds an `Entity*`, and so on. Hence "externally
discriminated." Hard constraint: every payload type must be trivially copyable
and fit in 8 bytes (`get<T>` / `operator=` are `memcpy`). `Term`, `EntityID`,
and `Entity*` satisfy this today; a wider write value would need indirection.

## Verbs

A verb is one proposed action. `transaction_verb_generic` is the shared body:
allocate the next `Node`, fill in `_desired`/`_operation`, then race to
`try_emplace` the key (initializing the atomic head to this node if we win) or,
if we lost the emplace race, CAS-prepend onto the existing head.

`_operation` is a bitset:

```
WAIT_NEVER       = 0
WAIT_ON_COMMIT   = 1
WAIT_ON_ABORT    = 2
WAIT_ALWAYS      = 3
WRITE_ON_COMMIT  = 4
```

- **Exclusive verbs** set `WRITE_ON_COMMIT`: at most one such node per key
  commits and writes its value; all other writers to that key abort.
- **Non-exclusive verbs** (`wait_on_*`) only observe: a waiter asks to be
  requeued next tick when the condition it named (commit / abort / either)
  occurs on that key, but it never blocks anyone.

`write_*` defaults to `WRITE_ON_COMMIT`; `wait_on_*` defaults to
`WAIT_ON_COMMIT`. `on_commit_sleep_for(n)` and `on_abort_retry()` are sugar
over `wait_on_time`.

## Priority and conflict resolution

`State` is `INITIAL | COMMITTED | ABORTED`, stored in `Transaction::_state`.

Priority is `hash_combine(entity_id, world_time)`. **Lower number means higher
priority.** Because it is re-salted with the time every tick, an entity that
loses this tick is likely to win a future one, so no entity starves
indefinitely.

### Why priorities are unique

Uniqueness is not luck; it is guaranteed by construction. The priority is

    priority(id, t) = hash( hash(id) ^ t )

where `hash` is the scalar mixer in `core/hash.hpp` (Numerical Recipes 7.1.4),
which is *invertible* on 64 bits -- a reversible / bijective integer hash, the
standard term for a function that maps a fixed-width integer domain onto itself
with no collisions. Composing bijections yields a bijection: `id -> hash(id)`,
xor with the fixed `t`, and the outer `hash` are each bijective, so for any
fixed time `priority(., t)` is a permutation of the EntityID space. Distinct
EntityIDs therefore always receive distinct priorities within a tick.

The chain that must hold:

1. EntityIDs are unique and never reused (see `entity_id.hpp`).
2. `hash(uint64_t)` is injective, hence bijective on 64 bits.
3. Therefore `priority(., t)` is injective in the id, so priorities are unique.

If either (1) or (2) breaks, two conflicting writers could share a priority,
neither would abort the other (the resolver uses a strict `<`), and both would
commit to one key -- a mutual-exclusion violation. `resolve()` guards this with
an always-on check (not a debug assert): if it meets a conflicting writer of a
*different* entity at *equal* priority, it prints both EntityIDs and the
colliding priority and aborts the process.

`Transaction::resolve()`:

```
if _state != INITIAL: return it                 // memoized
p = my priority
for each of my WRITE_ON_COMMIT nodes:
    for each other WRITE_ON_COMMIT node on that key's list:
        if other.priority == p and other is a different entity: PANIC
        if other.priority < p:                  // strictly higher priority
            if other.resolve() == COMMITTED: return abort()   // it displaces me
            // else other aborted; keep checking
return commit()
```

Key properties:

- **Termination.** Recursion only ever descends into *strictly* higher-priority
  (strictly lower-number) transactions. Priority strictly decreases along any
  recursion chain, so the graph traversed is acyclic and finite. We never
  resolve equal- or lower-priority neighbors (resolving our own node would
  create a self-cycle; resolving lower-priority ones is unnecessary and would
  reintroduce cycles).
- **Progress.** The unique globally-minimum-priority transaction has no higher
  conflict, so it always commits. At least one transaction commits per
  contended key.
- **Idempotence.** `commit()`/`abort()` are `exchange_relaxed`; the asserts
  guard against ever flipping COMMITTED <-> ABORTED. Resolving twice is a no-op.

## Memory ordering

The load-bearing fact: **Phase 1 happens-before Phase 2**, established by the
`co_await ready.coroutine_parallel_for_each(...)` join in `World::step()` (the
thread pool's task-completion path carries the release/acquire edge). Every
proposal write in Phase 1 is therefore visible to every resolver in Phase 2.

Given that edge:

- The CAS-prepend in `transaction_verb_generic` uses
  `compare_exchange_weak_relaxed_relaxed`. No acquire/release is needed because
  no thread follows `_next` until after the barrier.
- `resolve()` reads `_head->load_relaxed()` and walks `_next` with plain loads
  for the same reason: the list is immutable and fully published by Phase 2.
- `_state` is accessed only with relaxed load/exchange. This is sound because
  **resolution is a pure function of immutable post-barrier state**
  (`_nodes`, `_head`, priorities never change in Phase 2). Two threads
  resolving the same transaction concurrently compute the *same* COMMITTED /
  ABORTED answer; `_state` is a memoization cache that prunes repeated work, not
  a synchronization channel. No happens-before is needed between the racing
  resolvers because they exchange no information through `_state` beyond a value
  they can both derive independently.
- `World::step()`'s rebuild reads each list head with `load_acquire`. That is
  conservative relative to the argument above (relaxed would also be correct
  given the barrier); the acquire is harmless.

## A useful bit trick

`State` and the `WAIT_ON_*` condition bits are deliberately aligned:

```
COMMITTED == 1 == WAIT_ON_COMMIT
ABORTED   == 2 == WAIT_ON_ABORT
```

So during rebuild, `node->resolve() & node->_operation` is a one-line test for
"did the outcome this waiter cares about actually happen?" Keep this alignment
intact if either enum is edited.

## Waiting and scheduling

Rebuild (`coroutine_parallel_rebuild2` over a `WaitableMap`) does three things
per committed write to a key:

1. write the value into the new `kv` map;
2. requeue everyone who was waiting on that key (the `ki` waiter index from the
   *previous* world, plus this tick's `wait_on_*` nodes) into `next_ready`;
3. requeue the displaced previous occupant where relevant (e.g. the entity that
   vacated a coordinate).

`_wait_on_time` is the timer wheel: `wait_on_time(t)` / `on_commit_sleep_for` /
`on_abort_retry` insert `(time, entity_id)` so the entity re-fires on a future
tick. `partition_first(_waiting_on_time, time)` at the top of `step()` splits
out the entities due this tick. `next_ready` is finally merged back into
`new_waiting_on_time` at `new_time`.

## Invariants and assumptions

- Priorities are **unique** per tick, guaranteed by construction (an invertible
  hash of the unique EntityID; see "Why priorities are unique"). The resolver's
  strict `<` comparison relies on it, and `resolve()` enforces it with an
  always-on panic on any equal-priority conflict between distinct entities.
- A `Transaction` is sized once (`Transaction::make(context, entity, count)`)
  and `_nodes` is a flat trailing array; `_size` must not exceed the `count`
  passed at construction. Verbs append without bounds-checking.
- Transactions and their nodes are `EpochAllocated`; they live exactly one tick
  and are reclaimed by epoch advance, not freed individually.
- A committed write's payload type must match the map it lives in
  (externally-discriminated; no runtime tag check).

## Known limitations / sharp edges (as of this writing)

- **Priority uniqueness rests on two invariants, not on chance.** It cannot
  fail probabilistically: `priority` is an invertible hash of the unique
  EntityID, so distinct ids never collide (see "Why priorities are unique").
  It *can* fail if EntityID uniqueness is violated, or if someone swaps in a
  non-invertible mixer. `resolve()` panics hard on a same-priority conflict
  between distinct entities, so such a regression fails fast instead of
  silently letting two writers commit. The debug `assert(!writer)` during
  rebuild remains as a second line of defense.
- **`write_entity_id_for_coordinate` ignores its `operation` argument** and
  hardcodes `WRITE_ON_COMMIT`, diverging from the other write verbs that thread
  `operation` through. Likely unintended; revisit before relying on
  `WAIT_ON_COMMIT` semantics for that verb.
- **Rebuild is serial today.** `coroutine_parallel_rebuild2` /
  `parallel_rebuild` are the "Simple single-threaded implementation" per their
  own TODOs. The per-key resolution is structured to parallelize, but the tree
  rebuild does not yet.
- **Resolution recurses on the C++ stack.** Conflict chains are statistically
  short, but a pathological (or adversarial) chain could grow the stack
  unboundedly. No depth bound or explicit work-stack today.
- Open design questions are noted at the bottom of `world.cpp` (multiple
  independent transactions per entity; coupling the waitset more tightly to
  writes; densifying `next_ready`).

## Open design: deterministic EntityID assignment

New EntityIDs must be assigned by a rule identical on every client (not merely
unique), because `priority = hash(EntityID)` drives conflict resolution and any
divergence desyncs the sim (see `entity_id.hpp`). The scheme:

A transaction needing N new IDs registers the count N into a priority-ordered
structure during the tick. After the freeze barrier, its base is
`start_base + prefix_sum(counts of all higher-priority requesters)`, and
`start_base` advances by the tick total each step (IDs never reused). The
`[base, base+N)` ranges partition the tick's block, so they are unique; the
prefix sum is over the fixed priority order with an associative operator, so the
result is independent of thread schedule and machine. New-entity references in a
transaction's proposed writes are stated relative to `base` and rewritten to
absolute IDs in an assign sub-phase between the barrier and resolution. IDs
burned by aborting transactions are acceptable holes.

### Computing the prefix sum without a build phase

Avoid a dedicated post-barrier "build the index" pass (the barrier is already
ugly; do no bulk work in it). Instead make the priority-ordered structure a
skiplist whose order-statistic augmentation is filled in *lazily by the lookups
themselves*, demand-driven and cooperatively.

Augment each forward pointer with a `span`: the number of level-0 nodes it skips
over. A node's rank is the sum of spans along the search path to it, and a
transaction's base is the rank-based prefix sum of counts. Properties:

- Spans are unused during mutation (plain concurrent inserts, no span traffic).
  Sentinel `span == 0` means "unknown"; a real forward pointer always advances
  at least one node, so a true span is `>= 1`.
- After the freeze, a lookup that needs an uncomputed span drops a level and
  sums the lower-level spans across the gap (recursing where those are also
  unknown), then publishes the completed span. It never stores a running partial
  -- only the final value -- so a reader sees sentinel-or-final, never a torn
  intermediate.
- The race is benign: `span(A -> B) = rank(B) - rank(A)` is a deterministic
  function of the frozen topology, so every thread computing a given span writes
  identical bits. Concurrent writers are idempotent; no CAS needed, a plain
  relaxed store suffices.
- Memory ordering is relaxed. The freeze barrier already publishes the topology
  (nodes, pointers, keys) to every lookup thread; a span is a self-contained
  scalar, and a reader that consumes it then steps to nodes already visible via
  the barrier, so nothing is reachable only through the span store and no
  acquire/release edge is required. (Release/acquire would only improve memo
  sharing -- fewer duplicate computes -- a cache knob, not correctness.)

Cost: the first full-depth lookup computes and publishes the top-down spine in
`O(N)`; later lookups reuse it and only fill the regions their own path drops
into, `O(log N)` each. Aggregate is `O(N)` to populate (each span computed once,
by whoever reaches it first) plus `O(Q log N)` for Q queries; spans on no query
path stay unset. The one rough edge is a cold-start herd: W workers launching
their first lookup together may each redo the top spine before one publishes,
bounded by `O(W * N)` and one-time -- negligible at W = 4.

The memo is per-client and never shared over the wire, and base IDs are
topology-independent (a pure function of the ordered set and the counts), so
clients with differently shaped skiplists still compute identical bases. The
whole lazy/racy memo is invisible to determinism.

## File map

```
core/transaction.hpp   Transaction, Node, TransactionContext, verb declarations,
                       ExternallyDiscriminatedVariant, State/Operation enums
core/transaction.cpp   transaction_verb_generic, resolve/abort/commit, verbs
game/world.cpp         World::step() -- the driver, the barriers, the rebuild
game/entity.hpp        Entity::notify(TransactionContext*) -- producer hook
game/machine.cpp       Machine::notify -- a worked producer example
container/waitable_map.hpp   WaitableMap (kv + ki) and parallel rebuild
```
