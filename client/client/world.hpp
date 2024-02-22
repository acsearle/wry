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

namespace wry::sim {
    
    // somewhat abstracted interface
    
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
        HashMap<Coordinate, Value> _value_for_coordinate;
        Array<Entity*> _entities;
        
        HashMap<Coordinate, Entity*> _occupancy;

        // Conditions
        
        HashMap<Time,       QueueOfUnique<Entity*>> _waiting_on_time;
        HashMap<Coordinate, QueueOfUnique<Entity*>> _waiting_on_coordinate;
        HashMap<Entity*,    QueueOfUnique<Entity*>> _waiting_on_entity;
                            
        QueueOfUnique<Entity*>  _ready;
        
        // Transactions

        HashMap<Coordinate, TRANSACTION_STATE> _transaction_for_coordinate;
        HashMap<Entity*,    TRANSACTION_STATE> _transaction_for_entity;

        void step() {
            
            ++_tick;
            notify_by_world_time(this, _tick);

            QueueOfUnique<Entity*> working{std::move(_ready)};

            assert(_ready.empty());
            assert(_transaction_for_entity.empty());
            assert(_transaction_for_coordinate.empty());

            for (Entity* entity : working)
                entity->notify(this);

            working.clear();
            _transaction_for_coordinate.clear();
            _transaction_for_entity.clear();

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
        world->_waiting_on_time[when].push(entity);
    }
    
    inline void entity_wait_on_world_coordinate(Entity* entity, World* world, Coordinate xy) {
        assert(entity);
        assert(world);
        world->_waiting_on_coordinate[xy].push(entity);
    }
    
    inline void entity_wait_on_world_entity(Entity* entity, World* world, Entity* other) {
        assert(entity);
        assert(world);
        assert(other);
        world->_waiting_on_entity[other].push(entity);
    }

    inline void entity_ready_on_world(Entity* entity, World* world) {
        assert(entity);
        assert(world);
        world->_ready.push(entity);
    }
    
    inline void notify_by_world_time(World* world, Time when) {
        assert(world);
        auto pos = world->_waiting_on_time.find(when);
        if (pos != world->_waiting_on_time.end()) {
            world->_ready.push_range(std::move(pos->second));
            world->_waiting_on_time.erase(pos);
        }
    }

    inline void notify_by_world_coordinate(World* world, Coordinate xy) {
        assert(world);
        {
            auto pos = world->_waiting_on_coordinate.find(xy);
            if (pos != world->_waiting_on_coordinate.end()) {
                world->_ready.push_range(std::move(pos->second));
                world->_waiting_on_coordinate.erase(pos);
            }
        }
        {
            auto pos = world->_occupancy.find(xy);
            if (pos != world->_occupancy.end()) {
                entity_ready_on_world(pos->second, world);
            }
        }
    }

    inline void notify_by_world_entity(World* world, Entity* entity) {
        assert(entity);
        assert(world);
        auto pos = world->_waiting_on_entity.find(entity);
        if (pos != world->_waiting_on_entity.end()) {
            world->_ready.push_range(std::move(pos->second));
            world->_waiting_on_entity.erase(pos);
        }
    }
    
    inline bool can_read_world_coordinate(World* world, Coordinate where) {
        assert(world);
        auto pos = world->_transaction_for_coordinate.find(where);
        return (pos == world->_transaction_for_coordinate.end()) || (pos->second == TRANSACTION_STATE_READ);
    }
    
    inline void did_read_world_coordinate(World* world, Coordinate where) {
        assert(world);
        auto pos = world->_transaction_for_coordinate.find(where);
        if (pos == world->_transaction_for_coordinate.end())
            world->_transaction_for_coordinate.emplace(where, TRANSACTION_STATE_READ);
        else
            assert(pos->second == TRANSACTION_STATE_READ);
    }

    inline bool can_write_world_coordinate(World* world, Coordinate where) {
        assert(world);
        return !world->_transaction_for_coordinate.contains(where);
    }

