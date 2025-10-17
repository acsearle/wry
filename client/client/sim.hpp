//
//  sim.hpp
//  client
//
//  Created by Antony Searle on 9/10/2023.
//

#ifndef sim_hpp
#define sim_hpp

#include "stdint.hpp"
#include "hash.hpp"
#include "entity_id.hpp"

namespace wry {
    
    using Time = i64;
    
    inline void garbage_collected_scan(const Time&) { }
    inline void garbage_collected_shade(const Time&) { }

    struct World;
    struct Coordinate;
    struct Entity;
    struct EntityID;
    struct Transaction;
    struct TransactionSet;
    
    enum HEADING
    : i64 {
        
        HEADING_NORTH = 0,
        HEADING_EAST = 1,
        HEADING_SOUTH = 2,
        HEADING_WEST = 3,
        HEADING_MASK = 3
        
    };
    
    enum TRANSACTION_STATE {
        
        TRANSACTION_STATE_NONE = 0,
        TRANSACTION_STATE_READ = 1,
        TRANSACTION_STATE_WRITE = 2,
        TRANSACTION_STATE_FORBIDDEN = 3,
        
    };
    
    
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
    
    inline uint64_t persistent_map_index_for_key(Coordinate key) {
        return key.data();
    }
    
    inline void garbage_collected_scan(const Coordinate&) {}
    inline void garbage_collected_shade(const Coordinate&) {}
    
    struct MortonCoordinate {
        uint64_t data;
        constexpr bool operator==(const MortonCoordinate&) const = default;
        constexpr auto operator<=>(const MortonCoordinate&) const = default;
    };

    inline void garbage_collected_scan(const MortonCoordinate&) {}
    inline void garbage_collected_shade(const MortonCoordinate&) {}

    
    struct TransactionContext;
    
}

#include "value.hpp"


#endif /* sim_hpp */
