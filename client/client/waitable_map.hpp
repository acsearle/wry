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
    
    template<typename Key, typename T, typename H = DefaultKeyService<Key>>
    struct WaitableMap {
        
        // TODO: We are bloating WaitableMap by allocating a waitset pointer
        // for every entry even though they will be rare

        /*
        PersistentMap<Key, T> valuemap;
        PersistentMap<Key, PersistentSet<EntityID>> waitset;
         */
        // PersistentMap<Key, std::pair<T, PersistentSet<EntityID>>, H> inner;
        // PersistentSet<std::pair<Key, EntityID>>
        
        PersistentMap<Key, T> kv;
        PersistentSet<std::pair<Key, EntityID>> ki;
        
        bool try_get(Key key, T& victim) const {
            return kv.try_get(key, victim);
        }
        
        void set(Key key, T desired) {
            kv.set(key, std::move(desired));
        }
        
        /*
        void try_get(Key key, std::pair<std::optional<T>, std::optional<PersistentSet<EntityID>>>& victim) const {
            T a;
            if (valuemap.try_get(key, a)) {
                victim.first = a;
            } else {
                victim.first.reset();
            }
            PersistentSet<EntityID> b;
            if (waitset.try_get(key, b)) {
                victim.second = b;
            } else {
                victim.second.reset();
            }
        }
        
        void set(Key key, std::pair<std::optional<T>, std::optional<PersistentSet<EntityID>>>& desired) {
            if (desired.first) {
                valuemap.set(key, *(desired.first));
            } else {
                T victim{};
                if (valuemap.try_erase(key, victim)) {
                    desired.first = victim;
                }
            }
            if (desired.second) {
                waitset.set(key, *(desired.second));
            } else {
                PersistentSet<EntityID> victim{};
                if (waitset.try_erase(key, victim)) {
                    desired.second = victim;
                }
            }
        }
         */
        
    };
    
    template<typename Key, typename T, typename H>
    void garbage_collected_scan(const WaitableMap<Key, T, H>& x) {
        garbage_collected_scan(x.kv);
        garbage_collected_scan(x.ki);
    }
    
    /*
    template<typename Key, typename T, typename H, typename U, typename F>
    WaitableMap<Key, T, H> parallel_rebuild(const WaitableMap<Key, T, H>& w,
                                         const ConcurrentMap<Key, U>& value_modifications,
                                         F&& action_for_key) {
        return WaitableMap<Key, T, H>{
            parallel_rebuild(w.inner,
                             value_modifications,
                             std::forward<F>(action_for_key))
        };
    }

    template<typename Key, typename T, typename H, typename U, typename F>
    Coroutine::Future<WaitableMap<Key, T, H>>
    coroutine_parallel_rebuild(const WaitableMap<Key, T, H>& w,
                               const ConcurrentMap<Key, U>& value_modifications,
                               F&& action_for_key) {
        co_return WaitableMap<Key, T, H>{
            co_await coroutine_parallel_rebuild(w.inner,
                                                value_modifications,
                                                std::forward<F>(action_for_key))
        };
    }
     */
    
    template<typename Key, typename T, typename H, typename U, typename F>
    Coroutine::Future<WaitableMap<Key, T, H>>
    coroutine_parallel_rebuild2(const WaitableMap<Key, T, H>& source,
                               const ConcurrentMap<Key, U>& modifier,
                               F&& action_for_key) {
        // Simple single-threaded implementation
        
        // TODO: Descend the two trees and rebuild up from the leaves.
        // This can be made highly parallel.
        
        WaitableMap<Key, T, H> result{source};
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
            switch (p.second.tag) {
                case ParallelRebuildAction<T>::NONE:
                    break;
                case ParallelRebuildAction<T>::WRITE_VALUE:
                    mutator_overwrote(result.ki._inner);
                    result.ki = as_multimap_replace(result.ki, first->first, std::move(p.second.value));
                    break;
                case ParallelRebuildAction<T>::CLEAR_VALUE:
                    assert(p.second.value.empty());
                    mutator_overwrote(result.ki._inner);
                    result.ki = as_multimap_erase(result.ki, first->first);
                    break;
                case ParallelRebuildAction<T>::MERGE_VALUE:
                    mutator_overwrote(result.ki._inner);
                    result.ki = as_multimap_merge(result.ki, first->first, std::move(p.second.value));
                    break;
            }
        }
        co_return result;
    }

    
}

#endif /* waitable_map_hpp */
