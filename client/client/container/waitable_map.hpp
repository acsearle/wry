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

    // Apply one ki (waiter-index) action to the nested map, in place.  WRITE
    // replaces a key's waitset, CLEAR erases it, MERGE is the read-modify-write
    // upsert (union the new waiters into the existing set).  Serial for now (the
    // ki map is sparse); the same WRITE/CLEAR/MERGE semantics will move into a
    // combine when ki is parallelized.
    template<typename Key>
    void apply_ki_action(PersistentMap<Key, WaitSet, DefaultKeyService<Key>, ScanDiscipline>& ki,
                         Key key,
                         const ParallelRebuildAction<std::vector<EntityID>>& action) {
        using A = ParallelRebuildAction<std::vector<EntityID>>;
        switch (action.tag) {
            case A::NONE:
                break;
            case A::WRITE_VALUE: {
                WaitSet ws;
                for (EntityID e : action.value)
                    ws.set(e);
                ki.set(key, ws);
                break;
            }
            case A::CLEAR_VALUE: {
                assert(action.value.empty());
                WaitSet victim;
                (void) ki.try_erase(key, victim);
                break;
            }
            case A::MERGE_VALUE: {
                WaitSet ws;
                (void) ki.try_get(key, ws); // empty if absent
                for (EntityID e : action.value)
                    ws.set(e);
                ki.set(key, ws);
                break;
            }
        }
    }

    // Stage 0 (serial) -- kept as the oracle for the differential test.
    template<typename Key, typename T, typename U, typename F, typename S2, typename D2>
    Coroutine::Future<WaitableMap<Key, T>>
    coroutine_parallel_rebuild2_serial(const WaitableMap<Key, T>& source,
                                       const ConcurrentMap<Key, U, S2, D2>& modifier,
                                       F&& action_for_key) {
        WaitableMap<Key, T> result{source};
        // SAFETY: We can iterate the concurrent map here because it is
        // immutable in this phase
        auto first = modifier.begin();
        auto last = modifier.end();
        for (; first != last; ++first) {
            std::pair<ParallelRebuildAction<T>, ParallelRebuildAction<std::vector<EntityID>>> p;
            p = co_await action_for_key(*first);
            switch (p.first.tag) {
                case ParallelRebuildAction<T>::NONE:
                    break;
                case ParallelRebuildAction<T>::WRITE_VALUE:
                    result.kv.set(first->first, p.first.value);
                    break;
                case ParallelRebuildAction<T>::CLEAR_VALUE:
                    (void) result.kv.try_erase(first->first, p.first.value);
                    break;
                case ParallelRebuildAction<T>::MERGE_VALUE:
                    abort();
            }
            apply_ki_action(result.ki, first->first, p.second);
        }
        co_return result;
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

    // The dense kv value map and the (now nested, sparse) ki waiter index are
    // both rebuilt in parallel via the AMT co-recursion, forked concurrently.
    // One serial pass splits each key's bundled action into a kv value action and
    // a ki waiter action (NONE dropped to keep subtree sharing); both vectors are
    // code-ordered because the modifier is.  World::step() calls this; the name
    // is unchanged, so its call sites are not.
    template<typename Key, typename T, typename U, typename F, typename S2, typename D2>
    Coroutine::Future<WaitableMap<Key, T>>
    coroutine_parallel_rebuild2(const WaitableMap<Key, T>& source,
                               const ConcurrentMap<Key, U, S2, D2>& modifier,
                               F&& action_for_key) {
        WaitableMap<Key, T> result{source};
        using Code = typename DefaultKeyService<Key>::code_type;
        std::vector<std::pair<Code, ParallelRebuildAction<T>>> kv_mods;
        std::vector<std::pair<Code, ParallelRebuildAction<std::vector<EntityID>>>> ki_mods;
        for (auto first = modifier.begin(); first != modifier.end(); ++first) {
            std::pair<ParallelRebuildAction<T>, ParallelRebuildAction<std::vector<EntityID>>> p
                = co_await action_for_key(*first);
            Code code = DefaultKeyService<Key>{}.encode(first->first);
            if (p.first.tag != ParallelRebuildAction<T>::NONE)
                kv_mods.emplace_back(code, std::move(p.first));
            if (p.second.tag != ParallelRebuildAction<std::vector<EntityID>>::NONE)
                ki_mods.emplace_back(code, std::move(p.second));
        }
        Coroutine::Nursery nursery;
        co_await nursery.fork(result.kv,
            coroutine_parallel_rebuild_from_mods(source.kv, kv_mods,
                                                 ParallelRebuildValueCombine<T>{}));
        co_await nursery.fork(result.ki,
            coroutine_parallel_rebuild_from_mods(source.ki, ki_mods,
                                                 WaitSetMergeCombine{}));
        co_await nursery.join();
        co_return result;
    }

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

    // Apply the modifier entries [first, last) -- the mods of one 32-key leaf
    // range -- to the kv and ki leaves.
    template<typename Key, typename T, typename It, typename F>
    Coroutine::Future<std::pair<const UnifiedKvAMT<Key, T>*, const UnifiedKiAMT<Key>*>>
    unified_leaf(const UnifiedKvAMT<Key, T>* kv,
                 const UnifiedKiAMT<Key>* ki,
                 It first, It last,
                 const F& action_for_key) {
        using KvAMT = UnifiedKvAMT<Key, T>;
        using KiAMT = UnifiedKiAMT<Key>;
        using H = DefaultKeyService<Key>;
        using Code = typename H::code_type;
        using ValA = ParallelRebuildAction<T>;
        using KiA = ParallelRebuildAction<std::vector<EntityID>>;
        const KvAMT* kv2 = kv;
        const KiAMT* ki2 = ki;
        for (It it = first; it != last; ++it) {
            Code code = H{}.encode(it->first);
            std::pair<ValA, KiA> p = co_await action_for_key(*it);
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

    // Co-recurse over kv, ki and the modifier entries [first, last) for this frame
    // (sorted by code, all within the frame's range).  A single forward sweep
    // buckets the mods into child slots -- no re-seek, no materialization, and
    // empty children cost O(1).
    template<typename Key, typename T, typename It, typename F>
    Coroutine::Future<std::pair<const UnifiedKvAMT<Key, T>*, const UnifiedKiAMT<Key>*>>
    unified_frame(typename DefaultKeyService<Key>::code_type prefix, int shift,
                  const UnifiedKvAMT<Key, T>* kv,
                  const UnifiedKiAMT<Key>* ki,
                  It first, It last,
                  const F& action_for_key) {
        using KvAMT = UnifiedKvAMT<Key, T>;
        using KiAMT = UnifiedKiAMT<Key>;
        using H = DefaultKeyService<Key>;
        using Code = typename H::code_type;
        constexpr int SW = KvAMT::RADIX_LOG2;
        constexpr int WW = (int)KvAMT::WORD_WIDTH;
        constexpr int SLOTS = 1 << SW;

        if (shift == 0)
            co_return co_await unified_leaf<Key, T>(kv, ki, first, last, action_for_key);

        int child_shift = shift - SW;
        int n_slots = (shift + SW >= WW) ? (1 << (WW - shift)) : SLOTS;

        std::pair<const KvAMT*, const KiAMT*> results[SLOTS] = {};
        Coroutine::Nursery nursery;
        It it = first;
        for (int c = 0; c < n_slots; ++c) {
            Code child_prefix = prefix | ((Code)c << shift);
            const KvAMT* kv_c = unified_extract_child(kv, c, shift);
            const KiAMT* ki_c = unified_extract_child(ki, c, shift);
            // Bucket: advance `it` over the mods belonging to child c.
            It child_begin = it;
            if (c == n_slots - 1) {
                it = last; // the final child gets the remaining mods
            } else {
                Code child_hi = prefix | ((Code)(c + 1) << shift);
                while (it != last && H{}.encode(it->first) < child_hi)
                    ++it;
            }
            if (child_begin == it) {
                results[c] = {kv_c, ki_c}; // no mods: share
            } else {
                co_await nursery.fork(results[c],
                    unified_frame<Key, T>(child_prefix, child_shift, kv_c, ki_c,
                                          child_begin, it, action_for_key));
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
        std::pair<const KvAMT*, const KiAMT*> roots =
            co_await unified_frame<Key, T>((Code)0, top_shift, kv, ki,
                                           modifier.begin(), modifier.end(), action_for_key);
        WaitableMap<Key, T> result;
        result.kv = KvMap{ typename KvMap::Slot{ roots.first } };
        result.ki = KiMap{ typename KiMap::Slot{ roots.second } };
        co_return result;
    }

} // namespace wry

#endif /* waitable_map_hpp */
