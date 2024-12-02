//
//  world.cpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#include "world.hpp"

namespace wry::sim {
    
    struct TransactionSet {
        StableConcurrentMap<EntityID, Atomic<const Transaction::Node*>> _transactions_for_entity;
        StableConcurrentMap<Coordinate, Atomic<const Transaction::Node*>> _transactions_for_coordinate;
    };
    
    PersistentWorld* PersistentWorld::step() const {
        
        Time new_tick = _tick + 1;
        
        TransactionSet transaction_set;
        
        
        // each entity constructs a transaction and links it with all of its
        // accessed variables
        
        _ready.parallel_for_each([this, &transaction_set](EntityID entity_id) {
            const Entity* a;
            bool b = _entity_for_entity_id.get(entity_id, a);
            assert(b);
            a->notify2(this, &transaction_set);
        });
        
        // barrier; all transactions are linked up and ready to be resolved
        
        // recurse into the world and transaction structure by coordinate,
        // and rebuild the new world state from the leaves up; untouched
        // subtrees are structurally shared with the original
        
        auto new_value_for_coordinate
        = parallel_rebuild(_value_for_coordinate,
                           transaction_set._transactions_for_coordinate,
                           [](const std::pair<const Coordinate, Atomic<const Transaction::Node*>>& kv) -> Value {
            // resolve the transactions associated with this coordinate
            const Transaction::Node* head = kv.second.load(Ordering::ACQUIRE);
            for (; head; head = head->_next) {
                head->resolve();
            }
            
            
            
            return Value();
        });
        
        // likewise, recurse into structures by entity_id
        
        auto new_entity_for_entity_id
        = parallel_rebuild(_entity_for_entity_id,
                           transaction_set._transactions_for_entity,
                           [](const std::pair<const EntityID, Atomic<const Transaction::Node*>>& kv) -> const Entity* {
            return nullptr;
        });
        
        
        
        
        
        
        
        
        return new PersistentWorld{
            new_tick,
        };
        
    }
}
