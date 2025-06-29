//
//  context.hpp
//  client
//
//  Created by Antony Searle on 3/12/2024.
//

#ifndef context_hpp
#define context_hpp

#include "transaction.hpp"

namespace wry::sim {
    
    struct Context {
        
        const World* world;
        StableConcurrentMap<EntityID, Atomic<const Transaction::Node*>> _transactions_for_entity;
        StableConcurrentMap<Coordinate, Atomic<const Transaction::Node*>> _transactions_for_coordinate;
        StableConcurrentMap<Time, Atomic<const Transaction::Node*>> _transactions_for_time;
        
        
        
    };
    
}

#endif /* context_hpp */
