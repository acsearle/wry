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

#include "persistent_set.hpp"
#include "persistent_map.hpp"

#include "array_mapped_trie.hpp"


namespace wry::sim {
    
    // somewhat abstracted interface
    
    struct World;
    struct Entity;
    
    Time world_time(const World* world);

    // entity scheduling
    void entity_add_to_world(Entity* entity, World* world);
    inline void entity_ready_on_world(Entity* entity, World* world) {}
    
    void entity_wait_on_world_time(Entity* entity, World* world, Time when);
    void entity_wait_on_world_coordinate(Entity* entity, World* world, Coordinate where);
    void entity_wait_on_world_entity(Entity* entity, World* world, Entity* who);

    void notify_by_world_time(World* world, Time t);
    inline void notify_by_world_coordinate(World* world, Coordinate xy) {}
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

    struct World : GarbageCollected {
        
        Time _tick;

        PersistentSet<EntityID> _ready;

        PersistentMap<EntityID, const Entity*> _entity_for_entity_id;
        PersistentMap<Coordinate, Value> _value_for_coordinate;
        
        // these maps are transactional; transactions may make conflicting
        // changes to entity-for-id and to values-for-coordinates
        
        // (entities could be unbundled into more tables)
        // PersistentMap<EntityID, const Stack*>
        // PersistentMap<EntityID, Coordinate>
        // PersistentMap<EntityID, Orientation>
        
        
        // these multimaps are not transactional in their own right, rather they
        // are conflict-free replicated data types (or something); when the
        // transactions commit or abort, we may insert to these data structures
        // without the possibility of failure.  How does this square with
        // rebuilding, especially when double-layered?
        PersistentMap<Coordinate, PersistentSet<EntityID>> _entity_id_for_coordinate;
        PersistentMap<Time, PersistentSet<EntityID>> _waiting_for_time;
        PersistentMap<EntityID, PersistentSet<EntityID>> _waiting_for_entity_id;
        PersistentMap<Coordinate, PersistentSet<EntityID>> _waiting_for_coordinate;
        // These multimaps are also equivalent to PersistentSet<uint128_t>
        // The fact they are set<pair<a, b>> is what makes them CRDT; insertion
        // is idempotent

        
                
        virtual void _garbage_collected_enumerate_fields(TraceContext*) const override;

        
        World()
        : _tick{0}
        , _ready{}
        , _entity_for_entity_id{}
        , _value_for_coordinate{}
        , _entity_id_for_coordinate{}
        , _waiting_for_time{}
        , _waiting_for_entity_id{}
        , _waiting_for_coordinate{}
        {
        }
        
        World(Time tick,
              PersistentMap<EntityID, const Entity*> entity_for_entity_id,
              PersistentSet<EntityID> ready,
              PersistentMap<Time, PersistentSet<EntityID>> waiting_for_time,
              PersistentMap<EntityID, PersistentSet<EntityID>> waiting_for_entity_id,
              PersistentMap<Coordinate, PersistentSet<EntityID>> waiting_for_coordinate,
              PersistentMap<Coordinate, Value> value_for_coordinate,
              PersistentMap<Coordinate, PersistentSet<EntityID>> entity_id_for_coordinate)
        : _tick(tick)
        , _entity_for_entity_id(entity_for_entity_id)
        , _ready(ready)
        , _waiting_for_time(waiting_for_time)
        , _waiting_for_entity_id(waiting_for_entity_id)
        , _waiting_for_coordinate(waiting_for_coordinate)
        , _value_for_coordinate(value_for_coordinate)
        , _entity_id_for_coordinate(entity_id_for_coordinate)
        {}
        
        virtual ~World() {
            // printf("%s\n", __PRETTY_FUNCTION__);
        }
        

        World* step() const;
                                            
    }; // World
        
    inline Time world_time(const World* world) {
        assert(world);
        return world->_tick;
    }
    
    inline void shade(const World& self) {
        shade(&self);
    }
    
    /*
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
    
     */
    
} // namespace wry::sim


namespace wry::sim {
        
    struct WorkBag {
    };
    
    template<typename>
    struct CallTarget;
    
    template<typename>
    struct CallHandle;
    
    template<typename R, typename... Args>
    struct CallTarget<R(Args...)> {
        R (*_function_pointer)(void*, Args...);
    };
    
    template<typename R, typename... Args>
    struct CallHandle<R(Args...)> {
        CallTarget<R(Args...)>* _target;
        
        R operator()(Args... args) const {
            return (*(_target->_function_pointer))(_target, std::forward<Args>(args)...);
        }
        
    };
        
} // namespace wry



#endif /* world_hpp */
