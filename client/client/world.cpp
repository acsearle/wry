//
//  world.cpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#include "transaction.hpp"
#include "world.hpp"

namespace wry {
    
    void World::_garbage_collected_scan() const {
        // printf("%s\n", __PRETTY_FUNCTION__);
        garbage_collected_scan(_entity_id_for_coordinate);
        garbage_collected_scan(_entity_for_entity_id);
        garbage_collected_scan(_value_for_coordinate);
        garbage_collected_scan(_waiting_on_time);
        
    } // World::_garbage_collected_scan
    
    template<typename Key, typename H>
    struct AwaitablePersistentSet {
        PersistentSet<Key, H> _inner;
        Coroutine::Mutex _mutex;
        Coroutine::Task set(Key key) {
            auto guard{co_await _mutex};
            _inner.set(key);
        }
    };
            
    Coroutine::Future<World*> World::step() const {
        
        TransactionContext context{._world = this};
        
        Time new_time = _time + 1;
        
        
        // Debug
        printf("World step %lld\n", _time);
        _waiting_on_time.for_each([](auto const& p){
            printf("EntityID %lld is waiting for time %lld\n", p.second.data, p.first);
        });
        
        // Take the set of EntityIDs that are ready to run
        
        PersistentSet<std::pair<Time, EntityID>> ready ;
        PersistentSet<std::pair<Time, EntityID>> new_waiting_on_time;
        std::tie(ready, new_waiting_on_time) = partition_first(_waiting_on_time, _time);
        ConcurrentSkiplistSet<EntityID> next_ready;

        // In parallel, notify each Entity.  Entities will typically examine
        // the World and may propose a Transaction to change it.

        auto action_for_ready = [this, &context](std::pair<Time, EntityID> kv) {
            const Entity* a = nullptr;
            (void) _entity_for_entity_id.try_get(kv.second, a);
            assert(a);
            a->notify(&context);
        };
        co_await ready.coroutine_parallel_for_each(action_for_ready);
        
        // All transactions are now described and ready to be resolved in
        // parallel.
                    
        // Build the new map from the old map by resolving transactions and
        // implementing the resulting mutations
        
       
        WaitableMap<Coordinate, Value> new_value_for_coordinate;
        WaitableMap<Coordinate, EntityID> new_entity_id_for_coordinate;
        WaitableMap<EntityID, Entity const*> new_entity_for_entity_id;

                
        auto value_for_coordinate_action
        = [this, &next_ready]
        (const std::pair<Coordinate, Atomic<const Transaction::Node*>>& kv)
        -> Coroutine::Future<std::pair<ParallelRebuildAction<Value>, ParallelRebuildAction<std::vector<EntityID>>>> {
            
            using A = std::pair<ParallelRebuildAction<Value>, ParallelRebuildAction<std::vector<EntityID>>>;
            
            A result = {};
            const Transaction::Node* writer = nullptr;
            std::vector<EntityID> waiters;
            
            for (auto candidate = kv.second.load(Ordering::ACQUIRE);
                 candidate != nullptr;
                 candidate = candidate->_next)
            {
                Transaction::State resolution = candidate->resolve();
                if ((resolution == Transaction::State::COMMITTED)
                    && (candidate->_operation & Transaction::Operation::WRITE_ON_COMMIT))
                {
                    assert(!writer);
                    writer = candidate;
                } else if (candidate->_operation & resolution) {
                    waiters.push_back(candidate->_parent->_entity->_entity_id);
                }
            }
            
            if (writer) {
                assert(writer->_operation & Transaction::Operation::WRITE_ON_COMMIT);
                result.first.value = get<Value>(writer->_desired);
                result.first.tag = ParallelRebuildAction<Value>::WRITE_VALUE;
                if (writer->_operation & Transaction::Operation::WAIT_ON_COMMIT) {
                    result.second.value.push_back(writer->_parent->_entity->_entity_id);
                    result.second.tag = ParallelRebuildAction<std::vector<EntityID>>::WRITE_VALUE;
                } else {
                    result.second.tag = ParallelRebuildAction<std::vector<EntityID>>::CLEAR_VALUE;
                }
                for_each_if_first(_value_for_coordinate.ki,
                                  kv.first,
                                  [coordinate=kv.first, &next_ready](std::pair<Coordinate, EntityID> key) {
                    assert(key.first == coordinate);
                    next_ready.try_emplace(key.second);
                });
                for (EntityID key : waiters) {
                    next_ready.try_emplace(key);
                }
                
            } else if (!waiters.empty()) {
                result.second.value = std::move(waiters);
                result.second.tag = ParallelRebuildAction<std::vector<EntityID>>::MERGE_VALUE;
            }
            co_return result;
        };
                
        
        auto action_for_entity_id_for_coordinate
        = [this, &next_ready]
        (const std::pair<Coordinate, Atomic<const Transaction::Node*>>& kv)
        -> Coroutine::Future<std::pair<ParallelRebuildAction<EntityID>, ParallelRebuildAction<std::vector<EntityID>>>> {
            
            using A = std::pair<ParallelRebuildAction<EntityID>, ParallelRebuildAction<std::vector<EntityID>>>;
            
            A result = {};
            const Transaction::Node* writer = nullptr;
            std::vector<EntityID> waiters;
            
            for (auto candidate = kv.second.load(Ordering::ACQUIRE);
                 candidate != nullptr;
                 candidate = candidate->_next)
            {
                Transaction::State resolution = candidate->resolve();
                if ((resolution == Transaction::State::COMMITTED)
                    && (candidate->_operation & Transaction::Operation::WRITE_ON_COMMIT))
                {
                    assert(!writer);
                    writer = candidate;
                } else if (candidate->_operation & resolution) {
                    waiters.push_back(candidate->_parent->_entity->_entity_id);
                }
            }
            
            if (writer) {
                assert(writer->_operation & Transaction::Operation::WRITE_ON_COMMIT);
                result.first.value = get<EntityID>(writer->_desired);
                result.first.tag = ParallelRebuildAction<EntityID>::WRITE_VALUE;
                if (writer->_operation & Transaction::Operation::WAIT_ON_COMMIT) {
                    result.second.value.push_back(writer->_parent->_entity->_entity_id);
                    result.second.tag = ParallelRebuildAction<std::vector<EntityID>>::WRITE_VALUE;
                } else {
                    result.second.tag = ParallelRebuildAction<std::vector<EntityID>>::CLEAR_VALUE;
                }
                for_each_if_first(_entity_id_for_coordinate.ki,
                                  kv.first,
                                  [coordinate=kv.first, &next_ready](std::pair<Coordinate, EntityID> key) {
                    assert(key.first == coordinate);
                    next_ready.try_emplace(key.second);
                });
                for (EntityID key : waiters) {
                    next_ready.try_emplace(key);
                }
                
            } else if (!waiters.empty()) {
                result.second.value = std::move(waiters);
                result.second.tag = ParallelRebuildAction<std::vector<EntityID>>::MERGE_VALUE;
            }
            co_return result;
        };
        
        
        auto action_for_entity_for_entity_id
        = [this, &next_ready]
        (const std::pair<EntityID, Atomic<const Transaction::Node*>>& kv)
        -> Coroutine::Future<std::pair<ParallelRebuildAction<Entity const*>, ParallelRebuildAction<std::vector<EntityID>>>> {
            
            using A = std::pair<ParallelRebuildAction<Entity const*>, ParallelRebuildAction<std::vector<EntityID>>>;
            
            A result = {};
            const Transaction::Node* writer = nullptr;
            std::vector<EntityID> waiters;
            
            for (auto candidate = kv.second.load(Ordering::ACQUIRE);
                 candidate != nullptr;
                 candidate = candidate->_next)
            {
                Transaction::State resolution = candidate->resolve();
                if ((resolution == Transaction::State::COMMITTED)
                    && (candidate->_operation & Transaction::Operation::WRITE_ON_COMMIT))
                {
                    assert(!writer);
                    writer = candidate;
                } else if (candidate->_operation & resolution) {
                    waiters.push_back(candidate->_parent->_entity->_entity_id);
                }
            }
            
            if (writer) {
                assert(writer->_operation & Transaction::Operation::WRITE_ON_COMMIT);
                result.first.value = get<Entity const*>(writer->_desired);
                result.first.tag = ParallelRebuildAction<Entity const*>::WRITE_VALUE;
                if (writer->_operation & Transaction::Operation::WAIT_ON_COMMIT) {
                    result.second.value.push_back(writer->_parent->_entity->_entity_id);
                    result.second.tag = ParallelRebuildAction<std::vector<EntityID>>::WRITE_VALUE;
                } else {
                    result.second.tag = ParallelRebuildAction<std::vector<EntityID>>::CLEAR_VALUE;
                }
                for_each_if_first(_entity_for_entity_id.ki,
                                  kv.first,
                                  [coordinate=kv.first, &next_ready](std::pair<EntityID, EntityID> key) {
                    assert(key.first == coordinate);
                    next_ready.try_emplace(key.second);
                });
                for (EntityID key : waiters) {
                    next_ready.try_emplace(key);
                }
                
            } else if (!waiters.empty()) {
                result.second.value = std::move(waiters);
                result.second.tag = ParallelRebuildAction<std::vector<EntityID>>::MERGE_VALUE;
            }
            co_return result;
        };
        
        auto action_for_waiting_on_time
        = [new_waiting_on_time, this]
        (const std::pair<Time, Atomic<const Transaction::Node*>>& kv)
        -> Coroutine::Future<ParallelRebuildAction<PersistentSet<std::pair<Time, EntityID>>>> {
            // TODO: We need to special-case waits for new_time
            assert(kv.first > _time);
            ParallelRebuildAction<PersistentSet<std::pair<Time, EntityID>>> result{};
            result.tag = ParallelRebuildAction<PersistentSet<std::pair<Time, EntityID>>>::MERGE_VALUE;
            // new_waiting_on_time.try_get(kv.first, result.value);
            const Transaction::Node* head = kv.second.load(Ordering::RELAXED);
            for (; head; head = head->_next) {
                using std::get;
                EntityID entity_id = get<EntityID>(head->_desired);
                // State and Condition are bit-compatible
                if (head->resolve() & head->_operation)
                    result.value.set({kv.first, entity_id});
            }
            co_return result;
        };
        
        Coroutine::Nursery nursery;
        
        co_await nursery.fork(new_value_for_coordinate,
                              coroutine_parallel_rebuild2(_value_for_coordinate,
                                                         context._verb_value_for_coordinate,
                                                         value_for_coordinate_action));
        
        co_await nursery.fork(new_entity_id_for_coordinate,
                              coroutine_parallel_rebuild2(_entity_id_for_coordinate,
                                                         context._verb_entity_id_for_coordinate,
                                                         action_for_entity_id_for_coordinate));
        
        co_await nursery.fork(new_entity_for_entity_id,
                              coroutine_parallel_rebuild2(_entity_for_entity_id,
                                                         context._verb_entity_for_entity_id,
                                                         action_for_entity_for_entity_id));
        
        mutator_overwrote(new_waiting_on_time._inner);
        co_await nursery.fork(new_waiting_on_time,
                              coroutine_parallel_rebuild(new_waiting_on_time,
                                                         context._wait_on_time,
                                                         action_for_waiting_on_time));
        
        
        // TODO: Unlike the overwrites above, we have multiple winners and we
        // want to merge them into the target value
                
                
        co_await nursery.join();
        
        // HACK: We have two separate representations of work for next_time,
        // we need to merge them and expose them to the next cycle.  This hack
        // is serial and wasteful; we should direct write all these things into
        // a concurrent-map-ified next_ready and hold it over to the next step
        {
            // PersistentSet<EntityID> v{};
            // (void) new_waiting_on_time.try_get(new_time, v);
            for (auto key : next_ready) {
                // v.set(key);
                new_waiting_on_time.set({new_time, key});
            }
            // new_waiting_on_time.set(new_time, v);
        }
        
        

        // -- completion barrier --
        co_return new World{
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
