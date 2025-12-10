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

        /*
        PersistentMap<Key, T> valuemap;
        PersistentMap<Key, PersistentSet<EntityID>> waitset;
         */
        PersistentMap<Key, std::pair<T, PersistentSet<EntityID>>> inner;
        
        bool try_get(Key key, T& victim) const {
            std::pair<T, PersistentSet<EntityID>> a{};
            bool b = inner.try_get(key, a);
            if (b)
                victim = std::move(a.first);
            return b;
        }
        
        void set(Key key, T desired) {
            std::pair<T, PersistentSet<EntityID>> a{};
            a.first = desired;
            inner.set(key, a);
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
    
    template<typename Key, typename T>
    void garbage_collected_scan(const WaitableMap<Key, T>& x) {
        garbage_collected_scan(x.inner);
    }
    
    template<typename Key, typename T, typename U, typename F>
    WaitableMap<Key, T> parallel_rebuild(const WaitableMap<Key, T>& w,
                                         const ConcurrentMap<Key, U>& value_modifications,
                                         F&& action_for_key) {
        return WaitableMap<Key, T>{
            parallel_rebuild(w.inner,
                             value_modifications,
                             std::forward<F>(action_for_key))
        };
    }

    template<typename Key, typename T, typename U, typename F>
    Coroutine::Future<WaitableMap<Key, T>>
    coroutine_parallel_rebuild(const WaitableMap<Key, T>& w,
                               const ConcurrentMap<Key, U>& value_modifications,
                               F&& action_for_key) {
        co_return WaitableMap<Key, T>{
            co_await coroutine_parallel_rebuild(w.inner,
                                                value_modifications,
                                                std::forward<F>(action_for_key))
        };
    }

    
}

#endif /* waitable_map_hpp */
