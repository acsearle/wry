//
//  world.hpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#ifndef world_hpp
#define world_hpp

#include <set>
#include <map>
#include <unordered_map>

#include "sim.hpp"
#include "simd.hpp"
#include "table.hpp"
#include "tile.hpp"
#include "utility.hpp"
#include "machine.hpp"
#include "queue.hpp"
#include "HeapTable.hpp"

namespace wry::sim {
    
    // somewhat abstracted interface
    
    struct World;
    struct Entity;
    
    Time world_time(World* world);

    // entity scheduling
    void entity_add_to_world(Entity* entity, World* world);
    void entity_ready_on_world(Entity* entity, World* world);
    
    void entity_wait_on_world_time(Entity* entity, World* world, Time when);
    void entity_wait_on_world_coordinate(Entity* entity, World* world, Coordinate where);
    void entity_wait_on_world_entity(Entity* entity, World* world, Entity* who);

    void notify_by_world_time(World* world, Time t);
    void notify_by_world_coordinate(World* world, Coordinate xy);
    void notify_by_world_entity(World* world, Entity* f);
    
    // cooperative transactional memory

    bool can_read_world_coordinate(World* world, Coordinate where);
    bool can_write_world_coordinate(World* world, Coordinate where);
    
    void did_read_world_coordinate(World* world, Coordinate where);
    void did_write_world_coordinate(World* world, Coordinate where);

    Value peek_world_coordinate_value(World* world, Coordinate where);
    void set_world_coordinate_value(World* world, Coordinate where, Value what);
    void clear_world_coordinate_value(World* world, Coordinate where);

    Entity* peek_world_coordinate_occupant(World* world, Coordinate where);
    void set_world_coordinate_occupant(World* world, Coordinate where, Entity* who);
    void clear_world_coordinate_occupant(World* world, Coordinate where);

    bool can_read_world_entity(World* world, Entity* who);
    void did_read_world_entity(World* world, Entity* who);
    
    bool can_write_world_entity(World* world, Entity* who);
    void did_write_world_entity(World* world, Entity* who);

    
    
    
    struct World {
        
        // State
        
        Time _tick = 0;
        gc::HashMap<Coordinate, gc::Scan<Value>> _value_for_coordinate;
        gc::HashMap<Coordinate, gc::Scan<Entity*>> _occupant_for_coordinate;

        // Participants, in no particular order
        
        // Array<Entity*> _entities;
        gc::GCArray<gc::Scan<Entity*>> _entities;

        // Conditions
        
        gc::HashMap<Time,       QueueOfUnique<gc::Scan<Entity*>>> _waiting_for_time;
        gc::HashMap<Coordinate, QueueOfUnique<gc::Scan<Entity*>>> _waiting_for_coordinate;
        gc::HashMap<gc::Scan<Entity*>,    QueueOfUnique<gc::Scan<Entity*>>> _waiting_for_entity;
                            
        QueueOfUnique<gc::Scan<Entity*>>  _ready;
        
        // Spatial hashing
        // TODO: we may variously need
        // - entities at a Coordinate, for specific lookup
        //   - how does this differ from Occupancy
        // - entities in a screen-scale chunk == masked Coordinate
        
        // Do all Entities have a _location?  If they don't, they can't participate
        // in the localized memory system.  A global memory system, perhaps
        // "radio channels" has to operate differently (read old state, submit
        // new state, all changes resolved commutatively somehow, such as xor)
        
        // Pointers are a "free" sparse map by identity, we shouldn't give up
        // Entity* lightly for an EntityID
        
        // Entity Component Systems, database normalization, and the fact that
        // many particpants only rarely need various attributes all support
        // breaking up heavyweight OOP objects
        // - Most coordinates are not realized
        //   - Don't use a grid
        // - Most realized coordinates are near others
        //   - Maybe use a sparse map of dense chunks?
        // - Most coordinates are not occupied and involved in transactions
        //   - Don't keep transactions and occupants in the same structure
        //     as the more common values
        
        
        // Transactions
        
