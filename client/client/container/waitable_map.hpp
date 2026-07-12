//
//  waitable_map.hpp
//  client
//
//  Created by Antony Searle on 8/8/2025.
//

#ifndef waitable_map_hpp
#define waitable_map_hpp

#include "persistent_map.hpp"
#include "persistent_set.hpp"
#include "entity.hpp"

namespace wry {
    
    // A key's set of waiting entities.  Nested as the *value* of the ki map
    // (rather than flattened into pair<Key, EntityID> keys) so that per-key
    // waitset operations are single-key, which is what the parallel rebuild
    // eats; see container/docs/parallel_rebuild.md.  Sparse: only keys that
    // actually have waiters get an entry, so the dense kv map stays lean.
    using WaitSet = PersistentSet<EntityID, DefaultKeyService<EntityID>, ScanDiscipline>;

    template<typename Key, typename T>
    struct WaitableMap {

        PersistentMap<Key, T, DefaultKeyService<Key>, ScanDiscipline> kv;
        PersistentMap<Key, WaitSet, DefaultKeyService<Key>, ScanDiscipline> ki;

        bool try_get(Key key, T& victim) const {
            return kv.try_get(key, victim);
        }

        void set(Key key, T desired) {
            kv.set(key, std::move(desired));
        }

    };

    template<typename Key, typename T>
    void garbage_collected_scan(const WaitableMap<Key, T>& x) {
        garbage_collected_scan(x.kv);
        garbage_collected_scan(x.ki);
    }

    // ---- Rectangular region query -----------------------------------------
    //
    // Visit every kv entry whose Coordinate key lies in the CLOSED rectangle
    // [lo.x, hi.x] x [lo.y, hi.y], by recursive descent with per-block
    // pruning; entries arrive in Morton (code) order.
    //
    // An AMT node covers the Morton-code block [_prefix, _prefix +
    // 2^(_shift + SYMBOL_WIDTH)): its free bits are the code's low bits, and
    // de-interleaving makes them the low bits of each axis.  Decoding the two
    // corner codes (free bits all zero / all one) therefore yields, per axis,
    // a contiguous two's-complement interval [c0, c1] whenever that axis's
    // sign bit is among the fixed bits; a free sign bit means the whole axis
    // is free, and shows up as wrapped corners (c0 > c1), read as "spans
    // everything".  Blocks disjoint from the query prune; blocks contained in
    // the query switch to a test-free for_each.

    template<typename N, typename F>
    void _visit_in_region_descend(const N* node,
                                  Coordinate lo, Coordinate hi,
                                  F&& action) {
        if (!node)
            return;
        using H = DefaultKeyService<Coordinate>;
        Coordinate c0 = H{}.decode(node->_prefix);
        Coordinate c1 = H{}.decode(node->_prefix | ~node->get_prefix_mask());
        bool x_wraps = c0.x > c1.x;
        bool y_wraps = c0.y > c1.y;
        if (!x_wraps && ((c1.x < lo.x) || (hi.x < c0.x)))
            return;
        if (!y_wraps && ((c1.y < lo.y) || (hi.y < c0.y)))
            return;
        bool contained = !x_wraps && !y_wraps
            && (lo.x <= c0.x) && (c1.x <= hi.x)
            && (lo.y <= c0.y) && (c1.y <= hi.y);
        if (contained) {
            node->for_each([&action](uint64_t code, auto value) {
                action(H{}.decode(code), value);
            });
            return;
        }
        if (node->has_children()) {
            int n = std::popcount(node->_bitmap);
            for (int i = 0; i != n; ++i)
                _visit_in_region_descend(node->_children[i], lo, hi, action);
        } else {
            // partially-covered leaf: filter per entry
            node->for_each([&action, lo, hi](uint64_t code, auto value) {
                Coordinate xy = H{}.decode(code);
                if ((xy.x < lo.x) || (hi.x < xy.x) ||
                    (xy.y < lo.y) || (hi.y < xy.y))
                    return;
                action(xy, value);
            });
        }
    }

    template<typename T, typename F>
    void visit_in_region(const WaitableMap<Coordinate, T>& map,
                         Coordinate lo, Coordinate hi,
                         F&& action) {
        assert((lo.x <= hi.x) && (lo.y <= hi.y));
        _visit_in_region_descend(map.kv._inner ? &*map.kv._inner : nullptr,
                                 lo, hi, action);
    }

    // Combine for the ki waiter index: WRITE replaces a key's waitset, CLEAR
    // erases it, MERGE is the read-modify-write union (the combine's `old` arg is
    // the RMW read), NONE keeps it.
    struct WaitSetMergeCombine {
        std::optional<WaitSet> operator()(const WaitSet* old,
                                          const ParallelRebuildAction<std::vector<EntityID>>& a) const {
            using A = ParallelRebuildAction<std::vector<EntityID>>;
            switch (a.tag) {
                case A::WRITE_VALUE: {
                    WaitSet ws;
                    for (EntityID e : a.value)
                        ws.set(e);
                    return ws;
                }
                case A::CLEAR_VALUE:
                    return std::nullopt;
                case A::MERGE_VALUE: {
                    WaitSet ws = old ? *old : WaitSet{};
                    for (EntityID e : a.value)
                        ws.set(e);
                    return ws;
                }
                case A::NONE:
                    break;
            }
            return old ? std::optional<WaitSet>(*old) : std::nullopt;
        }
    };


