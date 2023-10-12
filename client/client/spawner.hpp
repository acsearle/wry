//
//  spawner.hpp
//  client
//
//  Created by Antony Searle on 11/10/2023.
//

#ifndef spawner_hpp
#define spawner_hpp

#include "entity.hpp"

namespace wry::sim {

    struct LocalizedEntity : Entity {
        
        Coordinate _location = {};

    };
    
    struct Spawner : LocalizedEntity {
                
        virtual void wake_location_locked(World&, Coordinate);
        virtual void wake_location_changed(World&, Coordinate);
        virtual void wake_time_elapsed(World&, Time);
        
    };

    struct Source : LocalizedEntity {
        
        Value _of_this;
        
        virtual void wake_location_locked(World&, Coordinate);
        virtual void wake_location_changed(World&, Coordinate);
        virtual void wake_time_elapsed(World&, Time);
        
    };

    struct Sink : LocalizedEntity {
                
        virtual void wake_location_locked(World&, Coordinate);
        virtual void wake_location_changed(World&, Coordinate);
        virtual void wake_time_elapsed(World&, Time);
        
    };

} // namespace wry::sim
#endif /* spawner_hpp */
