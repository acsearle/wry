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
        // The actual key-value mapping
        PersistentMap<Key, T> valuemap;
        // The set of entities to notify when we write for a key; this will
        // typically be a much smaller collection
        PersistentMap<Key, PersistentSet<EntityID>> waitset;
        
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
        
    };
    
    template<typename Key, typename T, typename U, typename F>
    WaitableMap<Key, T> parallel_rebuild(const WaitableMap<Key, T>& w,
                                         const ConcurrentMap<Key, U>& value_modifications,
                                         const ConcurrentMap<Key, EntityID>& waitlist_modifications,
                                         F&& action_for_key) {
        abort();
        /*
        WaitableMap<Key, T> result = w;
        
        auto a = value_modifications.begin();
        auto b = value_modifications.end();
        auto c = waitlist_modifications.begin();
        auto d = waitlist_modifications.end();
        
        for (;;) {
            // Sentinel
        }
        
        
        
        
        // Simple single-threaded implementation
        
        // TODO: Descend the two trees and build the new map from the leaves up,
        // reusing unaltered subtrees.  This is equivalent to inserting each
        // modification in turn, but avoids constructing many transitory
        // interior nodes, and can be made highly parallel.
        //
        // Note that the trie structure (unlike
        
        // WaitableMap<Key, T> result;
        // SAFETY: We can use the map unlocked here because it is immutable in
        // this phase
        auto first = modifier.begin();
        auto last = modifier.end();
        for (; first != last; ++first) {
            ParallelRebuildAction<T> action = action_for_key(*first);
            // Possible actions for a given key:
            // TRY_ERASE the key
            // INSERT_OR_ASSIGN a new value
            // REPLACE waiters
            // MERGE waiters
            switch (action.tag) {
                case ParallelRebuildAction<T>::NONE:
                    break;
                case ParallelRebuildAction<T>::WRITE_VALUE:
                    result.set(first->first, action.value);
                    break;
                case ParallelRebuildAction<T>::CLEAR_VALUE:
                    (void) result.try_erase(first->first, action.value);
                    break;
            }
        }
        return result;
         */
    };
    
    
    /*
    template<typename Key, typename T, typename U, typename F>
    WaitableMap<Key, T> parallel_rebuild(const WaitableMap<Key, T>& w,
                                         const ConcurrentMap<Key, U>& modifier,
                                         F&& action_for_key) {
        // Simple single-threaded implementation
        
        // TODO: Descend the two trees and build the new map from the leaves up,
        // reusing unaltered subtrees.  This is equivalent to inserting each
        // modification in turn, but avoids constructing many transitory
        // interior nodes, and can be made highly parallel.
        //
        // Note that the trie structure (unlike
        
        WaitableMap<Key, T> result;
        // SAFETY: We can use the map unlocked here because it is immutable in
        // this phase
        auto first = modifier.begin();
        auto last = modifier.end();
        for (; first != last; ++first) {
            ParallelRebuildAction<T> action = action_for_key(*first);
            // Possible actions for a given key:
            // TRY_ERASE the key
            // INSERT_OR_ASSIGN a new value
            // REPLACE waiters
            // MERGE waiters
            switch (action.tag) {
                case ParallelRebuildAction<T>::NONE:
                    break;
                case ParallelRebuildAction<T>::WRITE_VALUE:
                    result.set(first->first, action.value);
                    break;
                case ParallelRebuildAction<T>::CLEAR_VALUE:
                    (void) result.try_erase(first->first, action.value);
                    break;
            }
        }
        return result;
    };
     */
    
}

#endif /* waitable_map_hpp */
