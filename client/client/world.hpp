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
    
    Time world_get_time(const World* world);
    
    template<typename Key, typename T>
    struct WaitableMap {
        PersistentMap<Key, T> _map;
        PersistentMap<Key, PersistentSet<EntityID>> _waiting;
    };
    
    template<typename Key, typename T>
    void trace(const WaitableMap<Key, T>& x, void* context) {
        trace(x._map, context);
        trace(x._waiting, context);
    }

    struct World : GarbageCollected {
        
        Time _time;

        WaitableMap<Coordinate, EntityID> _entity_id_for_coordinate;
        WaitableMap<EntityID, const Entity*> _entity_for_entity_id;
        WaitableMap<Coordinate, Value> _value_for_coordinate;
        

        PersistentSet<EntityID> _ready;
        PersistentMap<Time, PersistentSet<EntityID>> _waiting_on_time;
        
        World()
        : _time{0}
        , _entity_id_for_coordinate{}
        , _entity_for_entity_id{}
        , _value_for_coordinate{}
        , _ready{}
        , _waiting_on_time{}
        {
        }
        
        World(Time time,
              WaitableMap<Coordinate, EntityID> entity_id_for_coordinate,
              WaitableMap<EntityID, const Entity*> entity_for_entity_id,
              WaitableMap<Coordinate, Value> value_for_coordinate,
              PersistentSet<EntityID> ready,
              PersistentMap<Time, PersistentSet<EntityID>> waiting_on_time)
        : _time(time)
        , _entity_id_for_coordinate(entity_id_for_coordinate)
        , _entity_for_entity_id(entity_for_entity_id)
        , _value_for_coordinate(value_for_coordinate)
        , _ready(ready)
        , _waiting_on_time(waiting_on_time)
        {}
        
        virtual ~World() = default;
        
        virtual void _garbage_collected_enumerate_fields(TraceContext*) const override;

        World* step() const;
                                            
    }; // World
        
    inline Time world_get_time(const World* world) {
        assert(world);
        return world->_time;
    }
    
    inline void shade(const World& self) {
        shade(&self);
    }
    
   
    
} // namespace wry::sim


#endif /* world_hpp */
