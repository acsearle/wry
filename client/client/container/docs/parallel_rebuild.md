# Parallel rebuild

## What it is

`World::step()` produces the next world by rebuilding each persistent map/set
from the previous one plus a batch of per-key modifications collected during the
tick (see `core/docs/transaction.md`). Each rebuild reads two immutable inputs:

- `source`: a persistent map or set backed by an array-mapped trie (AMT,
  `container/array_mapped_trie.hpp`), radix-partitioned by the key's encoded
  code.
- `modifier`: a `ConcurrentMap` (ordered skiplist) of the keys touched this
  tick, each carrying a `ParallelRebuildAction` (WRITE / CLEAR / MERGE / NONE).

and produces a new AMT with every action applied. Both inputs are frozen for the
duration -- the tick's barrier publishes them and nothing mutates them during
rebuild.

The live implementations (`container/waitable_map.hpp`,
`container/persistent_set.hpp`) are single-threaded loops over `modifier` that
`set` / `merge` / `erase` each entry into a path-copied result. This document is
the plan to make them genuinely parallel. (Three stale rebuild variants in
`persistent_map.hpp` were removed; `git log` has the sketch that delegated to a
never-written AMT overload.)

## Precondition for range splitting

Any parallel scheme slices a code range `[lo, hi)` of the AMT against a
contiguous span of the skiplist, so it requires that the **skiplist's comparator
order equal the AMT's radix order** (the `KeyService::encode` numeric order). The
serial loop does not need this; the parallel version does. Assert it per key type
(`Coordinate`, `EntityID`, `Time`).

## The dominant win: share unchanged subtrees

Independent of any cleverness, the biggest saving is structural sharing: a
subtree whose code range contains no modifier entry is returned by pointer,
unchanged. Most of the world does not change per tick, so most subtrees are
shared in O(1). This is a single cursor peek ("is the next modifier key < hi?")
and must be the first thing built. Everything below is about cheaply locating
where the modifier entries are; it is secondary to this short-circuit.

## Stage 0: the serial loop (oracle)

Keep the existing single-threaded rebuild as the correctness oracle. Stages 1 and
2 must produce a bit-identical resulting map, which a test can diff directly.

## Stage 1: parallel over the AMT

Co-recurse over the AMT, forking subtrees into a `Coroutine::Nursery` exactly as
`ArrayMappedTrie::coroutine_parallel_for_each` already does:

```
rebuild(node, [lo, hi)):
    if modifier has no key in [lo, hi):   return node           // share, O(1)
    if node is a leaf:                     apply entries -> new leaf
    else:
        partition the modifier sub-range by the child index (the B bits at
        node->_shift)
        for each child index in (node bitmap) UNION (modifier-touched slots):
            in both          -> fork rebuild(child_i, subrange_i)
            modifier only    -> fork rebuild(empty,   subrange_i)   // inserts
            bitmap only      -> keep child_i unchanged              // empty range
        join; assemble the new node, canonicalizing emptied / collapsed children
```

The "no key in `[lo, hi)`" test and the per-child boundaries use
`lower_bound(code)` (new, below). In Stage 1 each `lower_bound` is a top-down seek
from the head, O(log M); redundant but correct and already parallel. WRITE, CLEAR,
MERGE, insert and erase are all just "apply the actions in this range," so they
fall out uniformly.

## Stage 2: cursor co-descent (optimization)

Stage 1 re-seeks from the head for every subtree. Stage 2 threads a skiplist
cursor down in loose synchronization with the AMT descent, so each level refines
the cursor instead of restarting.

The substrate already exists: `ConcurrentSkiplistSet::FrozenCursor`
(`concurrent_skiplist.hpp`) is a post-freeze, non-atomic, multi-level cursor with
`down()` (drop a level), `right()` (advance at the current level), `key()`, and
`make_cursor()` (enter at head / top level). `down` / `right` are exactly the
operations needed.

