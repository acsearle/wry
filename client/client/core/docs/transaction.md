# Transactional world step

## Purpose

`Transaction` is the conflict-resolution engine for a single simulation tick.
It is *not* a general software transactional memory with optimistic read-set
validation. It is narrower and cheaper: every ready entity proposes a set of
writes, all conflicts over a given location are decided by a fixed priority
order, and exactly one writer per location wins. The whole tick is a pure
function of the previous `World`, so the simulation stays deterministic
(see `game/docs` on determinism).

Read this alongside `World::step()` and `World::_advance()` in
`game/world.cpp`. `transaction.hpp`
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
terms, it flows through the same ordered log). How the code does this is
"Deterministic EntityID assignment" below.

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
World::step(commands):                 (world.cpp)
  if any JOIN/LEAVE: stage a copy with the events applied (_with_events)
  _advance(commands):
  -- Phase 1: PROPOSE (parallel, one nursery) ----------------------
  fork notify_and_accumulate(_ready):
      a tree walk over the frozen ready set; at each ready EntityID
      entity->notify(context)
          builds a Transaction, calls verbs (write_*/wait_on_*),
          each verb appends a Node onto a per-key lock-free list,
          and returns how many new EntityIDs it requests
      the walk writes per-node rank prefixes, returns the tick total
  fork: copy the time wheel's next_time bucket into next_ready
  -- BARRIER (nursery.join) ----------------------------------------
  -- Phase 2: RESOLVE + REBUILD (per touched key, parallel) --------
  freeze each TransactionContext verb map
  fork one rebuild per WaitableMap, plus the time wheel:
      at each touched key, walk its conflict list, resolve() each node
      the single committed writer's value goes into the new kv map
      the key's waiter index (ki) is rewritten
      woken waiters are inserted into next_ready
      an installed Entity receives its EntityID ticket, then is sealed
  -- BARRIER (nursery.join) ----------------------------------------
  resolve the money scalar; make any terrain chunks the step needs
  return new World{ next_time, cursor + tick total, freeze(next_ready),
                    new maps..., new wheel }, sealed
```

Phase 1 only *mutates* the proposal graph (and the ready set's rank fields;
see "Late-written fields on published objects"). Phase 2 only *reads* the
graph (plus the per-transaction `_state` cache). Nothing in Phase 2 races with
Phase 1, and that separation is the entire basis for the relaxed atomics
below.

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
`_verb_located_for_coordinate`, `_verb_entity_for_entity_id`, `_verb_money`
(one key), and `_wait_on_time`. The map is an
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

Priority is `transaction_priority(entity_id, world_time)`, which is
`splitmix64(splitmix64(entity_id) ^ world_time)` (changed 2026-09-30; it was
the old byte hash chained over the two fields). **Lower number means higher
priority.** Because it is re-salted with the time every tick, an entity that
loses this tick is likely to win a future one, so no entity starves
indefinitely.

### Why priorities are unique

Uniqueness is not luck; it is guaranteed by construction. The priority is

    priority(id, t) = splitmix64( splitmix64(id) ^ t )

where `splitmix64` is the pinned mixer in `core/hash.hpp` (since 2026-09-30;
Numerical Recipes' `ranhash` before), which is *invertible* on 64 bits -- a reversible / bijective integer hash, the
standard term for a function that maps a fixed-width integer domain onto itself
with no collisions. Composing bijections yields a bijection: `id -> hash(id)`,
xor with the fixed `t`, and the outer `splitmix64` are each bijective, so for any
fixed time `priority(., t)` is a permutation of the EntityID space. Distinct
EntityIDs therefore always receive distinct priorities within a tick.

The chain that must hold:

1. EntityIDs are unique and never reused (see "Deterministic EntityID
   assignment").
2. `splitmix64(uint64_t)` is injective, hence bijective on 64 bits.
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
first `co_await nursery.join()` in `World::_advance()`, which joins
`notify_and_accumulate` and every frame it forked (the coroutine nursery's
completion path carries the release/acquire edge; this document does not
re-derive it). Every
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

An entity runs only when its EntityID is in the ready set of the World being
stepped (`World::_ready`). It gets there for the next tick in one of two ways:
it waited on a key and a committed write to that key happened, or it waited on
a time and that time came. Both are expressed as non-exclusive verb nodes; the
`WAIT_*` bits say which outcome of the waiter's *own* transaction the wait is
conditional on:

- `WAIT_ON_COMMIT` -- the wait takes effect only if my transaction commits;
- `WAIT_ON_ABORT` -- only if it aborts (`on_abort_retry()` is the idiom);
- `WAIT_ALWAYS` -- either way.

A wait whose condition does not hold is simply dropped. That is a sharp edge:
an entity whose only waits are `WAIT_ON_COMMIT` and whose transaction aborts is
never woken again unless something else names it. `Player::notify`
(`player.cpp`) uses `WAIT_ALWAYS` for exactly this reason, and
`Machine::notify` pairs its waits with `on_abort_retry()`.

The schedule lives in three places:

```
World::_ready            FrozenSkiplistMap<EntityID, ReadyValue>
                         who runs at _time (ordered by EntityID)
