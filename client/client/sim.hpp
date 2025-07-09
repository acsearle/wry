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

namespace wry::sim {
    
    using Time = i64;
    
    inline void trace(const Time&) { }
    inline void shade(const Time&) { }

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
        
    }; // struct Coordinate
    
    inline u64 hash(const Coordinate& x) {
        return hash_combine(&x, sizeof(x));
    }
    
    inline void trace(const Coordinate&) {}
    inline void shade(const Coordinate&) {}
    
    struct MortonCoordinate {
        uint64_t data;
        constexpr bool operator==(const MortonCoordinate&) const = default;
        constexpr auto operator<=>(const MortonCoordinate&) const = default;
    };

    inline void trace(const MortonCoordinate&) {}
    inline void shade(const MortonCoordinate&) {}

    
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
    
    inline void trace(const EntityID&) {}
    inline void shade(const EntityID&) {}
    
    struct TransactionContext;
    
}

#include "value.hpp"


#endif /* sim_hpp */
