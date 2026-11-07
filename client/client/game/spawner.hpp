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

        Coordinate _location = {};

        template<typename V>
        void _fields(V& v) const {
            Entity::_fields(v);
            v.state("_location", _location);
        }

    };
    
    struct Spawner : Fields<Spawner, LocalizedEntity> {

        template<typename V>
        void _fields(V& v) const {
            LocalizedEntity::_fields(v);
        }

        virtual int64_t notify(TransactionContext*) const override;

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Spawner");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Spawner* make_mutable_clone() const override {
            return new Spawner(*this);
        }

    };

    struct Source : Fields<Source, LocalizedEntity> {

        Term _of_this;

        template<typename V>
        void _fields(V& v) const {
            LocalizedEntity::_fields(v);
            v.state("_of_this", _of_this);
        }

        virtual int64_t notify(TransactionContext*) const override;

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Source");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Source* make_mutable_clone() const override {
            return new Source(*this);
        }

    };

    struct Sink : Fields<Sink, LocalizedEntity> {

        template<typename V>
        void _fields(V& v) const {
            LocalizedEntity::_fields(v);
        }

        virtual int64_t notify(TransactionContext*) const override;

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Sink");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Sink* make_mutable_clone() const override {
            return new Sink(*this);
        }

    };
    
    struct Counter : Fields<Counter, LocalizedEntity> {

        template<typename V>
        void _fields(V& v) const {
            LocalizedEntity::_fields(v);
        }

        virtual int64_t notify(TransactionContext*) const override;
        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Counter");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Counter* make_mutable_clone() const override {
            return new Counter(*this);
        }

    };
    
    struct Evenator : Fields<Evenator, LocalizedEntity> {

        template<typename V>
        void _fields(V& v) const {
            LocalizedEntity::_fields(v);
        }

        virtual int64_t notify(TransactionContext*) const override;

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Evenator");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Evenator* make_mutable_clone() const override {
            return new Evenator(*this);
        }

    };

    // Ship: a moored bulk carrier, the market.  It occupies a line of
    // SHIP_LENGTH cells: the prow at _location, pointing along _heading,
    // then SHIP_HOLDS open holds, then the stern with its superstructure.
    // Board-game logic, not simulation: anything put into a hold (by a
    // machine's STORE or DUMP from the cell beside it) that has a price
    // (matter.hpp MATTER_PRICE) vanishes, and its price is added to the
    // World's money.  Anything unpriced stays in the hold, which then
    // blocks further deliveries there until it is taken back out.  The
    // Ship occupies its cells, so machines drive around it, not through.
    struct Ship : Fields<Ship, LocalizedEntity> {

        static constexpr int SHIP_HOLDS = 4;
        static constexpr int SHIP_LENGTH = SHIP_HOLDS + 2;

        i64 _heading = HEADING_EAST;

        template<typename V>
        void _fields(V& v) const {
            LocalizedEntity::_fields(v);
            v.state("_heading", _heading);
        }

        // Cell k of the ship, k = 0 the prow, 1..SHIP_HOLDS the holds,
        // SHIP_LENGTH - 1 the stern: k cells aft of the prow
        Coordinate cell(int k) const {
            Coordinate c = _location;
            switch (_heading & 3) {
                case HEADING_NORTH: c.y -= k; break;
                case HEADING_EAST:  c.x -= k; break;
                case HEADING_SOUTH: c.y += k; break;
                case HEADING_WEST:  c.x += k; break;
            }
            return c;
        }

        virtual int64_t notify(TransactionContext*) const override;

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::Ship");
        virtual uint64_t _save_type_tag() const override final { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override final;

        [[nodiscard]] virtual Ship* make_mutable_clone() const override {
            return new Ship(*this);
        }

    };

} // namespace wry::sim
#endif /* spawner_hpp */
