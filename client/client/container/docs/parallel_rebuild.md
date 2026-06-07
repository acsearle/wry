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
- Stages 1 and 2: not started.
