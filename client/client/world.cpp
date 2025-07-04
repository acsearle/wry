//
//  world.cpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#include "world.hpp"
#include "context.hpp"

namespace wry::sim {
    
    void World::_garbage_collected_scan() const {
        printf("%s\n", __PRETTY_FUNCTION__);
        adl::trace(_entity_for_entity_id);
        adl::trace(_ready);
        adl::trace(_waiting_for_time);
        adl::trace(_waiting_for_entity_id);
        adl::trace(_waiting_for_coordinate);
        adl::trace(_value_for_coordinate);
        adl::trace(_entity_id_for_coordinate);
    }
        
    World* World::step() const {
        
        Context context;

        // each entity constructs a transaction and links it with all of its
        // accessed variables
        
        printf("_ready: %zd\n", _ready ? _ready->data.size() : 0);
        _ready->parallel_for_each([this, &context](EntityID entity_id) {
            printf("EntityID %lld\n", entity_id.data);
            const Entity* a = nullptr;
            bool b = _entity_for_entity_id->try_get(entity_id, a);
            assert(b);
            a->notify(&context);
        });
        
        // barrier; all transactions are linked up and ready to be resolved
        
        // recurse into the world and transaction structure by coordinate,
        // and rebuild the new world state from the leaves up; untouched
        // subtrees are structurally shared with the original
        
        Time new_tick = _tick + 1;
        
        auto new_entity_for_entity_id
        = parallel_rebuild(_entity_for_entity_id,
                           context._transactions_for_entity,
                           [](const std::pair<const EntityID, Atomic<const Transaction::Node*>>& kv) -> const Entity* {
            return nullptr;
        });
        
        auto new_ready = _ready;
        auto new_waiting_for_time = _waiting_for_time;
        auto new_waiting_for_entity_id = _waiting_for_entity_id;
        auto new_waiting_for_coordinate = _waiting_for_coordinate;

        auto new_value_for_coordinate
        = parallel_rebuild(_value_for_coordinate,
                           context._transactions_for_coordinate,
                           [this](const std::pair<const Coordinate, Atomic<const Transaction::Node*>>& kv) -> Value {
            // resolve the transactions associated with this coordinate
            const Transaction::Node* head = kv.second.load(Ordering::ACQUIRE);
            for (; head; head = head->_next) {
                head->resolve();
            }
            Value v;
            _value_for_coordinate->try_get(kv.first, v);
            return v;
        });

        auto new_entity_id_for_coordinate = _entity_id_for_coordinate;

        return new World{
            new_tick,
            new_entity_for_entity_id,
            new_ready,
            new_waiting_for_time,
            new_waiting_for_entity_id,
            new_waiting_for_coordinate,
            new_value_for_coordinate,
            new_entity_id_for_coordinate,
        };
        
    }
}
