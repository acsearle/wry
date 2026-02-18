//
//  concurrent_map.hpp
//  client
//
//  Created by Antony Searle on 19/7/2025.
//

#ifndef concurrent_map_hpp
#define concurrent_map_hpp

#include "concurrent_skiplist.hpp"

namespace wry {
    
    template<typename Key, typename T>
    using ConcurrentMap = ConcurrentSkiplistMap<Key, T>;
    
} // namespace wry

#endif /* concurrent_map_hpp */
