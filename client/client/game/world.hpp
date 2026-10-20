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
#include "terrain.hpp"
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

    struct ReadyValue {
        mutable int64_t n = -1;
        mutable int64_t requested = -1;
    };

    inline void garbage_collected_scan(ReadyValue const&) {}

    // World IS-A HeapTerm.  It can travel as the OBJECT payload of a
    // Term, which unifies the save-format polymorphic dispatch path
    // (every snapshotable thing reaches the registry through
    // Saver::visit_heap_value) and lets the simulation root be a
    // Term-shaped reference if/when that's wanted.  Operator hooks
    // inherit ERROR defaults from HeapTerm; World is identity-only
    // (does not override _term_eq / _term_less / _term_hash).
    struct World : Fields<World, HeapTerm> {

        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::World");

        Time _time;

        // Starts at 1; 0 is used to represent no EntityID
        EntityID _entity_id_source;


        FrozenSkiplistMap<EntityID, ReadyValue, DefaultKeyService<EntityID>, ScanDiscipline> _ready;

        // Occupancy vs location (split 2026-07-26):
        //
        // _entity_id_for_coordinate is OCCUPANCY: mutually exclusive sole
        // occupant or empty.  One entity can occupy several cells (a
        // travelling machine holds both endpoints).  Occupancy is what
        // blocks movement.
        //
        // _located_for_coordinate is LOCATION: the authoritative
        // Coordinate -> Set<EntityID> multimap of everything "at" a cell,
        // independent of occupancy.  Non-occupying entities (Source,
        // Sink, Spawner) appear only here; machines appear in both maps
        // (deliberate duplication -- the noted-not-built alternative is
        // to treat occupancy as one location source and this map as the
        // non-occupants' side, unioning at query time).  Region queries
        // over this map can surface an entity once per cell it spans;
        // consumers de-duplicate.  The value type is EntityIDSet, the same
        // PersistentSet<EntityID> instantiation the ki waiter index uses
        // under the name WaitSet, deliberately, so the save format reuses
        // the same entry emitter and registry entry.
        WaitableMap<Coordinate, EntityID> _entity_id_for_coordinate;
        WaitableMap<Coordinate, EntityIDSet> _located_for_coordinate;
        WaitableMap<EntityID, const Entity*> _entity_for_entity_id;
        WaitableMap<Coordinate, Term> _term_for_coordinate;
        WaitableMap<Coordinate, Terrain> _terrain_for_coordinate;

        using Set = PersistentSet<std::pair<Time, EntityID>, DefaultKeyService<std::pair<Time, EntityID>>, ScanDiscipline>;
        Set _waiting_on_time;

        World()
        : _time{0}
        , _entity_id_source{1}
        , _ready{}
        , _entity_id_for_coordinate{}
        , _located_for_coordinate{}
        , _entity_for_entity_id{}
        , _term_for_coordinate{}
        , _terrain_for_coordinate{}
        , _waiting_on_time{}
        {
        }

        World(Time time,
              EntityID entity_id_source,
              FrozenSkiplistMap<EntityID, ReadyValue, DefaultKeyService<EntityID>, ScanDiscipline> ready,
              WaitableMap<Coordinate, EntityID> entity_id_for_coordinate,
              WaitableMap<Coordinate, EntityIDSet> located_for_coordinate,
              WaitableMap<EntityID, const Entity*> entity_for_entity_id,
              WaitableMap<Coordinate, Term> value_for_coordinate,
              WaitableMap<Coordinate, Terrain> terrain_for_coordinate,
              Set waiting_on_time)
        : _time(time)
        , _entity_id_source(entity_id_source)
        , _ready(ready)
        , _entity_id_for_coordinate(entity_id_for_coordinate)
        , _located_for_coordinate(located_for_coordinate)
        , _entity_for_entity_id(entity_for_entity_id)
        , _term_for_coordinate(value_for_coordinate)
        , _terrain_for_coordinate(terrain_for_coordinate)
        , _waiting_on_time(waiting_on_time)
        {
        }

        virtual ~World() {
            // printf("~World at %p\n", this);
        }

        void hack_repair_invariant();

        // The saved and hashed shape of the schedule: the time wheel with
        // the ready set folded in as the (_time, id) bucket.  The save
        // builds that set; the hash, a sum over members, adds the bucket's
        // per-member hashes to the wheel's cached sum (step() asserts that
        // nothing waits at or before _time outside _ready, which is what
        // makes the two equal).  _ready itself is enumerated as transient:
        // scanned, never saved or hashed on its own.
        struct TimeWheelFold {
            const Set& wheel;
            const FrozenSkiplistMap<EntityID, ReadyValue, DefaultKeyService<EntityID>, ScanDiscipline>& ready;
            Time time;
        };

        // Save format dispatch.  Body implementation lives in
        // io/save_format.cpp alongside the other save bodies.
        virtual uint64_t _save_type_tag() const override { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override;

        // The World content hash (multiplayer.md 9.1): what lockstep peers
        // compare each step, and what a loaded save must reproduce.  The
        // positional combine over these fields (core/fields.hpp): the map
        // roots' cached hashes, O(1) each, plus the ready fold.  step()
        // seals the new World before returning; hack_repair_invariant
        // seals a commissioned one.
        template<typename V>
        void _fields(V& v) const {
            v.state("_time", _time);
            v.state("_entity_id_source", _entity_id_source);
            v.transient("_ready", _ready);
            v.state("_entity_id_for_coordinate", _entity_id_for_coordinate);
            v.state("_located_for_coordinate", _located_for_coordinate);
            v.state("_entity_for_entity_id", _entity_for_entity_id);
            v.state("_term_for_coordinate", _term_for_coordinate);
            v.state("_terrain_for_coordinate", _terrain_for_coordinate);
            v.state("_waiting_on_time", TimeWheelFold{_waiting_on_time, _ready, _time});
        }

        coroutine::Future<Root<World*>> step() const;

        EntityID generate_entity_id() { return _entity_id_source++; }

    }; // World
        
    inline Time world_get_time(const World* world) {
        assert(world);
        return world->_time;
    }
    
    inline void garbage_collected_shade(const World& self) {
        garbage_collected_shade(&self);
    }

    inline void garbage_collected_scan(const World::TimeWheelFold& x) {
        garbage_collected_scan(x.wheel);
    }

    inline uint64_t content_hash(const World::TimeWheelFold& x) {
        uint64_t sum = x.wheel.hash();
        for (auto [entity_id, _] : x.ready)
            sum += World::Set::hash_of({x.time, entity_id});
        return sum;
    }
    
   
    
} // namespace wry::sim


#endif /* world_hpp */
