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
        PersistentMap<Key, T> _map;
        // The set of entities to notify when we write for a key; this will
        // typically be a much smaller collection
        PersistentMap<Key, PersistentSet<EntityID>> _waiting;
    };
    
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
    
}

#endif /* waitable_map_hpp */