World::_waiting_on_time  PersistentSet<(Time, EntityID)>, the time wheel
                         who runs at a time > _time
WaitableMap::ki          PersistentMap<Key, WaitSet>, per map
                         who runs at the first commit to this key
```

`_advance` asserts (Debug) that nothing in the wheel is due at or before
`_time`; anything due now is in `_ready` instead. The save and the World hash
fold `_ready` back in as the wheel's `(_time, id)` bucket
(`World::TimeWheelFold`, `world.hpp`), so the split is invisible to both.
`World::hack_repair_invariant()` establishes it for a hand-built or loaded
World by moving the `_time` bucket out of the wheel into `_ready`.

### Building `next_ready`

`next_ready` is a `ConcurrentSkiplistMap<EntityID, ReadyValue>` local to
`World::_advance()`. Every producer below inserts with `try_emplace`, which is
a no-op for an id already present, so the final contents are a set union and
do not depend on which thread inserted what, or in which order. It is frozen
into the new World's `_ready` only after the Phase 2 join; nothing reads it
before then.

1. **The wheel's next bucket.** At the top of `_advance`,
   `partition_first(_waiting_on_time, next_time)` splits the wheel into the
   `next_time` bucket and the rest. The bucket is copied into `next_ready` by a
   `coroutine_parallel_for_each` forked into the *same* nursery as
   `notify_and_accumulate`, so it overlaps Phase 1 (notify never touches
   `next_ready`). The rest is the base the new wheel is rebuilt from.
2. **Time waits proposed this tick.** `wait_on_time(t)` (and its sugar
   `on_commit_sleep_for`, `on_abort_retry`) appends a node to
   `TransactionContext::_wait_on_time` keyed by `t` with the caller's id as
   payload; `write_entity_id_for_time(t, id)` is the same with another
   entity's id (a Spawner schedules its newborn Machine this way). Both assert
   `t > now`. In Phase 2, `action_for_waiting_on_time` (`world.cpp`) tests
   `resolve() & _operation` on each node (see "A useful bit trick"): if it
   fires and `t == next_time` the id goes straight into `next_ready`; if
   `t > next_time` it is merged into the new wheel. The wheel's rebuild
   (`coroutine_parallel_rebuild` over a `PersistentSet`, `persistent_set.hpp`)
   is a serial loop, but it runs as one fork concurrent with the four map
   rebuilds.
3. **Key waits.** `wait_on_value_for_coordinate`,
   `wait_on_entity_id_for_coordinate`, `wait_on_located_for_coordinate` and
   `wait_on_entity_for_entity_id` append a waiter node to the *same* per-key
   list the writers use. The four `action_for_*` lambdas in `_advance` (one per
   `WaitableMap`, identical in shape) walk a touched key's list and classify
   each node: the one committed `WRITE_ON_COMMIT` node is the writer; any
   other node with `resolve() & _operation` nonzero is a fired waiter. Then:

   - **The key was written.** The kv map takes the writer's value (a null
     `Term` erases the key in the term map; absence is the one representation
     of empty). Everyone in this World's `ki` set for the key is woken into
     `next_ready`, and so is every fired waiter from this tick. The key's
     `ki` entry is replaced by `{writer}` if the writer's node also carries
     `WAIT_ON_COMMIT` (it waits against the value it just wrote), else erased.
   - **No write, but fired waiters.** They are not woken; they are merged
     (set union) into the key's `ki` entry and wait for a later commit.
   - **Neither.** The key's kv and ki are unchanged.

   A `ki` entry is only ever added to, replaced, or erased as a whole
   (`WaitSetMergeCombine`, `waitable_map.hpp`), and only a committed write
   wakes it. So an entity that was woken by some other route stays in stale
   `ki` sets and later gets a spurious wakeup; notify must tolerate that (for
   example the `PHASE_MINING` early return in `Machine::notify`).

The rebuilds are `coroutine_parallel_rebuild2_unified` (`waitable_map.hpp`):
one modifier-driven co-recursion over kv, ki and the frozen verb map, forking
per AMT child, with the `action_for_*` lambda (resolution, `next_ready`
inserts, the ticket write below) evaluated lazily at the leaves. Untouched
subtrees are shared with the previous World. See
`container/docs/parallel_rebuild.md`.

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
- **The time wheel's rebuild is serial.** The four `WaitableMap` rebuilds are
  parallel (`coroutine_parallel_rebuild2_unified`), but the wheel's
  `coroutine_parallel_rebuild` (`persistent_set.hpp`) is a single loop over the
  frozen `_wait_on_time` map; it overlaps the others as one fork.
- **A commit-only wait dies with its transaction.** See "Waiting and
  scheduling": an aborted transaction whose waits are all `WAIT_ON_COMMIT`
  leaves its entity unscheduled. Every producer has to pair its waits with an
  abort path.
- **Resolution recurses on the C++ stack.** Conflict chains are statistically
  short, but a pathological (or adversarial) chain could grow the stack
  unboundedly. No depth bound or explicit work-stack today.
- Open design questions are noted at the bottom of `world.cpp` (multiple
  independent transactions per entity; coupling the waitset more tightly to
  writes; densifying `next_ready`).

## Deterministic EntityID assignment

New EntityIDs must be assigned by a rule identical on every client (not merely
unique), because `priority = hash(EntityID)` drives conflict resolution and any
divergence desyncs the sim (see "How the multiplayer plan maps onto Calvin").

The rules (decided 2026-08-16):

- Every id comes from one cursor, `World::_entity_id_source` (starts at 1; 0
  means "no EntityID"). The cursor only grows.
- Ids are permanent: never reused, and never compacted at load. A remap would
  change priorities, hence conflict outcomes, so a loaded save would fork from
  its own history; it would also break peer replay and ids embedded in Terms.
  Holes (ids handed out and never used) are accepted; the 64-bit ceiling is
  designed around, not budgeted per session.

### Outside a step: `generate_entity_id`

Code that builds a World it alone holds -- worldgen (`game/world_state.cpp`),
the JOIN event in `World::_with_events()`, test fixtures -- calls
`World::generate_entity_id()`, a plain post-increment of the cursor. Order is
deterministic because that code is sequential (or, for JOIN, follows the
order of the server's event stream).

### Inside a step: requests, rank, tickets

During a step the World is immutable and notifies run in parallel, so nobody
may touch the cursor. Instead an entity *requests* ids and is later handed a
*ticket*:

1. **Request.** `Entity::notify(TransactionContext*)` returns an `int64_t`:
   the number of new ids it wants this tick. Today only `Spawner::notify`
   (`game/spawner.cpp`) returns nonzero, and always 1.
2. **Rank.** The ready set is ordered by EntityID
   (`DefaultKeyService<EntityID>` encodes the raw id), *not* by priority. An
   entity's block is `[cursor + prefix, cursor + prefix + requested)`, where
   `prefix` is the sum of the requests of every ready entity with a smaller
   EntityID. The blocks of one tick tile `[cursor, cursor + total)`, so they
   are disjoint; the next World's cursor is `_entity_id_source +
   entity_id_requests` (the tick total), whether or not the requesters commit.
3. **Delivery.** In `action_for_entity_for_entity_id`, once the committed
   writer of key `k` is known, `try_lookup_cumulant(_ready, k, cumulant)` is
   consulted; if it returns true the installed Entity gets
   `_free_entity_id = _entity_id_source + cumulant`, and is then `seal()`ed.
   The ticket is the block's first id; the holder knows its own count.
   Normally the writer of `k` is `k`'s own successor clone, so a requester
   that commits a write to itself receives its ticket in the same tick.
4. **Spend.** A Spawner with a ticket names its new Machine with it, installs
   a successor clone with `_free_entity_id = 0`, and requests 1 again; the
   same rebuild deals the successor a fresh ticket. A Spawner without one
   installs a clone and requests 1, spawning nothing that tick. So a ticket
   dealt at the end of tick T is spent at T+1 or later.

`try_lookup_cumulant` returns false for an id not in the ready set (an entity
installed by someone else's transaction, e.g. the Machine a Spawner just made)
and for a ready entity that requested 0 (every Machine successor). In both
cases the installed Entity keeps whatever `_free_entity_id` its builder gave
it; that is what stops a held ticket from being clobbered.

Ids are burned, never reissued, when a requester's transaction aborts, or
when it requests but does not write its own key: the cursor advanced anyway.
Each spawn-capable entity also holds one reserved-but-unspent id at all times,
burned when it dies. Both are accepted.

Pre-loaded tickets must be minted from the cursor, never written as a literal,
or they collide with the first block dealt (which starts at the cursor).
Worldgen's Spawner does this (`p->_free_entity_id =
world->generate_entity_id()` in `world_state.cpp`); a JOIN's Player gets an
explicit `EntityID{}`; the Spawners built by the fixtures in
`io/save_format.cpp` and `game/world_dump.cpp` start ticketless and request on
their first notify.

### Computing the prefix without a build phase

The prefix is computed by the notify traversal itself, so there is no
post-barrier "build the index" pass and no assign sub-phase.

`notify_and_accumulate` (`world.cpp`) instantiates `rank_frame` over the
frozen ready set. A frozen skiplist is the left-child/right-sibling encoding
of a tree: each node is discovered exactly once, at its own top level, by the
frame whose interval `[self, bound)` contains it (`Node::for_each_child`). One
coroutine frame per node (and one for the head):

```
rank_frame(self, bound):
  for each child of self in [self, bound): fork rank_frame(child, ...)
                                           into results[child height]
  if not head: results[0] = notify(self); self.requested = results[0]
  join
  prefix-sum results[0 .. height]
  for each child: child.n = results[child height - 1]
  return results[height]                      // this subtree's total
