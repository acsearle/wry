//
//  world.cpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#include "transaction.hpp"
#include "world.hpp"

namespace wry {
    
    void World::_garbage_collected_enumerate_fields(TraceContext* context) const {
        // printf("%s\n", __PRETTY_FUNCTION__);
        trace(_entity_id_for_coordinate, context);
        trace(_entity_for_entity_id, context);
        trace(_value_for_coordinate, context);
        trace(_waiting_on_time, context);
    }
        
    World* World::step() const {
        
        TransactionContext context;
        context._world = this;
        Time new_time = _time + 1;
        
        printf("World step %lld\n", _time);
        
        // Take the set of EntityIDs that are ready to run
        
        PersistentSet<EntityID> ready;
        auto new_waiting_on_time = _waiting_on_time;
        (void) new_waiting_on_time.try_erase(_time, ready);
        
        // In parallel, notify each Entity.  Entities will typically examine
        // the World and may propose a Transaction to change it.
                
        // TODO: parallel implementation of parallel for each
        // Note that the Transactions, ConcurrentMaps etc. involved are already
        // thread safe, so all we need is parallel execution and a completion
        // barrier.
        ready.parallel_for_each([this, &context](EntityID entity_id) {
            const Entity* a = nullptr;
            (void) _entity_for_entity_id._map.try_get(entity_id, a);
            assert(a);
            a->notify(&context);
        });
        
        // -- ready.parallel_for_each completion barrier --
        
        // All transactions are now described and ready to be resolved in
        // parallel.
        
        // TODO: parallel implementation of parallel_rebuild
        // The objects involved are thread-aware.  We need parallel execution
        // with continuation.
        
        // TODO: support changing entity_id_for_coordinate
        // TODO: support changing entity_for_entity_id
        auto new_entity_id_for_coordinate = _entity_id_for_coordinate;
        auto new_entity_for_entity_id = _entity_for_entity_id;

        WaitableMap<Coordinate, Value> new_value_for_coordinate;

        
        // TODO: This lambda can be made generic for any exclusive-write
        // key-value store
        
        // Construct the new map by resolving what and if writes occur to each
        // key
        new_value_for_coordinate._map
        = parallel_rebuild(_value_for_coordinate._map,
                           context._write_value_for_coordinate,
                           [this](const std::pair<Coordinate, Atomic<const Transaction::Node*>>& kv)
                           -> ParallelRebuildAction<Value> {
            // Resolve _all_ the transactions associated with this key.  If we
            // don't eagerly resolve the low priority transactions, they might
            // never be resolved at all.
            const Transaction::Node* winner = nullptr;
            for (auto candidate = kv.second.load(Ordering::ACQUIRE);
                 candidate != nullptr;
                 candidate = candidate->_next)
            {
                if (!winner) {
                    if (candidate->resolve() == Transaction::State::COMMITTED)
                        winner = candidate;
                } else {
                    // We already established that another transaction
                    // committed, so this one
                    candidate->abort();
                }
            }
            // Value result = {};
            ParallelRebuildAction<Value> result;
            if (winner) {
                result.tag = ParallelRebuildAction<Value>::WRITE_VALUE;
                // The desired value is type-erased (for now)
                using std::get;
                result.value = get<Value>(winner->_desired);

                // TODO: The value has changed, so we need to notify everybody
                // waiting on the value.  But it is too late to participate in
                // write_waiting_on_time.  Instead we write into an epheremeral
                // object.
                // TODO: This releases a thundering herd; if they all try to write
                // to the location next tick, only one succeeds and the rest sleep.
                
            } else {
                result.tag = ParallelRebuildAction<Value>::NONE;
            }
            return result;
        });
        
        // TODO: This lambda can be made generic for any nonexclusive
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
        
        new_value_for_coordinate._waiting
        = parallel_rebuild(_value_for_coordinate._waiting,
                           context._wait_on_value_for_coordinate,
                           [this](const std::pair<Coordinate, Atomic<const Transaction::Node*>>& kv)
                           -> ParallelRebuildAction<PersistentSet<EntityID>> {
            ParallelRebuildAction<PersistentSet<EntityID>> result;
            result.tag = ParallelRebuildAction<PersistentSet<EntityID>>::WRITE_VALUE;
            _value_for_coordinate._waiting.try_get(kv.first, result.value);
            const Transaction::Node* head = kv.second.load(Ordering::RELAXED);
            for (; head; head = head->_next) {
                using std::get;
                EntityID entity_id = get<EntityID>(head->_desired);
                // State and Condition are bit-compatible
                if (head->resolve() & head->_condition)
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
                if (head->resolve() & head->_condition)
                    result.value.set(entity_id);
            }
            return result;
        });
    

        return new World{
            new_time,
            new_entity_id_for_coordinate,
            new_entity_for_entity_id,
            new_value_for_coordinate,
            new_waiting_on_time
        };
        
    }
}