        // These are an example of non-owning per-frame temporary objects
        
        HashMap<Coordinate, TRANSACTION_STATE> _transaction_state_for_coordinate;
        HashMap<Entity*,    TRANSACTION_STATE> _transaction_state_for_entity;
                
        void step() {
            
            ++_tick;
            notify_by_world_time(this, _tick);

            QueueOfUnique<gc::Scan<Entity*>> working{std::move(_ready)};

            assert(_ready.empty());
            assert(_transaction_state_for_entity.empty());
            assert(_transaction_state_for_coordinate.empty());

            // for (Entity* entity : working)
                // entity->notify(this);
            size_t n = working.queue.size();
            for (size_t i = 0; i != n; ++i) {
                working.queue[i]->notify(this);
            }

            working.clear();

            // TODO: clearing a hash set is O(capacity) not O(size)
            _transaction_state_for_coordinate.clear();
            _transaction_state_for_entity.clear();

        }
                                
    }; // World
        
    inline Time world_time(World* world) {
        assert(world);
        return world->_tick;
    }
    
    inline void entity_wait_on_world_time(Entity* entity, World* world, Time when) {
        assert(entity);
        assert(world);
        assert(when - world->_tick > 0);
        world->_waiting_for_time.find_or_emplace(when).push(entity);
    }
    
    inline void entity_wait_on_world_coordinate(Entity* entity, World* world, Coordinate xy) {
        assert(entity);
        assert(world);
        world->_waiting_for_coordinate.find_or_emplace(xy).push(entity);
    }
    
    inline void entity_wait_on_world_entity(Entity* entity, World* world, Entity* other) {
        assert(entity);
        assert(world);
        assert(other);
        world->_waiting_for_entity.find_or_emplace(other).push(entity);
    }

    inline void entity_ready_on_world(Entity* entity, World* world) {
        assert(entity);
        assert(world);
        world->_ready.push(entity);
    }
    
    inline void notify_by_world_time(World* world, Time when) {
        assert(world);
        // TODO: these iterators aren't gonna be stable enough for this?
        auto pos = world->_waiting_for_time.find(when);
        if (pos) {
            world->_ready.push_range(std::move(pos->second));
            world->_waiting_for_time.erase(pos->first);
        }
    }

    inline void notify_by_world_coordinate(World* world, Coordinate xy) {
        assert(world);
        {
            // TODO: these iterators aren't gonna be stable enough for this?
            auto pos = world->_waiting_for_coordinate.find(xy);
            if (pos) {
                world->_ready.push_range(std::move(pos->second));
                world->_waiting_for_coordinate.erase(pos->first);
            }
        }
        {
            // TODO: these iterators aren't gonna be stable enough for this?
            //auto pos = world->_occupant_for_coordinate.find(xy);
            //if (pos != world->_occupant_for_coordinate.end()) {
            //    entity_ready_on_world(pos->second, world);
            //}
            Entity* p = world->_occupant_for_coordinate.read(xy);
            if (p)
                entity_ready_on_world(p, world);
        }
    }

    inline void notify_by_world_entity(World* world, Entity* entity) {
        assert(entity);
        assert(world);
        auto pos = world->_waiting_for_entity.find(entity);
        if (pos) {
            world->_ready.push_range(std::move(pos->second));
            world->_waiting_for_entity.erase(pos->first);
        }
    }
    
    inline bool can_read_world_coordinate(World* world, Coordinate where) {
        assert(world);
        auto pos = world->_transaction_state_for_coordinate.find(where);
        return (pos == world->_transaction_state_for_coordinate.end()) || (pos->second == TRANSACTION_STATE_READ);
    }
    
