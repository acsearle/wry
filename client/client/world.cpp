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
        
        // printf("World step %lld\n", _time);
        
        // Take the set of EntityIDs that are ready to run
        
        PersistentSet<EntityID> ready;
        auto new_waiting_on_time = _waiting_on_time.clone_and_try_erase(_time, ready).first;
        
        // TODO: next_ready needs to be concurrent
        PersistentSet<EntityID> next_ready;
        
        // In parallel, notify each Entity.  Entities will typically examine
        // the World and may propose a Transaction to change it.
        
        
        ready.parallel_for_each([this, &context](EntityID entity_id) {
            const Entity* a = nullptr;
            (void) _entity_for_entity_id.try_get(entity_id, a);
            assert(a);
            a->notify(&context);
        });
        
        // -- completion barrier --
        
        // All transactions are now described and ready to be resolved in
        // parallel.
                    
        // Build the new map from the old map by resolving transactions and
        // implementing the resulting mutations
        
       
        WaitableMap<Coordinate, Value> new_value_for_coordinate;
        
        new_value_for_coordinate
        = parallel_rebuild(_value_for_coordinate,
                           context._verb_value_for_coordinate,
                           [this, &next_ready](const std::pair<Coordinate, Atomic<const Transaction::Node*>>& kv)
                           -> ParallelRebuildAction<std::pair<Value, PersistentSet<EntityID>>> {
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
            using P = std::pair<Value, PersistentSet<EntityID>>;
            using A = ParallelRebuildAction<std::pair<Value, PersistentSet<EntityID>>>;
            A result{};
            if (writer) {
                assert(writer->_operation & Transaction::Operation::WRITE_ON_COMMIT);
                P b;
                b.first = get<Value>(writer->_desired);
                if (writer->_operation & Transaction::Operation::WAIT_ON_COMMIT) {
                    b.second.set(writer->_parent->_entity->_entity_id);
                }
                result.tag = A::WRITE_VALUE;
                result.value = b;
                // Publish new and old waiters somewhere

                P c{};
                (void) _value_for_coordinate.inner.try_get(kv.first, c);
                c.second.for_each([&next_ready](EntityID key) {
                    next_ready.set(key);
                });
                for (EntityID key : waiters)
                    next_ready.set(key);
                
            } else if (!waiters.empty()) {
                P b{};
                (void) _value_for_coordinate.inner.try_get(kv.first, b);
                // b now represents the old state, which may have been nil
                for (EntityID key : waiters)
                    b.second.set(key);
                result.tag = A::WRITE_VALUE;
                result.value = b;
            }
            return result;
        });
        
        
        WaitableMap<Coordinate, EntityID> new_entity_id_for_coordinate;
        new_entity_id_for_coordinate
        = parallel_rebuild(_entity_id_for_coordinate,
                           context._verb_entity_id_for_coordinate,
                           [this, &next_ready](const std::pair<Coordinate, Atomic<const Transaction::Node*>>& kv)
                           -> ParallelRebuildAction<std::pair<EntityID, PersistentSet<EntityID>>> {
            //printf("Rebuild entity_id_for_coordinate %d %d\n", kv.first.x, kv.first.y);
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
            using P = std::pair<EntityID, PersistentSet<EntityID>>;
            using A = ParallelRebuildAction<std::pair<EntityID, PersistentSet<EntityID>>>;
            A result{};
            if (writer) {
                P b;
                b.first = get<EntityID>(writer->_desired);
                //printf("Writing %lld\n", b.first.data);
                if (writer->_operation & Transaction::Operation::WAIT_ON_COMMIT) {
                    b.second.set(writer->_parent->_entity->_entity_id);
                }
                result.tag = A::WRITE_VALUE;
                result.value = b;
                // Publish new and old waiters somewhere
                
                P c{};
                (void) _entity_id_for_coordinate.inner.try_get(kv.first, c);
                c.second.for_each([&next_ready](EntityID key) {
                    next_ready.set(key);
                });
                for (EntityID key : waiters)
                    next_ready.set(key);
                
            } else if (!waiters.empty()) {
                P b{};
                (void) _entity_id_for_coordinate.inner.try_get(kv.first, b);
                // b now represents the old state, which may have been nil
                for (EntityID key : waiters)
                    b.second.set(key);
                result.tag = A::WRITE_VALUE;
                result.value = b;
            }
            return result;
        });
        
        WaitableMap<EntityID, Entity const*> new_entity_for_entity_id;
        new_entity_for_entity_id
        = parallel_rebuild(_entity_for_entity_id,
                           context._verb_entity_for_entity_id,
                           [this, &next_ready](const std::pair<EntityID, Atomic<const Transaction::Node*>>& kv)
                           -> ParallelRebuildAction<std::pair<Entity const*, PersistentSet<EntityID>>> {
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
            using P = std::pair<Entity const*, PersistentSet<EntityID>>;
            using A = ParallelRebuildAction<P>;
            A result{};
            if (writer) {
                P b;
                b.first = get<Entity const*>(writer->_desired);
                if (writer->_operation & Transaction::Operation::WAIT_ON_COMMIT) {
                    b.second.set(writer->_parent->_entity->_entity_id);
                }
                result.tag = A::WRITE_VALUE;
                result.value = b;
                // Publish new and old waiters somewhere
                
                P c{};
                (void) _entity_for_entity_id.inner.try_get(kv.first, c);
                c.second.for_each([&next_ready](EntityID key) {
                    next_ready.set(key);
                });
                for (EntityID key : waiters)
                    next_ready.set(key);
                
            } else if (!waiters.empty()) {
                P b{};
                (void) _entity_for_entity_id.inner.try_get(kv.first, b);
                // b now represents the old state, which may have been nil
                for (EntityID key : waiters)
                    b.second.set(key);
                result.tag = A::WRITE_VALUE;
                result.value = b;
            }
            return result;
        });
        
        new_waiting_on_time
        = parallel_rebuild(new_waiting_on_time,
                           context._wait_on_time,
                           [&new_waiting_on_time, this](const std::pair<Time, Atomic<const Transaction::Node*>>& kv)
                           -> ParallelRebuildAction<PersistentSet<EntityID>> {
            // TODO: We need to special-case waits for new_time
            assert(kv.first > _time);
            ParallelRebuildAction<PersistentSet<EntityID>> result;
            result.tag = ParallelRebuildAction<PersistentSet<EntityID>>::WRITE_VALUE;
            new_waiting_on_time.try_get(kv.first, result.value);
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
        
        // HACK: We have two separate representations of work for next_time,
        // we need to merge them and expose them to the next cycle.  This hack
        // is serial and wasteful; we should direct write all these things into
        // a concurrent-map-ified next_ready and hold it over to the next step
        {
            PersistentSet<EntityID> v{};
            (void) new_waiting_on_time.try_get(new_time, v);
            next_ready.for_each([&v](EntityID key) {
                v.set(key);
            });
            new_waiting_on_time.set(new_time, v);
        }
        
        

        
        
    
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