    inline void did_write_world_coordinate(World* world, Coordinate where) {
        assert(world);
        auto [pos, did_emplace] = world->_transaction_for_coordinate.emplace(where, TRANSACTION_STATE_WRITE);
        assert(did_emplace);
        notify_by_world_coordinate(world, where);
    }
    
    inline Value peek_world_coordinate_value(World* world, Coordinate where) {
        assert(world);
        auto pos = world->_value_for_coordinate.find(where);
        if (pos != world->_value_for_coordinate.end())
            return pos->second;
        else
            return { DISCRIMINANT_NONE, 0 };
    }
    
    inline void set_world_coordinate_value(World* world, Coordinate where, Value what) {
        assert(world);
        assert(what.discriminant);
        world->_value_for_coordinate[where] = what;
    }
    
    inline void clear_world_coordinate_value(World* world, Coordinate where) {
        assert(world);
        world->_value_for_coordinate.erase(where);
    }
    
    inline Entity* peek_world_coordinate_occupant(World* world, Coordinate where) {
        assert(world);
        Entity* who = nullptr;
        auto pos = world->_occupancy.find(where);
        if (pos != world->_occupancy.end()) {
            who = pos->second;
            assert(who);
        }
        return who;
    }

    inline void set_world_coordinate_occupant(World* world, Coordinate where, Entity* who) {
        assert(world);
        assert(who);
        world->_occupancy.emplace(where, who);
    }

    inline void clear_world_coordinate_occupant(World* world, Coordinate where) {
        assert(world);
        world->_occupancy.erase(where);
    }
    
    inline bool can_read_world_entity(World* world, Entity* who) {
        auto pos = world->_transaction_for_entity.find(who);
        return (pos == world->_transaction_for_entity.end()) || (pos->second == TRANSACTION_STATE_READ);
    }
    
    inline void did_read_world_entity(World* world, Entity* who) {
        auto pos = world->_transaction_for_entity.find(who);
        if (pos == world->_transaction_for_entity.end())
            world->_transaction_for_entity.emplace(who, TRANSACTION_STATE_READ);
        else
            assert(pos->second == TRANSACTION_STATE_READ);
    }
    
    inline bool can_write_world_entity(World* world, Entity* who) {
        return !world->_transaction_for_entity.contains(who);
    }
    
    inline void did_write_world_entity(World* world, Entity* who) {
        auto [pos, did_emplace] = world->_transaction_for_entity.emplace(who, TRANSACTION_STATE_WRITE);
        assert(did_emplace);
        notify_by_world_entity(world, who);
    }
    
    inline void entity_add_to_world(Entity* who, World* world) {
        world->_entities.push_back(who);
    }
    
} // namespace wry::sim


/*
 printf("--\n");
 for (Entity* p : _entities) {
 if (Machine* q = dynamic_cast<Machine*>(p)) {
 printf("machine @ %p\n", q);
 printf("    phase is %d\n", q->_phase);
 printf("    new_location is %d %d\n", q->_new_location.x, q->_new_location.y);
 } else {
 printf("entity @ %p\n", p);
 }
 
 // check that everything is waiting on something somewhere
 int n = 0;
 for (Entity* e : _waiting_on_step) {
 if (p == e) {
 printf("    is _ready\n");
 ++n;
 }
 }
 
 for (const auto& [k, v] : _waiting_on_time)
 for (Entity* e : v)
 if (p == e) {
 printf("    is _waiting_on_time %lld\n", k);
 ++n;
 }
 
 for (const auto& [k, v] : _waiting_on_coordinate) {
 for (Entity* e : v) {
 if (p == e) {
 printf("    is _waiting_on_coordinate %d %d\n", k.x, k.y);
 ++n;
 }
 }
 }
 
 for (const auto& [k, v] : _waiting_on_entity) {
 for (Entity* e : v) {
 if (p == e) {
 printf("    is _waiting_on_entity %p\n", k);
 ++n;
 }
 }
 }
 
 if (!n) {
 printf("    is_abandoned!\n");
 }
 */

#endif /* world_hpp */
