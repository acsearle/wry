//
//  player.hpp
//  client
//
//  Created by Antony Searle on 18/10/2025.
//

#ifndef player_hpp
#define player_hpp

#include "entity.hpp"
#include "mutex.hpp"
#include "concurrent_queue.hpp"

namespace wry {
        
    struct Player : Entity {
        
        struct Action {
            enum Tag {
                NONE,
                WRITE_VALUE_FOR_COORDINATE,
                
            };
            Tag tag = {};
            union {
                struct {
                    Coordinate coordinate;
                    Value value = {};
                };
            };
        };
        
        mutable BlockingDeque<Action> _queue;
                
        virtual void notify(TransactionContext*) const override;

        virtual void _garbage_collected_scan() const override;
        
    };
    
} // namespace wry

#endif /* player_hpp */
