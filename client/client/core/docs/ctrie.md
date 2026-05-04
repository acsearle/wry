# Ctrie + weak interning

## Purpose

`Ctrie` is the global string-intern dictionary.  Keys are `HeapString*`; the
trie holds them **weakly** so the collector can reclaim unreferenced strings.
Mutators looking up a key get either an existing live `HeapString*` (with a
proof that this cycle's collector won't reclaim it) or `nullptr` if it's
mid-collection, in which case they install a fresh one.

Algorithm: Prokopec, Bagwell, Bronson, Odersky, *Concurrent Tries with
Efficient Non-Blocking Snapshots* (PPoPP 2012) — the LNode/SNode/TNode
structure here matches that paper.  An earlier paper, Prokopec, Bagwell,
Odersky, *Cache-Aware Lock-Free Concurrent Hash Tries* (2011), covers the
basic tomb-and-resurrect machinery without LNodes.  We do not implement the
GCAS / RDCSS snapshot machinery — we have no use for snapshots.

## Structure

```
Ctrie::root → INode
INode.main  : Atomic<MainNode*>            (release/acquire on every read/CAS)
MainNode    := CNode | TNode | LNode
Branch      := INode | SNode

CNode  { bmp, array[] : Branch[] }         immutable after publish
SNode  { sn    : HeapString*               (immutable)
       , state : Atomic<WeakState>         (mutates per protocol below) }
LNode  { sn    : SNode*                    (immutable)
       , next  : Atomic<LNode*>            (collector-only writer) }
TNode  { sn    : SNode* }                  (immutable; compression tombstone)
```

`SNode` is the per-key leaf wrapper.  Singletons live as a `Branch` directly
inside a CNode array; collisions live in an `LNode` chain hanging off an
INode at the deepest level.  `HeapString` is the value being interned; it is
*not* a Branch.

Branching factor 2^W = 64 (W = 6).  At `lev > 60` (hash bits exhausted) the
two-key CNode constructor falls through to an LNode chain instead of a
deeper CNode.

## Variations from the paper

- `Ctrie::root` is set once in the constructor and never replaced.  The
  paper's null-root state is intentionally not modeled.
- The weak-slot protocol (`SNode::state`) has no analog in the paper.  It is
  the reason `SNode` exists in this codebase as a distinct type rather than
  letting the value type double as the leaf.
- `LNode::next` is atomic (paper's is plain) so that the collector can splice
  GONE entries out of a chain without rebuilding it.
- We don't implement snapshots, so no `Gen`, no `GCAS`, no `RDCSS`.

## Weak slot protocol

State and transitions on `SNode::state`:

```
        ┌── M ──┐
        │       ▼
   READY ◄──── WAS_LOADED       (M = mutator CAS to WAS_LOADED)
     │                          (C = collector, two flavors)
     ▼ C
   GONE   (terminal; sn and SNode queued for epoch-deferred free)
```

Mutator `lock(SNode*) → HeapString*`:
```
loop {
    s = state.load_relaxed();
    if (s == GONE) return nullptr;
    if (state.compare_exchange_weak_relaxed_relaxed(s, WAS_LOADED))
        return sn;
}
```

**The mutator MUST perform the RMW even when state is already WAS_LOADED.**
Its observation has to appear in the slot's modification order so the
collector's subsequent `(READY, GONE)` CAS reliably fails.  A read-acquire
shortcut is *unsound*.

Collector per-SNode in `_sweep_weak`:
```
s = READY;
if (state.compare_exchange_strong_relaxed_relaxed(s, GONE)) {
    splice_or_unparent(this);          // remove from LNode chain or CNode
    queue_epoch_deferred_free(sn);
    queue_epoch_deferred_free(this);
} else if (s == WAS_LOADED) {
    state.compare_exchange_strong_relaxed_relaxed(s, READY);   // best-effort
    sn->_gray.fetch_or_relaxed(_gray_for_allocation);
    sn->_black |= _black_for_allocation;
}
// else GONE; nothing to do
```

Resurrection ORs every currently-active gray *and* black bit, making `sn`
"as-if traced" for all live cycles.  Sound because HeapStrings have no
children to scan.

## Phase ordering

Per-bit phase chain (extending [tricolor.md](tricolor.md)):
```
UNUSED → GRAY_PUBLISHED → BLACK_PUBLISHED → WEAK_DECISION
       → SWEEPING → WHITE_PUBLISHED → CLEARING → UNUSED
```

`WEAK_DECISION` is a one-scan phase.  Any bit reaching this phase triggers a
collector pass over all known SNodes; resurrection writes apply to every
currently-active bit, not just the one driving the phase.

## Memory ordering

Three independent layers — do not conflate:

| Concern | Mechanism |
|---|---|
| `SNode::sn` valid for deref | Release-acquire on parent `INode::main` |
| `SNode::state` linearization | All ops relaxed; per-location coherence |
| `LNode::next` traversal vs. splice | Release on collector splice; acquire on mutator load |
| `sn` deref under concurrent free | Epoch-deferred free, embargo ≥ 3 epochs |
| Resurrected color visible next cycle | Existing color-publish via epoch system |

The relaxed-everywhere result for `state` is counterintuitive but correct:
`sn` is published once (by the SNode constructor + parent INode.main release)
and never republished, so per-location coherence is all the protocol needs
out of the state atomic.

## Invariants

1. CNode and TNode are fully immutable after publication.
2. SNode is constructed thread-private; after publish, only `state` mutates,
   and only as described above.  `sn` is fixed for life.
3. LNode is constructed thread-private; after publish, only `next` mutates,
   and only by the collector's splice-out of a GONE successor.  `sn` is
   fixed for life.
4. `state` is monotonic in `READY < WAS_LOADED < GONE` with `GONE` terminal.
   Resurrect maps `WAS_LOADED → READY` (not a regression — that's the
   collector acknowledging mutator activity and resetting the bookkeeping
   for the next cycle).
5. `Ctrie::root` is non-null and constant for the lifetime of the Ctrie.
6. `HeapString` does not appear directly as a Branch.  Every leaf reference
   to a HeapString goes through an SNode.

## Phase status

- **Phase 0** (current): SNode/LNode/TNode types in place; trie operations
  work end-to-end as a non-interning identity dict.  `SNode::state` not yet
  added.  CNode scan strong-traces all entries (belt-and-braces — a true
  weak edge would let the GC reclaim things the trie still references).
  `HeapString::make` does **not** route through the trie.
- **Phase 1**: introduce `WeakState`/`SNode::state`, `LNode::next` becomes
  atomic, splice-out machinery.
- **Phase 2**: collector `_sweep_weak` virtual + `WEAK_DECISION` phase +
  epoch-deferred free.
- **Phase 3**: route `HeapString::make` through a constinit-initialised
  global Ctrie singleton.
- **Phase 4**: resurrect the live mutator-side lock paths in
  `LNode::find_or_copy_emplace` and `SNode::_ctrie_bn_find_or_emplace`.

## See also

- [tricolor.md](tricolor.md) — collector phases, k-collections, embargo.
- [garbage_collected.cpp](../garbage_collected.cpp) — collector loop.
- Papers: Prokopec et al., 2011 (basic Ctrie); Prokopec et al., 2012
  (LNode/SNode/TNode; we adopt this structure modulo snapshots).
