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
        virtual void _garbage_collected_scan(void*) const override {
        }

    };
    
    struct Spawner : LocalizedEntity {
                
        virtual void notify(TransactionContext*) const override;
        
    };

    struct Source : LocalizedEntity {
        
        Value _of_this;
        
        virtual void notify(TransactionContext*) const override;
        virtual void _garbage_collected_scan(void*p) const override {
            trace(_of_this,p);
        }

    };

    struct Sink : LocalizedEntity {
                
        virtual void notify(TransactionContext*) const override;
        virtual void _garbage_collected_scan(void*) const override {
        }
        
    };
    
    struct Counter : LocalizedEntity {
        virtual void notify(TransactionContext*) const override;
        virtual void _garbage_collected_scan(void*) const override {
        }
    };

} // namespace wry::sim
#endif /* spawner_hpp */
