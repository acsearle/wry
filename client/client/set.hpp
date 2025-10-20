//
//  set.hpp
//  client
//
//  Created by Antony Searle on 20/10/2025.
//

#ifndef set_hpp
#define set_hpp

#include <set>

namespace wry {
    
    template<typename Key, typename Compare>
    void garbage_collected_scan(const std::set<Key, Compare>& s) {
        for (const Key& k : s)
            garbage_collected_scan(k);
    }
    
} // namespace wry

#endif /* set_hpp */
