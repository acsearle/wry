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
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        
        struct Action {
            enum Tag {
                NONE,
                WRITE_VALUE_FOR_COORDINATE,
                
            };
            Tag tag = {};
            union {
                struct {
                    Coordinate coordinate;
                    Term value = {};
                };
            };
        };
        
        mutable BlockingDeque<Action> _queue;
                
        virtual void notify(TransactionContext*) const override;

        virtual void _garbage_collected_scan() const override;


        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Player");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final {}


    };
    
} // namespace wry

#endif /* player_hpp */
