//
//  world.hpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#ifndef world_hpp
#define world_hpp

#include "sim.hpp"
#include "simd.hpp"
#include "tile.hpp"
#include "utility.hpp"
#include "machine.hpp"
#include "queue.hpp"

#include "persistent_set.hpp"
#include "persistent_map.hpp"
#include "waitable_map.hpp"


namespace wry {
    
    // somewhat abstracted interface
    
    struct World;
    struct Entity;
    
    Time world_get_time(const World* world);
    
    struct World : GarbageCollected {
        
        Time _time;

        WaitableMap<Coordinate, EntityID, CoordinateHasher> _entity_id_for_coordinate;
        WaitableMap<EntityID, const Entity*, EntityIDHasher> _entity_for_entity_id;
        WaitableMap<Coordinate, Value, CoordinateHasher> _value_for_coordinate;
        

        PersistentMap<Time, PersistentSet<EntityID, EntityIDHasher>> _waiting_on_time;
        
        World()
        : _time{0}
        , _entity_id_for_coordinate{}
        , _entity_for_entity_id{}
        , _value_for_coordinate{}
        , _waiting_on_time{}
        {
        }
        
        World(Time time,
              WaitableMap<Coordinate, EntityID, CoordinateHasher> entity_id_for_coordinate,
              WaitableMap<EntityID, const Entity*, EntityIDHasher> entity_for_entity_id,
              WaitableMap<Coordinate, Value, CoordinateHasher> value_for_coordinate,
              PersistentMap<Time, PersistentSet<EntityID, EntityIDHasher>> waiting_on_time)
        : _time(time)
        , _entity_id_for_coordinate(entity_id_for_coordinate)
        , _entity_for_entity_id(entity_for_entity_id)
        , _value_for_coordinate(value_for_coordinate)
        , _waiting_on_time(waiting_on_time)
        {}
        
        virtual ~World() = default;
        
        virtual void _garbage_collected_scan() const override;

        Coroutine::Future<World*> step() const;
                                            
    }; // World
        
    inline Time world_get_time(const World* world) {
        assert(world);
        return world->_time;
    }
    
    inline void garbage_collected_shade(const World& self) {
        garbage_collected_shade(&self);
    }
    
   
    
} // namespace wry::sim


#endif /* world_hpp */
