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
    
    template<typename Key, typename T>
    struct WaitableMap {
        
        // TODO: We are bloating WaitableMap by allocating a waitset pointer
        // for every entry even though they will be rare

        PersistentMap<Key, T> kv;
        PersistentSet<std::pair<Key, EntityID>> ki;
        
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
    
    template<typename Key, typename T, typename U, typename F>
    Coroutine::Future<WaitableMap<Key, T>>
    coroutine_parallel_rebuild2(const WaitableMap<Key, T>& source,
                               const ConcurrentMap<Key, U>& modifier,
                               F&& action_for_key) {
        // Simple single-threaded implementation
        
        // TODO: Descend the two trees and rebuild up from the leaves.
        // This can be made highly parallel.
        
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
            switch (p.second.tag) {
                case ParallelRebuildAction<T>::NONE:
                    break;
                case ParallelRebuildAction<T>::WRITE_VALUE:
                    result.ki = as_multimap_replace(result.ki, first->first, std::move(p.second.value));
                    break;
                case ParallelRebuildAction<T>::CLEAR_VALUE:
                    assert(p.second.value.empty());
                    result.ki = as_multimap_erase(result.ki, first->first);
                    break;
                case ParallelRebuildAction<T>::MERGE_VALUE:
                    result.ki = as_multimap_merge(result.ki, first->first, std::move(p.second.value));
                    break;
            }
        }
        co_return result;
    }

} // namespace wry

#endif /* waitable_map_hpp */