Position the cursor entering a subtree `[lo, hi)` at `pred(lo)` at the **highest
level whose `right()` step stays within `[lo, hi)`** -- equivalently, the cursor
whose topmost skip spans the whole range; one level up would overshoot it. To
split among children at increasing boundaries `lo = b0 < b1 < ... < bk = hi`,
sweep the cursor forward with `right()` at the current level, dropping with
`down()` as each child's range narrows, locating each `pred(b_i)` in one monotone
left-to-right pass: O(span at this level + level drops), never re-seeking from the
head. Hand each child its sub-cursor, re-fitting the level to the child's narrower
range.

"Loose" because radix levels shed B bits per step while skiplist levels shed
roughly half the expected span each, and the skiplist is randomized: you `down()`
by range-fit, not by a fixed ratio.

## New primitives

1. `lower_bound(code)` on the skiplist (Set + Map): first key >= code, or end().
   Generalizes `find`, returning its level-0 landing position on a miss.
2. AMT co-recursive `rebuild(node, range, action)` forking into a `Nursery`.
   (Stage 1.)
3. Thin `WaitableMap` / `PersistentSet` / `PersistentMap` wrappers replacing the
   serial bodies, keeping the signatures `World::step()` already calls. (Stage 1.)
4. The level-fit + monotone-sweep cursor threading over `FrozenCursor`. (Stage 2.)

## Validation and determinism

The result is a pure function of `(source, modifier, action)` over disjoint key
ranges with no shared mutable state, so it is fork-order independent and identical
across clients (the multiplayer requirement, see `core/docs/transaction.md`).
Stage 2 changes only traversal cost, not output. Test by running Stage 1 / Stage 2
and the Stage 0 serial loop on the same inputs and asserting equal maps.

## Status

- `lower_bound` (Set + Map) and its test: implemented in
  `concurrent_skiplist.{hpp,cpp}`.
- Stage 1 core: `ArrayMappedTrie::coroutine_parallel_rebuild` (co-recursion,
  structural-sharing short-circuit, per-index fork/assemble, with
  `rebuild_serial` as the base case) implemented in `array_mapped_trie.hpp`,
  plus a differential test vs a `std::map` oracle (`amt_parallel_rebuild` in
  `persistent_map.cpp`) that also checks source immutability.  Builds and passes
  the full suite.  Note: this core takes a *materialized* sorted `mods` vector
  and slices it with `std::lower_bound`; the skiplist `lower_bound` is the Stage
  2 primitive and is not yet on this path.  The test checks content equivalence
  (try_get over the key domain), not byte-identical trie shape -- shape is a
  derived structure (see `core/docs/transaction.md` on determinism).
- Stage 1 wrapper: `coroutine_parallel_rebuild(PersistentMap, ConcurrentMap,
  action_for_key)` in `persistent_map.hpp` materializes a *real* skiplist
  modifier (NONE-filtered) and drives the AMT core, returning a new
  PersistentMap.  Differential test `persistentmap_parallel_rebuild` (real
  `ConcurrentSkiplistMap` vs `std::map` oracle, plus source immutability) passes.
  This is the dense-kv rebuild primitive `World::step()` will use.
- Stage 1 wired into `World::step()`: `WaitableMap::coroutine_parallel_rebuild2`
  (the body `world.cpp` already calls -- no call-site change) now does one serial
  pass that applies the sparse `ki` waiter-index multimap in place and collects
  the dense `kv` value actions, then rebuilds `kv` in parallel via
  `coroutine_parallel_rebuild_from_mods`.  The old serial body is kept as
  `coroutine_parallel_rebuild2_serial` (the Stage-0 oracle).  The time-wheel
  `PersistentSet` rebuild stays serial.  Differential test
  `waitablemap_parallel_rebuild` (parallel kv == serial kv == `std::map` oracle,
  ki path exercised) passes; full suite green.
  - Coverage gap: the suite does not drive an actual `World::step()`, so the live
    integration (the real conflict-resolving `action_for_key` + its side effects)
    is compiled but not runtime-tested.  The differential test covers the changed
    rebuild code at the function level.
