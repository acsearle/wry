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
                
        virtual void notify(World&);
        
    };

    struct Source : LocalizedEntity {
        
        Value _of_this;
        
        virtual void notify(World&);

    };

    struct Sink : LocalizedEntity {
                
        virtual void notify(World&);

    };

} // namespace wry::sim
#endif /* spawner_hpp */
