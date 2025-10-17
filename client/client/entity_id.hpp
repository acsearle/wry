//
//  entity_id.hpp
//  client
//
//  Created by Antony Searle on 15/10/2025.
//

#ifndef entity_id_hpp
#define entity_id_hpp

#include "stdint.hpp"
#include "hash.hpp"

namespace wry {
    
    struct EntityID {
        uint64_t data;
        constexpr bool operator==(const EntityID&) const = default;
        constexpr auto operator<=>(const EntityID&) const = default;
        constexpr explicit operator bool() const { return (bool)data; }
        
        // defer the difficult problem to getting new unique EntityIDs in a way
        // that is independent of thread scheduling across different machines
        static EntityID oracle();
        
    };
    
    inline u64 hash(const EntityID& x) {
        return hash_combine(&x, sizeof(x));
    }
    
    inline uint64_t persistent_map_index_for_key(EntityID entity_id) {
        return entity_id.data;
    }
    
    
    inline void garbage_collected_scan(const EntityID&) {}
    inline void garbage_collected_shade(const EntityID&) {}
    
} // namespace wry

#endif /* entity_id_hpp */
