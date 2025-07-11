//
//  entity.hpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#ifndef entity_hpp
#define entity_hpp

#include "sim.hpp"

namespace wry::sim {
        
    // This is the base class of things with behavior
    //
    // They are GarbageCollected, but are they Values?
        
    struct Entity : GarbageCollected {
                        
        virtual ~Entity() = default;
        
        virtual void notify(TransactionContext*) const {}

        EntityID _entity_id;
        
        Entity();

    }; // struct Entity
      
};

#endif /* entity_hpp */