    // ---- Unified frozen-cursor rebuild (Stage 2 scaffold) -----------------
    //
    // One modifier-driven co-recursion over kv, ki and the modifier, descending
    // by AMT prefix frames.  Scaffold: the modifier is sliced with `lower_bound`
    // (the FrozenCursor co-descent is a later, isolated swap).  `action_for_key`
    // is evaluated lazily at the leaves -- so resolution + its `next_ready` side
    // effects now run in the parallel phase.  The (materialize + fork kv + fork
    // ki) `coroutine_parallel_rebuild2` is the differential-test oracle.
    //
    // Precondition: the modifier's comparator agrees with DefaultKeyService<Key>
    // (so its order is the AMT code order); see container/docs/parallel_rebuild.md.

    template<typename Key, typename T>
    using UnifiedKvAMT = ArrayMappedTrie<typename DefaultKeyService<Key>::code_type, T, ScanDiscipline>;
    template<typename Key>
    using UnifiedKiAMT = ArrayMappedTrie<typename DefaultKeyService<Key>::code_type, WaitSet, ScanDiscipline>;

    // The subtree of `node` for child `slot` at this frame: a slot child if the
    // node sits exactly at the frame, the whole node if it is deeper and lands in
    // this slot, otherwise null.
    template<typename N>
    const N* unified_extract_child(const N* node, int slot, int shift) {
        if (!node)
            return nullptr;
        if (node->_shift == shift) {
            if (!((node->_bitmap >> slot) & 1))
                return nullptr;
            return node->_children[node->get_compressed_index_for_index(slot)];
        }
        int c = (int)((node->_prefix >> shift) & N::INDEX_MASK);
        return (c == slot) ? node : nullptr;
    }

    // Build a node from its (disjoint, index-ordered) child subtrees, collapsing
    // to honour the ">= 2 children" invariant.
    template<typename N, typename W>
    const N* unified_assemble(W prefix, int shift, const N* const* out, int n_slots) {
        int n = 0;
        const N* only = nullptr;
        for (int c = 0; c < n_slots; ++c)
            if (out[c]) { ++n; only = out[c]; }
        if (n == 0)
            return nullptr;
        if (n == 1)
            return only;
        N* node = N::make(prefix, shift, n, 0, 0);
        for (int c = 0; c < n_slots; ++c)
            if (out[c])
                node->insert_child(out[c]);
        return node;
    }

    // Apply the modifier entries of one 32-key leaf range [lo, lo+32) to the kv
    // and ki leaves, walking the frozen skiplist at level 0 from `cursor`.
    template<typename Key, typename T, typename Cur, typename F>
    Coroutine::Future<std::pair<const UnifiedKvAMT<Key, T>*, const UnifiedKiAMT<Key>*>>
    unified_leaf(typename DefaultKeyService<Key>::code_type lo,
                 const UnifiedKvAMT<Key, T>* kv,
                 const UnifiedKiAMT<Key>* ki,
                 Cur cursor,
                 const F& action_for_key) {
        using KvAMT = UnifiedKvAMT<Key, T>;
        using KiAMT = UnifiedKiAMT<Key>;
        using H = DefaultKeyService<Key>;
        using Code = typename H::code_type;
        using ValA = ParallelRebuildAction<T>;
        using KiA = ParallelRebuildAction<std::vector<EntityID>>;
        auto codeof = [](const Cur& x) -> __uint128_t {
            auto* k = x.key();
            return k ? (__uint128_t)H{}.encode(k->first) : ((__uint128_t)1 << 64);
        };
        __uint128_t lo128 = lo, hi128 = (__uint128_t)lo + 32;
        Cur c = cursor;
        while (!c.bottom())
            c = c.down();
        while (codeof(c) < lo128)
            c = c.right();
        const KvAMT* kv2 = kv;
        const KiAMT* ki2 = ki;
        while (codeof(c) < hi128) {
            Code code = (Code)codeof(c);
            std::pair<ValA, KiA> p = co_await action_for_key(*c.key());
            c = c.right();
            if (p.first.tag != ValA::NONE) {
                T old{};
                bool has = kv2 && kv2->try_get(code, old);
                std::optional<T> nu = ParallelRebuildValueCombine<T>{}(has ? &old : nullptr, p.first);
                if (nu)
                    kv2 = KvAMT::insert(kv2, code, std::move(*nu));
                else if (has) {
                    T victim{};
                    kv2 = kv2->clone_and_erase_key(code, victim).first;
                }
            }
            if (p.second.tag != KiA::NONE) {
                WaitSet old{};
                bool has = ki2 && ki2->try_get(code, old);
                std::optional<WaitSet> nu = WaitSetMergeCombine{}(has ? &old : nullptr, p.second);
                if (nu)
                    ki2 = KiAMT::insert(ki2, code, std::move(*nu));
                else if (has) {
                    WaitSet victim{};
                    ki2 = ki2->clone_and_erase_key(code, victim).first;
                }
            }
        }
        co_return std::pair<const KvAMT*, const KiAMT*>{kv2, ki2};
    }

