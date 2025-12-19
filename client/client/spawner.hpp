//
//  spawner.hpp
//  client
//
//  Created by Antony Searle on 11/10/2023.
//

#ifndef spawner_hpp
#define spawner_hpp

#include "entity.hpp"

namespace wry {

    struct LocalizedEntity : Entity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        
        Coordinate _location = {};
        virtual void _garbage_collected_scan() const override {
        }

    };
    
    struct Spawner : LocalizedEntity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

                
        virtual void notify(TransactionContext*) const override;
        
    };

    struct Source : LocalizedEntity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        
        Value _of_this;
        
        virtual void notify(TransactionContext*) const override;
        virtual void _garbage_collected_scan() const override {
            garbage_collected_scan(_of_this);
        }

    };

    struct Sink : LocalizedEntity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

                
        virtual void notify(TransactionContext*) const override;
        virtual void _garbage_collected_scan() const override {
        }
        
    };
    
    struct Counter : LocalizedEntity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        
        virtual void notify(TransactionContext*) const override;
        virtual void _garbage_collected_scan() const override {
        }
    };
    
    struct Evenator : LocalizedEntity {
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        virtual void notify(TransactionContext*) const override;
        virtual void _garbage_collected_scan() const override {
        }
    };

} // namespace wry::sim
#endif /* spawner_hpp */
