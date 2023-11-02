//
//  entity.hpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#ifndef entity_hpp
#define entity_hpp

#include "array.hpp"
#include "sim.hpp"
#include "utility.hpp"
#include "string.hpp"

namespace wry::sim {
    
    // This is the base class of things with behavior
    
    struct Entity {
        
        u64 _persistent_id = {}; // use this to lookup all cold data?
        
        virtual ~Entity() = default;
        
        virtual void wake_location_locked(World&, Coordinate) = 0;
        virtual void wake_location_changed(World&, Coordinate) = 0;
        virtual void wake_time_elapsed(World&, Time) = 0;

    }; // struct Entity
  
};

#endif /* entity_hpp */
