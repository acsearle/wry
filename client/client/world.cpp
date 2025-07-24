//
//  world.cpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#include "world.hpp"
#include "context.hpp"

namespace wry::sim {
    
    void World::_garbage_collected_enumerate_fields(TraceContext* context) const {
        // printf("%s\n", __PRETTY_FUNCTION__);
        trace(_entity_id_for_coordinate, context);
        trace(_entity_for_entity_id, context);
        trace(_value_for_coordinate, context);
        trace(_ready, context);
        trace(_waiting_on_time, context);
    }
        
    World* World::step() const {
        
        TransactionContext context;
        context._world = this;

        // each entity constructs a transaction and links it with all of its
        // accessed variables
        
        printf("World step %lld\n", _time);
        _ready.parallel_for_each([this, &context](EntityID entity_id) {
            const Entity* a = nullptr;
            bool b = _entity_for_entity_id._map.try_get(entity_id, a);
            assert(b);
            a->notify(&context);
        });
        
        // barrier; all transactions are linked up and ready to be resolved
        
        // recurse into the world and transaction structure by coordinate,
        // and rebuild the new world state from the leaves up; untouched
        // subtrees are structurally shared with the original
        
        Time new_time = _time + 1;
        
        auto new_entity_id_for_coordinate = _entity_id_for_coordinate;
        
        auto new_entity_for_entity_id = _entity_for_entity_id;

        auto new_ready = _ready;
        auto new_waiting_on_time = _waiting_on_time;

        decltype(_value_for_coordinate) new_value_for_coordinate;
        
        new_value_for_coordinate._waiting = parallel_rebuild(_value_for_coordinate._waiting,
                           context._wait_on_value_for_coordinate,
                           [this](const std::pair<Coordinate, Atomic<const Transaction::Node*>>& kv) -> PersistentSet<EntityID> {
            PersistentSet<EntityID> result;
            _value_for_coordinate._waiting.try_get(kv.first, result);
            const Transaction::Node* head = kv.second.load(Ordering::RELAXED);
            for (; head; head = head->_next) {
                using std::get;
                EntityID entity_id = get<EntityID>(head->_desired);
                switch (head->resolve()) {
                    case Transaction::State::INITIAL:
                        abort();
                    case Transaction::State::COMMITTED:
                        if (head->_condition & Transaction::Condition::ON_COMMIT)
                            result.set(entity_id);
                        break;
                    case Transaction::State::ABORTED:
                        if (head->_condition & Transaction::Condition::ON_ABORT)
                            result.set(entity_id);
                        break;
                }
            }
            return result;
        });

        new_value_for_coordinate._map
        = parallel_rebuild(_value_for_coordinate._map,
                           context._write_value_for_coordinate,
                           [this](const std::pair<Coordinate, Atomic<const Transaction::Node*>>& kv) -> Value {
            // resolve the transactions associated with this coordinate
            const Transaction::Node* head = kv.second.load(Ordering::ACQUIRE);
            const Transaction::Node* winner = nullptr;
            for (; head; head = head->_next) {
                if (!winner) {
                    if (head->resolve() == Transaction::State::COMMITTED) {
                        winner = head;
                    }
                } else {
                    // We know who gets to write to this coordinate, but
                    // resolving that did not necessarily resolve all of the
                    // transactions on this coordinate.  If we don't
                    // proactively abort them, they might never be resolved.
                    head->abort();
                }
            }
            Value result = {};
            if (winner) {
                // The desired value is type-erased (for now)
                using std::get;
                result = get<Value>(winner->_desired);
            } else {
                // If there was no winner, all transactions on this location got
                // aborted by conflicts at other locations
                
                // TODO: change the interface so we can support no-action
                (void) _value_for_coordinate._map.try_get(kv.first, result);
            }
            return result;
        });
                
    

        return new World{
            new_time,
            new_entity_id_for_coordinate,
            new_entity_for_entity_id,
            new_value_for_coordinate,
            new_ready,
            new_waiting_on_time
        };
        
    }
}
