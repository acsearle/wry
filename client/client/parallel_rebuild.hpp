//
//  parallel_rebuild.hpp
//  client
//
//  Created by Antony Searle on 4/11/2025.
//

#ifndef parallel_rebuild_hpp
#define parallel_rebuild_hpp

#include "concurrent_map.hpp"
#include "coroutine.hpp"
#include "persistent_map.hpp"

namespace wry {
    
    template<typename T, typename K, typename U, typename Compare>
    void parallel_rebuild3(array_mapped_trie::Node<T> const* source,
                           typename ConcurrentSkiplistSet<std::pair<K, U>, Compare>::FrozenCursor cursor) {
        
        void const* results[64];
        
        
    }
    
    
    /*
    
    template<typename Key, typename T, typename U, typename F>
    coroutine::co_fork parallel_rebuild2(const PersistentMap<Key, T>& source,
                                         const ConcurrentMap<Key, U>& modifier,
                                         F&& action_for_key) {
        
        
        array_mapped_trie::Node<T>const* a = source._inner;
        
        
        
        
        // Simple single-threaded implementation
        
        // TODO: Descend the two trees and rebuild up from the leaves.
        // This can be made highly parallel.
        
        PersistentMap<Key, T> result{source};
        // SAFETY: We can iterate the concurrent map here because it is
        // immutable in this phase
        auto first = modifier.begin();
        auto last = modifier.end();
        for (; first != last; ++first) {
            ParallelRebuildAction<T> action = action_for_key(*first);
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
    }
     */
     
}

#endif /* parallel_rebuild_hpp */
