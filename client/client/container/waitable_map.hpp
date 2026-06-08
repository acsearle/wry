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

    // Stage 1: the dense kv value map is rebuilt in parallel via the AMT
    // co-recursion; the sparse ki waiter-index multimap is rebuilt serially (its
    // representation is slated to change -- not worth parallelizing this pass).
    // World::step() calls this; the name is unchanged, so its call sites are not.
    template<typename Key, typename T, typename U, typename F, typename S2, typename D2>
    Coroutine::Future<WaitableMap<Key, T>>
    coroutine_parallel_rebuild2(const WaitableMap<Key, T>& source,
                               const ConcurrentMap<Key, U, S2, D2>& modifier,
                               F&& action_for_key) {
        WaitableMap<Key, T> result{source};
        using Code = typename DefaultKeyService<Key>::code_type;
        // One serial pass: apply the sparse ki actions in place, and collect the
        // dense kv actions (NONE dropped to preserve subtree sharing) for the
        // parallel rebuild.  The modifier is code-ordered, so kv_mods is too.
        std::vector<std::pair<Code, ParallelRebuildAction<T>>> kv_mods;
        for (auto first = modifier.begin(); first != modifier.end(); ++first) {
            std::pair<ParallelRebuildAction<T>, ParallelRebuildAction<std::vector<EntityID>>> p
                = co_await action_for_key(*first);
            apply_ki_action(result.ki, first->first, p.second);
            if (p.first.tag != ParallelRebuildAction<T>::NONE)
                kv_mods.emplace_back(DefaultKeyService<Key>{}.encode(first->first),
                                     std::move(p.first));
        }
        result.kv = co_await coroutine_parallel_rebuild_from_mods(source.kv, kv_mods);
        co_return result;
    }

} // namespace wry

#endif /* waitable_map_hpp */