```

A height-h child covers a later key interval than every lower child of the
same frame, so `child.n` is the sum of the requests that precede the child
*within its parent's frame*. The head frame's return is the tick total.

`try_lookup_cumulant(ready, id, victim)` runs an ordinary `find` with a
visitor that adds the `n` of every node it *enters*. A search enters each node
on its path from that node's left-child/right-sibling parent, at the node's
top level, so the per-frame prefixes telescope to the absolute exclusive
prefix; the head is never entered and needs no `n`.

Determinism: ready-set membership is World state; the requests are notify's
output, a function of that state; the prefix is integer addition over
EntityID order, exact and associative. The per-node `n` values depend on the
skiplist's node heights, which this argument does not need clients to agree
on; only the telescoped sum along a search path is observable, and that is the
topology-independent rank prefix. The `ready_rank` test after `_advance` in
`world.cpp` is the oracle: random ready maps with synthetic weights (some
zero), `rank_frame` run exactly as the step runs it, every lookup checked
against the brute-force exclusive prefix, and absent ids and zero requesters
required to return false.

### Late-written fields on published objects

The scheme writes two `mutable` fields after the object that holds them is,
or is about to be, part of an immutable World. Both rest on a single-writer
plus barrier argument rather than on atomics.

- **`ReadyValue::n` and `ReadyValue::requested`** (`world.hpp`). These live in
  the nodes of `_ready` of the World being stepped, which is sealed and may
  be shared. They are plain `int64_t`, created as `-1` by `next_ready`'s
  `try_emplace` and written during the *next* `_advance`: `requested` by the
  node's own frame, `n` by its parent frame after that frame's
  `nursery.join()`. Each field has exactly one writer per `_advance`. They are
  read only by `try_lookup_cumulant` in Phase 2, after the Phase 1 join, which
  orders every write before every read; the lookup asserts both are `>= 0`.
  They are neither saved nor hashed (`_ready` is `transient` in
  `World::_fields`, and `TimeWheelFold` hashes only the ids), so writing them
  does not disturb the World's seal.

  Unenforced contract: at most one `_advance` runs over a given `_ready` at a
  time. `_with_events` copies the World, and the staged copy shares the base
  World's frozen ready nodes, so stepping a World and its staged copy (or one
  World twice) concurrently would be a data race on these fields. The values
  written would be identical, but that is still undefined behaviour in C++.
  No caller found doing so; not verified exhaustively.
- **`Entity::_free_entity_id`** (`entity.hpp`). Installed Entities are const.
  The successor a transaction proposes is built by its proposer in Phase 1
  (`Machine::notify` keeps building its clone after handing it to the
  transaction), is reachable only through its node's `_desired` until the
  rebuild installs it, and receives its ticket in
  `action_for_entity_for_entity_id`, the unique committer leaf for its key,
  immediately before `seal()`. The field *is* hashed state, so the ticket
  must land before the seal; the new World that publishes the Entity is
  constructed after the Phase 2 join. Losing proposals' clones are never
  ticketed or sealed.

## File map

```
core/transaction.hpp   Transaction, Node, TransactionContext, verb declarations,
                       ExternallyDiscriminatedVariant, State/Operation enums
core/transaction.cpp   transaction_verb_generic, resolve/abort/commit, verbs
game/world.cpp         World::step() / _advance() -- the driver, the barriers,
                       the rebuild; notify_and_accumulate, rank_frame,
                       try_lookup_cumulant, the ready_rank oracle test
game/entity.hpp        Entity::notify(TransactionContext*) -- producer hook,
                       Entity::_free_entity_id
game/world.hpp         ReadyValue, World::_ready / _waiting_on_time / cursor
game/machine.cpp       Machine::notify -- a worked producer example
game/spawner.cpp       Spawner::notify -- the one EntityID requester
container/waitable_map.hpp   WaitableMap (kv + ki), WaitSetMergeCombine,
                       coroutine_parallel_rebuild2_unified
```
