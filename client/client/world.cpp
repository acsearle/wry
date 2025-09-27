//
//  world.cpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#include "transaction.hpp"
#include "world.hpp"

namespace wry {
    
    auto
    World::_garbage_collected_scan() const -> void {
        
        // printf("%s\n", __PRETTY_FUNCTION__);
        garbage_collected_scan(_entity_id_for_coordinate);
        garbage_collected_scan(_entity_for_entity_id);
        garbage_collected_scan(_value_for_coordinate);
        garbage_collected_scan(_waiting_on_time);
        
    } // World::_garbage_collected_scan
        
    auto
    World::step() const -> World* {
        
        TransactionContext context{._world = this};
        
        Time new_time = _time + 1;
        
        printf("World step %lld\n", _time);
        
        // Take the set of EntityIDs that are ready to run
        
        PersistentSet<EntityID> ready;
        auto new_waiting_on_time = _waiting_on_time.clone_and_try_erase(_time, ready).first;
        
        PersistentSet<EntityID> next_ready;
        
        // In parallel, notify each Entity.  Entities will typically examine
        // the World and may propose a Transaction to change it.
        
        
        ready.parallel_for_each([this, &context](EntityID entity_id) {
            const Entity* a = nullptr;
            (void) _entity_for_entity_id.valuemap.try_get(entity_id, a);
            assert(a);
            a->notify(&context);
        });
        
        // -- completion barrier --
        
        // All transactions are now described and ready to be resolved in
        // parallel.
        
        auto new_entity_id_for_coordinate = _entity_id_for_coordinate;
        auto new_entity_for_entity_id = _entity_for_entity_id;
            
        // Build the new map from the old map by resolving transactions and
        // implementing the resulting mutations
        
       
        WaitableMap<Coordinate, Value> new_value_for_coordinate;
        
        new_value_for_coordinate.valuemap
        = parallel_rebuild(_value_for_coordinate.valuemap,
                           context._write_value_for_coordinate,
                           [this](const std::pair<Coordinate, Atomic<const Transaction::Node*>>& kv)
                           -> ParallelRebuildAction<Value> {
            const Transaction::Node* winner = nullptr;
            for (auto candidate = kv.second.load(Ordering::ACQUIRE);
                 candidate != nullptr;
                 candidate = candidate->_next)
            {
                if (!winner) {
                    if (candidate->resolve() == Transaction::State::COMMITTED) {
                        winner = candidate;
                        // We continue to eagerly resolve all the transactions
                        // or they may never be resolved at all
                    }
                } else {
                    // We already established that another transaction
                    // committed, so this one must abort
                    candidate->abort();
                }
            }
            ParallelRebuildAction<Value> result;
            if (winner) {
                result.tag = ParallelRebuildAction<Value>::WRITE_VALUE;
                // The desired value is type-erased (for now)
                using std::get;
                result.value = get<Value>(winner->_desired);
            } else {
                result.tag = ParallelRebuildAction<Value>::NONE;
            }
            return result;
        });
        

        
        new_value_for_coordinate.waitset
        = parallel_rebuild(_value_for_coordinate.waitset,
                           context._wait_on_value_for_coordinate,
                           [this, &context](const std::pair<Coordinate, Atomic<const Transaction::Node*>>& kv)
                           -> ParallelRebuildAction<PersistentSet<EntityID>> {
            ParallelRebuildAction<PersistentSet<EntityID>> result;
            result.tag = ParallelRebuildAction<PersistentSet<EntityID>>::WRITE_VALUE;
            _value_for_coordinate.waitset.try_get(kv.first, result.value);
            const Transaction::Node* head = kv.second.load(Ordering::RELAXED);
            for (; head; head = head->_next) {
                using std::get;
                EntityID entity_id = get<EntityID>(head->_desired);
                // State and Condition are bit-compatible
                if (head->resolve() & head->_operation)
                    result.value.set(entity_id);
            }
            return result;
        });

                
        
        new_waiting_on_time
        = parallel_rebuild(new_waiting_on_time,
                           context._wait_on_time,
                           [this](const std::pair<Time, Atomic<const Transaction::Node*>>& kv)
                           -> ParallelRebuildAction<PersistentSet<EntityID>> {
            ParallelRebuildAction<PersistentSet<EntityID>> result;
            result.tag = ParallelRebuildAction<PersistentSet<EntityID>>::WRITE_VALUE;
            _waiting_on_time.try_get(kv.first, result.value);
            const Transaction::Node* head = kv.second.load(Ordering::RELAXED);
            for (; head; head = head->_next) {
                using std::get;
                EntityID entity_id = get<EntityID>(head->_desired);
                // State and Condition are bit-compatible
                if (head->resolve() & head->_operation)
                    result.value.set(entity_id);
            }
            return result;
        });
    
        // -- completion barrier --

        return new World{
            new_time,
            new_entity_id_for_coordinate,
            new_entity_for_entity_id,
            new_value_for_coordinate,
            new_waiting_on_time
        };
        
    } // World::step
    
} // namespace wry



// TODO: can entities meaningfully propose multiple independent
// transactions?  How are they ordered if not by (function of)
// EntityID?
// TODO: support changing entity_id_for_coordinate
// TODO: support changing entity_for_entity_id


// TODO: lambda can be made generic for any nonexclusive
// insert-only key-set store

// TODO: To achieve parallel rebuild, we need to turn this into
// PersistentSet<Pair<Coordinate, EntityID>>.  We can't reasonably
// expect any locality here.  We do need a prefix search on Coordinate,
// so we might need a 128 bit key of hash(Coordinate) cat hash(EntityID).

// TODO: This must be more tightly coupled with writes to the kv store.
// When we write, we want to wake up all previous waiters, and put all
// new waiters into the readylist, except only the writer if it has also
// requested to be woken (i.e. they all wait against the value they
// expect at the end of the cycle).  If this is genuinely complex we can
// also wake the writer immediately because of benign spurious wakeups
//
// The key-value mapping can be dense and large
// The key-waitset mapping is expected to sparse and small

// Thus we don't want to store it as a map of Key -> (Value, Set) because
// the Set storage is almost always wasted

// (We could store Value + indirection Value x Set)

// The value and the waitset change together
// (OR, we record that a change happened and clear/use the waitset in the
// next phase)

// Either way, we want to handle the mutations together.
// This means we have a single modification map for the key that will
// include combinations of write, erase, and wait
