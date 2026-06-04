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
#include "term.hpp"

#include "persistent_set.hpp"
#include "persistent_map.hpp"
#include "save_types.hpp"
#include "waitable_map.hpp"


namespace wry {

    // somewhat abstracted interface

    struct World;
    struct Entity;
    struct Saver;
    struct Loader;

    Time world_get_time(const World* world);

    // World IS-A HeapTerm.  It can travel as the OBJECT payload of a
    // Term, which unifies the save-format polymorphic dispatch path
    // (every snapshotable thing reaches the registry through
    // Saver::visit_heap_value) and lets the simulation root be a
    // Term-shaped reference if/when that's wanted.  Operator hooks
    // inherit ERROR defaults from HeapTerm; World is identity-only
    // (does not override _term_eq / _term_less / _term_hash).
    struct World : HeapTerm {

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::World");

        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }


        Time _time;

        WaitableMap<Coordinate, EntityID> _entity_id_for_coordinate;
        WaitableMap<EntityID, const Entity*> _entity_for_entity_id;
        WaitableMap<Coordinate, Term> _term_for_coordinate;

        using Set = PersistentSet<std::pair<Time, EntityID>, DefaultKeyService<std::pair<Time, EntityID>>, ScanDiscipline>;
        Set _waiting_on_time;


        World()
        : _time{0}
        , _entity_id_for_coordinate{}
        , _entity_for_entity_id{}
        , _term_for_coordinate{}
        , _waiting_on_time{}
        {
        }

        World(Time time,
              WaitableMap<Coordinate, EntityID> entity_id_for_coordinate,
              WaitableMap<EntityID, const Entity*> entity_for_entity_id,
              WaitableMap<Coordinate, Term> value_for_coordinate,
              Set waiting_on_time)
        : _time(time)
        , _entity_id_for_coordinate(entity_id_for_coordinate)
        , _entity_for_entity_id(entity_for_entity_id)
        , _term_for_coordinate(value_for_coordinate)
        , _waiting_on_time(waiting_on_time)
        {}

        virtual ~World() {
            // printf("~World at %p\n", this);
        }

        virtual void _garbage_collected_scan() const override;

        // Save format dispatch.  Body implementation lives in
        // io/save_format.cpp alongside the other save bodies.
        virtual uint64_t _save_type_tag() const override { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override;

        Coroutine::Future<Root<World*>> step() const;

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
