//
//  tile.hpp
//  client
//
//  Created by Antony Searle on 9/10/2023.
//

#ifndef tile_hpp
#define tile_hpp

#include "sim.hpp"
#include "contiguous_deque.hpp"
#include "entity.hpp"

namespace wry::sim {
    
    // todo: we squander lots of memory here; there will be many more
    // tiles than machines, so having multiple queue headers inline is
    // wasteful; we should employ some sparse strategy
    
    // "infinite": procedural terrain
    // explored: just terrain
    // common: stuff
    // rare: waiters
    
    // - separate tables for values?
    // - machine-intrusive linked-list queues (but, each machine can be in
    //   several queues)
    // - tagged pointers; common values inline; point out to more complex
    //   values; recycle the pointee as the values and queues vary
    // - tiles hotswap themselves with more complex implementations as they
    //   acquire dependents
    //
    
    // empty tiles are infinite
    // tiles are common
    // locks are rare
    // contested locks are rarer
    // observers are rare
    
    
    /*
    
    struct Tile {

        // Transactor _transaction;
        Value _value;
        // Entity* _occupant;
        
        // void notify_occupant(World* world);
        
    };
     */
    
    // We've simplified Tiles down to just Values
    //
    // we may later reuse the name for underlying terrain 
    
    
} // namespace wry::sim

#endif /* tile_hpp */

