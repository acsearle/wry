//
//  coordinate.hpp
//  client
//
//  Created by Antony Searle on 31/12/2025.
//

#ifndef coordinate_hpp
#define coordinate_hpp

#include <compare>
#include <cstring>

#include "stdint.hpp"
#include "hash.hpp"
#include "key_service.hpp"

namespace wry {
    
    struct Coordinate {
        
        i32 x;
        i32 y;
        
        constexpr bool operator==(const Coordinate&) const = default;
        constexpr auto operator<=>(const Coordinate&) const = default;
        
        uint64_t data() const {
            uint64_t a = {};
            std::memcpy(&a, &x, 8);
            return a;
        }
        
    }; // struct Coordinate
    
    inline u64 hash(const Coordinate& x) {
        return hash_combine(&x, sizeof(x));
    }
    
    // z-order (Morton) keys for the 2D coordinates give the tries a quadtree-
    // like structure, so that memory locality tends to follow spatial locality.
    
    template<>
    struct DefaultKeyService<Coordinate> {
        
        using key_type = Coordinate;
        using hash_type = uint64_t;
        
        hash_type hash(key_type xy) const {
            return _morton_from_xy_neon(xy.x, xy.y);
        }
        
        constexpr key_type unhash(hash_type h) const {
            // __builtin_memcpy(&key, &h, 8); // constexpr
            //return key;
            uint64_t xy = morton2_reverse(h);
            Coordinate key = {};
            __builtin_memcpy(&key, &xy, 8);
            return key;
            
        }
        
        constexpr bool compare(key_type a, key_type b) const {
            return hash(a) < hash(b);
        }
        
    }; // DefaultKeyService<Coordinate>
    
    inline void garbage_collected_scan(const Coordinate&) {}
    inline void garbage_collected_shade(const Coordinate&) {}
    
    
    // TODO: Is MortonCoordinate a standalone type, or how we hash coordinates,
    // or how we store coordinates everywhere
    
    // Note that we can actually do arithmetic and comparisons on Morton
    // coordinates by masking out the odd(even) bits such that carrys get
    // propagated correctly.
    
    struct MortonCoordinate {
        uint64_t data;
        constexpr bool operator==(const MortonCoordinate&) const = default;
        constexpr auto operator<=>(const MortonCoordinate&) const = default;
    };
        
    inline void garbage_collected_scan(const MortonCoordinate&) {}
    inline void garbage_collected_shade(const MortonCoordinate&) {}

    
} // namespace wry

#endif /* coordinate_hpp */
