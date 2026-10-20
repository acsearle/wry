//
//  player.hpp
//  client
//
//  Created by Antony Searle on 18/10/2025.
//

#ifndef player_hpp
#define player_hpp

#include "entity.hpp"

namespace wry {

    // A client's presence in the simulation: an EntityID that acts on the
    // client's behalf.  It holds no input of its own.  Each step's actions
    // arrive as the step's input (server.hpp: Command), tagged with the
    // client id that sent them, and notify() applies those whose client
    // maps to this entity (World::_player_for_client).  A Player is spawned
    // by the JOIN server event and never acts again after LEAVE (idle in
    // place; multiplayer.md section 6).
    struct Player : Fields<Player, Entity> {

        // One thing a client asks the simulation to do.  Total: applied
        // against whatever the World is when its step comes, degrading to
        // a no-op (multiplayer.md section 3).  The Term is rooted for as
        // long as the action waits for its step.  Its canonical text is a
        // command line (command_types.hpp: format_action / parse_action).
        struct Action {
            enum Tag : uint8_t {
                NONE,
                WRITE_VALUE_FOR_COORDINATE,
            };
            Tag tag = NONE;
            Coordinate coordinate = {};
            Root<Term> value;
        };

        template<typename V>
        void _fields(V& v) const {
            Entity::_fields(v);
        }

        virtual int64_t notify(TransactionContext*) const override;

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Player");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Player* make_mutable_clone() const override {
            abort();
        }

    };

    inline Player::Action make_write_action(Coordinate at, Term value) {
        Player::Action a;
        a.tag = Player::Action::WRITE_VALUE_FOR_COORDINATE;
        a.coordinate = at;
        a.value = value;
        return a;
    }

} // namespace wry

#endif /* player_hpp */
