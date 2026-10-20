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
        
    struct Player : Fields<Player, Entity> {

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
        
        // Client-local pending input, consumed by notify(); not
        // simulation state, so neither saved nor hashed.
        mutable BlockingDeque<Action> _queue;

        template<typename V>
        void _fields(V& v) const {
            Entity::_fields(v);
            v.transient("_queue", _queue);
        }

        virtual int64_t notify(TransactionContext*) const override;


        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Player");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Player* make_mutable_clone() const override {
            abort();
        }

    };

    // The queue's elements hold Terms; defined in player.cpp, declared
    // here because the scan is instantiated wherever Player's field
    // enumeration is (term.hpp Fields).
    void garbage_collected_scan(const Player::Action& x);
    
} // namespace wry

#endif /* player_hpp */