    // Co-recurse over kv, ki and the modifier for this frame.  The frozen-cursor
    // partitioner hands each non-empty child a covering cursor (descend-just-
    // enough); empty children share.  No re-seek, no materialization.
    template<typename Key, typename T, typename Cur, typename F>
    Coroutine::Future<std::pair<const UnifiedKvAMT<Key, T>*, const UnifiedKiAMT<Key>*>>
    unified_frame(typename DefaultKeyService<Key>::code_type prefix, int shift,
                  const UnifiedKvAMT<Key, T>* kv,
                  const UnifiedKiAMT<Key>* ki,
                  Cur cursor,
                  const F& action_for_key) {
        using KvAMT = UnifiedKvAMT<Key, T>;
        using KiAMT = UnifiedKiAMT<Key>;
        using H = DefaultKeyService<Key>;
        using Code = typename H::code_type;
        constexpr int SW = KvAMT::RADIX_LOG2;
        constexpr int WW = (int)KvAMT::WORD_WIDTH;
        constexpr int SLOTS = 1 << SW;

        if (shift == 0)
            co_return co_await unified_leaf<Key, T>(prefix, kv, ki, cursor, action_for_key);

        int child_shift = shift - SW;
        int n_slots = (shift + SW >= WW) ? (1 << (WW - shift)) : SLOTS;

        std::optional<Cur> child_cur[SLOTS] = {};
        skiplist_partition_frame(cursor, (uint64_t)prefix, shift, n_slots, child_cur,
                                 [](const auto& key) -> uint64_t { return H{}.encode(key.first); });

        std::pair<const KvAMT*, const KiAMT*> results[SLOTS] = {};
        Coroutine::Nursery nursery;
        for (int c = 0; c < n_slots; ++c) {
            Code child_prefix = prefix | ((Code)c << shift);
            const KvAMT* kv_c = unified_extract_child(kv, c, shift);
            const KiAMT* ki_c = unified_extract_child(ki, c, shift);
            if (!child_cur[c]) {
                results[c] = {kv_c, ki_c}; // no mods: share
            } else {
                co_await nursery.fork(results[c],
                    unified_frame<Key, T>(child_prefix, child_shift, kv_c, ki_c,
                                          *child_cur[c], action_for_key));
            }
        }
        co_await nursery.join();

        const KvAMT* kv_out[SLOTS];
        const KiAMT* ki_out[SLOTS];
        for (int c = 0; c < n_slots; ++c) {
            kv_out[c] = results[c].first;
            ki_out[c] = results[c].second;
        }
        const KvAMT* kv2 = unified_assemble(prefix, shift, kv_out, n_slots);
        const KiAMT* ki2 = unified_assemble(prefix, shift, ki_out, n_slots);
        co_return std::pair<const KvAMT*, const KiAMT*>{kv2, ki2};
    }

    template<typename Key, typename T, typename U, typename F, typename S2, typename D2>
    Coroutine::Future<WaitableMap<Key, T>>
    coroutine_parallel_rebuild2_unified(const WaitableMap<Key, T>& source,
                                        const ConcurrentMap<Key, U, S2, D2>& modifier,
                                        F&& action_for_key) {
        using KvMap = PersistentMap<Key, T, DefaultKeyService<Key>, ScanDiscipline>;
        using KiMap = PersistentMap<Key, WaitSet, DefaultKeyService<Key>, ScanDiscipline>;
        using KvAMT = UnifiedKvAMT<Key, T>;
        using KiAMT = UnifiedKiAMT<Key>;
        using Code = typename DefaultKeyService<Key>::code_type;
        const KvAMT* kv = source.kv._inner ? &*source.kv._inner : nullptr;
        const KiAMT* ki = source.ki._inner ? &*source.ki._inner : nullptr;
        constexpr int SW = KvAMT::RADIX_LOG2;
        constexpr int WW = (int)KvAMT::WORD_WIDTH;
        int top_shift = ((WW - 1) / SW) * SW; // largest multiple of SW below WW
        auto cursor = modifier.make_cursor();
        std::pair<const KvAMT*, const KiAMT*> roots =
            co_await unified_frame<Key, T>((Code)0, top_shift, kv, ki, cursor, action_for_key);
        WaitableMap<Key, T> result;
        result.kv = KvMap{ typename KvMap::Slot{ roots.first } };
        result.ki = KiMap{ typename KiMap::Slot{ roots.second } };
        co_return result;
    }

} // namespace wry

#endif /* waitable_map_hpp */

