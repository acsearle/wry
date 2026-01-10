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
        // IDs are never reused, setting a hard scaling limit
        // TODO: We can compact IDs and times when loading, reducing this limit
        // to a per-session limit
    };
    
    inline u64 hash(const EntityID& x) {
        return hash_combine(&x, sizeof(x));
    }
    
    template<typename> struct DefaultKeyService;
    
    
    template<>
    struct DefaultKeyService<EntityID> {
        using key_type = EntityID;
        using hash_type = uint64_t;
        
        constexpr hash_type hash(key_type key) const {
            return key.data;
        }
        
        constexpr key_type unhash(hash_type h) const {
            return EntityID{.data = h};
        }
        
        constexpr bool compare(key_type a, key_type b) const {
            return hash(a) < hash(b);
        }

    
    };
    
    
    inline void garbage_collected_scan(const EntityID&) {}
    inline void garbage_collected_shade(const EntityID&) {}
    
} // namespace wry

#endif /* entity_id_hpp */
