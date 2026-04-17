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
    
    namespace array_mapped_trie {
        
        
        
        
        template<typename T, typename Key, typename U, typename F>
        Node<T> const*
        _parallel_rebuild(Node<T> const* source,
                          F&& action,
                          uint64_t prefix,
                          int shift,
                          typename ConcurrentMap<Key, U>::Cursor cursor
                          ) {
            
        }
        
        
        template<typename T, typename Key, typename U, typename F>
        Node<T> const*
        parallel_rebuild(Node<T> const* source,
                         ConcurrentMap<Key, U> const& modifier,
                         F&& action) {
            return _parallel_rebuild(source,
                                     action,
                                     0,
                                     63,
                                     modifier.make_cursor());
        }
        

    }
    

     
} // namespace wry

#endif /* parallel_rebuild_hpp */