    inline void did_read_world_coordinate(World* world, Coordinate where) {
        assert(world);
        auto pos = world->_transaction_state_for_coordinate.find(where);
        if (pos == world->_transaction_state_for_coordinate.end())
            world->_transaction_state_for_coordinate.emplace(where, TRANSACTION_STATE_READ);
        else
            assert(pos->second == TRANSACTION_STATE_READ);
    }

    inline bool can_write_world_coordinate(World* world, Coordinate where) {
        assert(world);
        return !world->_transaction_state_for_coordinate.contains(where);
    }

    inline void did_write_world_coordinate(World* world, Coordinate where) {
        assert(world);
        auto [pos, did_emplace] = world->_transaction_state_for_coordinate.emplace(where, TRANSACTION_STATE_WRITE);
        assert(did_emplace);
        notify_by_world_coordinate(world, where);
    }
    
    inline Value peek_world_coordinate_value(World* world, Coordinate where) {
        assert(world);
//        auto pos = world->_value_for_coordinate.find(where);
//        if (pos != world->_value_for_coordinate.end())
//            return pos->second;
//        else
//            return Value();
        return (world->_value_for_coordinate.read(where));
    }
    
    inline void set_world_coordinate_value(World* world, Coordinate where, Value what) {
        assert(world);
        assert(!value_is_null(what));
        // world->_value_for_coordinate[where] = std::move(what);
        world->_value_for_coordinate.write(where, std::move(what));
    }
    
    inline void clear_world_coordinate_value(World* world, Coordinate where) {
        assert(world);
        world->_value_for_coordinate.erase(where);
    }
    
    inline Entity* peek_world_coordinate_occupant(World* world, Coordinate where) {
        assert(world);
        //Entity* who = nullptr;
        // auto pos = world->_occupant_for_coordinate.find(where);
        //if (pos != world->_occupant_for_coordinate.end()) {
        //    who = pos->second;
        //    assert(who);
        //}
        // return who;
        return world->_occupant_for_coordinate.read(where);
    }

    inline void set_world_coordinate_occupant(World* world, Coordinate where, Entity* who) {
        assert(world);
        assert(who);
        // world->_occupant_for_coordinate.emplace(where, who);
        world->_occupant_for_coordinate.write(where, who);
    }

    inline void clear_world_coordinate_occupant(World* world, Coordinate where) {
        assert(world);
        world->_occupant_for_coordinate.erase(where);
    }
    
    inline bool can_read_world_entity(World* world, Entity* who) {
        auto pos = world->_transaction_state_for_entity.find(who);
        return (pos == world->_transaction_state_for_entity.end()) || (pos->second == TRANSACTION_STATE_READ);
    }
    
    inline void did_read_world_entity(World* world, Entity* who) {
        auto pos = world->_transaction_state_for_entity.find(who);
        if (pos == world->_transaction_state_for_entity.end())
            world->_transaction_state_for_entity.emplace(who, TRANSACTION_STATE_READ);
        else
            assert(pos->second == TRANSACTION_STATE_READ);
    }
    
    inline bool can_write_world_entity(World* world, Entity* who) {
        return !world->_transaction_state_for_entity.contains(who);
    }
    
    inline void did_write_world_entity(World* world, Entity* who) {
        auto [pos, did_emplace] = world->_transaction_state_for_entity.emplace(who, TRANSACTION_STATE_WRITE);
        assert(did_emplace);
        notify_by_world_entity(world, who);
    }
    
    inline void entity_add_to_world(Entity* who, World* world) {
        world->_entities.push_back(who);
    }
    
    inline void shade(const sim::World& self) {
        adl::shade(self._value_for_coordinate);
        adl::shade(self._occupant_for_coordinate);
        adl::shade(self._entities);
        adl::shade(self._waiting_for_time);
        adl::shade(self._waiting_for_coordinate);
        adl::shade(self._waiting_for_entity);
        adl::shade(self._ready);
    }

    
} // namespace wry::sim

#endif /* world_hpp */
