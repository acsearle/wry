//
//  map.hpp
//  client
//
//  Created by Antony Searle on 20/10/2025.
//

#ifndef map_hpp
#define map_hpp

#include <map>

namespace wry {
    
    template<typename Key, typename T, typename Compare>
    void garbage_collected_scan(const std::map<Key, T, Compare>& m) {
        for (const auto& p : m)
            garbage_collected_scan(p);
    }

} // namespace wry

#endif /* map_hpp */
