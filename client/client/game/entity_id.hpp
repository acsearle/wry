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
        constexpr EntityID& operator++() { ++data; return *this; }
        constexpr EntityID operator++(int) { return EntityID{data++}; }
        constexpr EntityID& operator+=(uint64_t n) { data += n; return *this; }
    };
    
    inline u64 hash(const EntityID& x) {
        return hash_combine(&x, sizeof(x));
    }
    
    template<typename> struct DefaultKeyService;
    
    
    template<>
    struct DefaultKeyService<EntityID> {
        using key_type = EntityID;
        using code_type = uint64_t;

        constexpr code_type encode(key_type key) const {
            return key.data;
        }

        constexpr key_type decode(code_type h) const {
            return EntityID{.data = h};
        }

        constexpr bool operator()(key_type a, key_type b) const {
            return encode(a) < encode(b);
        }

    
    };
    
    
    inline void garbage_collected_scan(const EntityID&) {}
    inline void garbage_collected_shade(const EntityID&) {}
    
} // namespace wry

#endif /* entity_id_hpp */
