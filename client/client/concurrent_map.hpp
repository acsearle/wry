//
//  concurrent_map.hpp
//  client
//
//  Created by Antony Searle on 19/7/2025.
//

#ifndef concurrent_map_hpp
#define concurrent_map_hpp

#include <map>
#include <mutex>

#include "utility.hpp"

namespace wry {
    
    template<typename Key, typename T>
    struct StableConcurrentMap {
        std::mutex _mutex;
        std::map<Key, T> _map;
        
        bool insert_or_assign(auto&& k, auto&& v) {
            std::unique_lock lock{_mutex};
            return _map.insert_or_assign(FORWARD(k),
                                         FORWARD(v)).first;
        }
        
        decltype(auto) subscript_and_mutate(auto&& k, auto&& f) {
            std::unique_lock lock{_mutex};
            return FORWARD(f)(_map[FORWARD(k)]);
        }
        
        decltype(auto) access(auto&& f) {
            std::unique_lock lock{_mutex};
            FORWARD(f)(_map);
        }
        
    };
    
}
#endif /* concurrent_map_hpp */
