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
#include "coordinate.hpp"

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
    
    struct TransactionContext;
    
}

#include "value.hpp"


#endif /* sim_hpp */