- Stage 2 (cursor co-descent): the *narrow* version (just swap the kv modifier
  read for a cursor) was shelved as low-value -- bundling forces materialization
  regardless.  But the **fused** version below revives it for the right reason: by
  rebuilding kv+ki together and reading the modifier via cursor, it parallelizes
  `action_for_key` (i.e. conflict *resolution*) -- the lever worth pulling -- and
  drops materialization.

  **Unified tri-recursion (per WaitableMap).** One modifier-driven co-recursion
  over kv (`AMT<code,T>`), ki (`AMT<code,WaitSet>`), and the modifier skiplist,
  descending in lockstep by AMT prefix frames:
  ```
  rebuild(frame{prefix,shift}, const kv*, const ki*, mod-locator)
    -> (const kv*, const ki*)     // returns input ptr when a subtree is unchanged
  ```
  Per frame, for each of <=32 child slots: extract the kv/ki child (a slot child,
  or the whole node if it is deeper and lands in this slot, or null); ask the
  locator "any mods in the child range?"; if none, share the children; if a leaf,
  `co_await action_for_key` per in-range modifier entry and apply both the value
  combine (kv) and the WaitSet-union combine (ki); else fork and recurse.  Assemble
  with collapse.  `action_for_key`'s side effects (`resolve()`, `next_ready`) now
  run in the parallel leaf phase -- safe because `resolve()` is idempotent and
  `next_ready` is concurrent and order-independent, but the differential test must
  assert the resulting `next_ready` set matches.

  Staging (de-risk by isolating the novel cursor): (1) tri-recursion with the
  modifier sliced by `lower_bound` (no cursor yet, no materialization), diffed
  against the current `coroutine_parallel_rebuild2` oracle (kv', ki', next_ready);
  (2) swap `lower_bound` slicing for the `FrozenCursor` co-descent (bug surface
  isolated to the cursor); (3) wire into `world.cpp`.  The current (materialize +
  fork kv + fork ki) path is kept as the oracle throughout.

  Status: step (1) done -- `coroutine_parallel_rebuild2_unified` in
  `waitable_map.hpp` (helpers `unified_frame` / `unified_leaf` /
  `unified_extract_child` / `unified_assemble`; AMT gained `RADIX_LOG2`).  The
  differential test asserts unified kv'/ki' == the serial/parallel/std oracles
  across all action kinds with a non-empty source ki; builds, full suite green.
  Caveat: `next_ready` equivalence is not *directly* asserted (the unit test's
  `action_for_key` is side-effect-free).  It follows from `action_for_key` being
  invoked exactly once per modifier key (which the kv'/ki' match implies) plus
  set semantics, and gets exercised for real at the world level (step 3).
  Steps (2) FrozenCursor swap and (3) world wiring: not started.

- Waiter index (`ki`) nesting -- the real blocker on full-rebuild parallelism:
  - The old flat `PersistentSet<pair<Key, EntityID>>` made a key's waitset a
    prefix *subtree*, so per-key replace/erase were subtree ops that don't fit the
    per-key parallel rebuild (and the pair code's A/B split at bit 64 isn't
    SYMBOL_WIDTH-aligned, so it straddles).
  - Now nested: `ki : PersistentMap<Key, WaitSet>`, `WaitSet = PersistentSet<
    EntityID>` (sparse: only keys with waiters).  Per-key ops become single-key:
    WRITE=replace, CLEAR=erase, MERGE=read-modify-write upsert (union).  Wake-on-
    write is now `source.ki.try_get(key)` + `for_each` (world.cpp).
    `as_multimap_*` and `for_each_if_first` removed.
  - Now parallel: `ki` actions feed `coroutine_parallel_rebuild_from_mods` with a
    `WaitSet`-union combine (`WaitSetMergeCombine`; the combine's `old` arg is the
    RMW read -- no new AMT primitive), and `kv` + `ki` are forked concurrently in
    `coroutine_parallel_rebuild2`.  `from_mods` was generalized to take a caller-
    supplied combine (`ParallelRebuildValueCombine` for kv).  The serial
    `apply_ki_action` path stays as the oracle.  Differential test extended to
    assert `parallel.ki == serial.ki == oracle` (WaitSet contents, all action
    kinds, non-empty source ki); builds; full suite green.  The whole
    `WaitableMap` rebuild is now parallel.
  - Save/load of the nested `ki` is **stubbed with a TODO** (round-trips as empty;
    `io/` has no save round-trip test and the rep may still change).
