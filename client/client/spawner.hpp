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
        virtual void _object_scan() const override {
        }

    };
    
    struct Spawner : LocalizedEntity {
                
        virtual void notify(World*) override;
        
    };

    struct Source : LocalizedEntity {
        
        Value _of_this;
        
        virtual void notify(World*) override;
        virtual void _object_scan() const override {
            value_trace(_of_this);
        }

    };

    struct Sink : LocalizedEntity {
                
        virtual void notify(World*) override;
        virtual void _object_scan() const override {
        }
        
    };

} // namespace wry::sim
#endif /* spawner_hpp */
