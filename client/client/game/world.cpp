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
        garbage_collected_scan(_ready);
        garbage_collected_scan(_entity_id_for_coordinate);
        garbage_collected_scan(_located_for_coordinate);
        garbage_collected_scan(_entity_for_entity_id);
        garbage_collected_scan(_term_for_coordinate);
        garbage_collected_scan(_terrain_for_coordinate);
        garbage_collected_scan(_waiting_on_time);

    } // World::_garbage_collected_scan

    /*
    template<typename Key, typename H>
    struct AwaitablePersistentSet {
        PersistentSet<Key, H> _inner;
        Coroutine::Mutex _mutex;
        Coroutine::Task set(Key key) {
            auto guard{co_await _mutex};
            _inner.set(key);
        }
    };
     */

    void World::hack_repair_invariant() {
        std::pair<Time, wry::EntityID> victim;
        if (_waiting_on_time.try_front(victim)) {
            assert(victim.first >= _time);
            if (victim.first == _time) {

                // TODO: Require the arguments to already be partitioned

                // HACK: We've been given a waiting_on_time that includes
                // elements that should be in _ready.

                Set waiting_on_now;
                std::tie(waiting_on_now, _waiting_on_time) = partition_first(_waiting_on_time, _time);

                // HACK: If the world is in a bad state from being manually
                // constructed, the ready set should be empty
                assert(_ready.is_empty());

                ConcurrentSkiplistSet<ReadyKey, ReadyKeyCompare, ScanDiscipline> mut_ready;

                // Copy the EntityIDs waiting on now to the _ready skiplist
                waiting_on_now.for_each([this, &mut_ready] (std::pair<Time, EntityID> x) {
                    assert(x.first == _time);
                    (void) mut_ready.try_emplace(x.second);
                });

                _ready = FrozenSkiplistSet<ReadyKey, ReadyKeyCompare, ScanDiscipline>(std::move(mut_ready));

                // HACK: _ready is now populated, _waiting_on_time is now pruned

            }
        }
    }

    // Fused dispatch-and-accumulate over the frozen ready set.
    //
    // A frozen skiplist is the left-child/right-sibling encoding of a tree:
    // each node is discovered exactly once, at its own top level, by the
    // frame whose interval [self, bound) contains it.  One frame per node
    // (and one for the head): spawn a child frame for each successor in the
    // tower, top level first (each successor tightens the bound for the
    // levels below it), notify our own entity while the children run, join,
    // then prefix-sum.  With successors laid out so that a HIGHER level
    // covers a LATER key interval, the level-i child's subtree is preceded
    // within this frame by self plus the children at levels below i -- that
    // is the value written into its `n`.  The frame returns its subtree
    // total; the head frame's return is the tick's total, which advances the
    // World's EntityID cursor.
    //
    // `n` and `requested` are written exactly once, by the parent frame
    // (n) and the owning frame (requested), and are complete only once the
    // notify nursery in step() has joined; try_lookup_cumulant reads them
    // in the rebuild, on the far side of that barrier.  Two frames never
    // touch the same field.
    //
    // The lookup that consumes `n` adds it for every node it ENTERS on the
    // way down, so the head needs none.  The head frame is the same code
    // with a null entity: no self-notify, weight zero.
    using ReadyNode = _skiplist_detail::Node<ReadyKey, ReadyKeyCompare, ScanDiscipline>;
    using Next = ReadyNode::AtomicSlot<ReadyNode* _Nullable>;
    [[nodiscard]] Coroutine::Future<int64_t> notify_and_accumulate(Next const* _Nonnull self_next,
                                                                   size_t self_levels,
                                                                   ReadyNode const* _Nullable self,
                                                                   ReadyNode const* _Nullable bound,
                                                                   TransactionContext* context) {
        Coroutine::Nursery nursery;

        // results[0] is our own weight; results[i + 1] receives the level-i
        // child's subtree total (or stays zero for a level with no child).
        // TODO: memory waste; worst case is much bigger than likely cases
        assert(self_levels <= 64);
        int64_t results[65] = {};
        ReadyNode const* bound2 = bound;
        for (size_t i = self_levels; i--;) {
            ReadyNode const* child = self_next[i].nonatomic_load();
            if (child != bound2) {
                assert(child);
                // TODO: fork or soon?
                nursery.soon(results[i + 1],
                             notify_and_accumulate(child->_next, child->_size, child, bound2, context));
                bound2 = child;
            }
        }
        // Our own contribution, while the children run.  The head has no
        // entity and contributes nothing.
        if (self) {
            Entity const* entity = nullptr;
            bool flag = context->try_read_entity_for_entity_id(self->_key.id, entity);
            assert(flag && entity);
            results[0] = entity->notify(context);
            self->_key.requested = results[0];
        }
        // Wait for results
        co_await nursery.join();
        // Accumulate
        for (size_t i = 0; i != self_levels; ++i) {
            results[i + 1] += results[i];
        }
        // Write back if bounds permit
        bound2 = bound;
        for (size_t i = self_levels; i--;) {
            ReadyNode const* child = self_next[i].nonatomic_load();
            if (child != bound2) {
                assert(child);
                child->_key.n = results[i];
                bound2 = child;
            }
        }
        // Kick the total up to the next level
        co_return results[self_levels];
    }

    [[nodiscard]] bool try_lookup_cumulant(FrozenSkiplistSet<ReadyKey, ReadyKeyCompare, ScanDiscipline> const& ready,
                                           EntityID id,
                                           int64_t& victim) {
        int64_t n = 0;
        size_t i = ready._head->_top.load_relaxed() - 1;
        assert((i + 1) > 0);
        auto left = ready._head->_next + i;
        for (;;) {
            ReadyNode* _Nullable candidate = left->nonatomic_load();
            if (!candidate || (id < candidate->_key.id)) {
                // Descend a level
                if (i == 0) {
                    // Looked up an EntityID that wasn't ready, such as when
                    // one Entity creates another
                    return false;
                }
                --i;
                --left;
            } else if (candidate->_key.id < id) {
                // Move right: we enter candidate's interval, so add its
                // prefix-within-frame; the target lies inside it.
                left = candidate->_next + i;
                assert(candidate->_key.n >= 0);
                n += candidate->_key.n;
            } else {
                // Found: we enter the target too, and its n is the last
                // term -- its own prefix within the frame we reached it
                // from.  The entered nodes' n telescope to the absolute
                // exclusive prefix.
                assert(candidate->_key.n >= 0);
                n += candidate->_key.n;
                victim = n;
                assert(candidate->_key.requested >= 0);
                return candidate->_key.requested;
            }
        }
    }

    Coroutine::Future<Root<World*>> World::step() const {
#ifndef NDEBUG
        {
            std::pair<Time, wry::EntityID> x;
            if (_waiting_on_time.try_front(x)) {
                assert(x.first > _time);
            }
        }
#endif // NDEBUG

        TransactionContext context{._world = this};
        
        Time next_time = _time + 1;

        // printf("World step %lld\n", _time);
        // _waiting_on_time.for_each([](auto const& p){
        //     printf("EntityID %lld is waiting for time %lld\n", p.second.data, p.first);
        //  });

        // Immutable:
        // this->_ready contains all EntityIDs to notify at this->_time
        // this->_waiting_on_time contains all EntityIDs to notify after this->_time

        auto [waiting_on_next_time, next_waiting_on_time] = partition_first(_waiting_on_time, next_time);
        ConcurrentSkiplistSet<ReadyKey, ReadyKeyCompare, ScanDiscipline> next_ready;

        // Mutable:
        // waiting_on_next_time contains all EntityIDs to notify at next_time
        // next_waiting_on_time contains all EntityIDs to notify after next time
        // next_ready _will_ contain all EntityIDs to notify at next_time

        // TODO: We don't need to create waiting_on_next_time, we just need a
        // masked for_each on _waiting_on_time

        int64_t entity_id_requests = 0;
        {
            Coroutine::Nursery nursery;

            // For each EntityID ready now, look up the Entity and notify it.
            // On notification, entities will typically examine the World and
            // may propose a Transaction to change it, and may request some
            // number of new EntityIDs; the traversal augments the ready set
            // with the cumulant of those requests (see notify_and_accumulate).
            // TODO: Rummaging in the skiplist implementation
            assert(_ready._head);
            co_await nursery.fork(entity_id_requests,
                                  notify_and_accumulate(_ready._head->_next,
                                                        _ready._head->_top.nonatomic_load(),
                                                        nullptr,   // head: no entity
                                                        nullptr,   // bound: +infinity
                                                        &context));

            // For each EntityID ready next_time, copy it into next_ready
            co_await nursery.fork(waiting_on_next_time
                                  .coroutine_parallel_for_each([next_time, &next_ready](std::pair<Time, EntityID> kv) {
                assert(kv.first == next_time);
                next_ready.try_emplace(kv.second);
            }));

            co_await nursery.join();
        }

        // All transactions are now described and ready to be resolved in
        // parallel.
                    
        // Build the new map from the old map by resolving transactions and
        // implementing the resulting mutations

        WaitableMap<Coordinate, Term> new_value_for_coordinate;
        WaitableMap<Coordinate, EntityID> new_entity_id_for_coordinate;
        WaitableMap<Coordinate, WaitSet> new_located_for_coordinate;
        WaitableMap<EntityID, Entity const*> new_entity_for_entity_id;

                
        auto value_for_coordinate_action
        = [this, &next_ready]
        (const std::pair<Coordinate, Atomic<const Transaction::Node*>>& kv)
        -> Coroutine::Future<std::pair<ParallelRebuildAction<Term>, ParallelRebuildAction<std::vector<EntityID>>>> {
            
            using A = std::pair<ParallelRebuildAction<Term>, ParallelRebuildAction<std::vector<EntityID>>>;
            
            A result = {};
            const Transaction::Node* writer = nullptr;
            std::vector<EntityID> waiters;
            
            for (auto candidate = kv.second.load_acquire();
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
                result.first.value = get<Term>(writer->_desired);
                result.first.tag = ParallelRebuildAction<Term>::WRITE_VALUE;
                if (writer->_operation & Transaction::Operation::WAIT_ON_COMMIT) {
                    result.second.value.push_back(writer->_parent->_entity->_entity_id);
                    result.second.tag = ParallelRebuildAction<std::vector<EntityID>>::WRITE_VALUE;
                } else {
                    result.second.tag = ParallelRebuildAction<std::vector<EntityID>>::CLEAR_VALUE;
                }
                {
                    WaitSet ws;
                    if (_term_for_coordinate.ki.try_get(kv.first, ws))
                        ws.for_each([&next_ready](EntityID waiter) {
                            next_ready.try_emplace(waiter);
                        });
                }
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
            
            for (auto candidate = kv.second.load_acquire();
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
                {
                    WaitSet ws;
                    if (_entity_id_for_coordinate.ki.try_get(kv.first, ws))
                        ws.for_each([&next_ready](EntityID waiter) {
                            next_ready.try_emplace(waiter);
                        });
                }
                for (EntityID key : waiters) {
                    next_ready.try_emplace(key);
                }
                
            } else if (!waiters.empty()) {
                result.second.value = std::move(waiters);
                result.second.tag = ParallelRebuildAction<std::vector<EntityID>>::MERGE_VALUE;
            }
            co_return result;
        };
        
        
        auto action_for_located_for_coordinate
        = [this, &next_ready]
        (const std::pair<Coordinate, Atomic<const Transaction::Node*>>& kv)
        -> Coroutine::Future<std::pair<ParallelRebuildAction<WaitSet>, ParallelRebuildAction<std::vector<EntityID>>>> {

            using A = std::pair<ParallelRebuildAction<WaitSet>, ParallelRebuildAction<std::vector<EntityID>>>;

            A result = {};
            const Transaction::Node* writer = nullptr;
            std::vector<EntityID> waiters;

            for (auto candidate = kv.second.load_acquire();
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
                result.first.value = get<WaitSet>(writer->_desired);
                result.first.tag = ParallelRebuildAction<WaitSet>::WRITE_VALUE;
                if (writer->_operation & Transaction::Operation::WAIT_ON_COMMIT) {
                    result.second.value.push_back(writer->_parent->_entity->_entity_id);
                    result.second.tag = ParallelRebuildAction<std::vector<EntityID>>::WRITE_VALUE;
                } else {
                    result.second.tag = ParallelRebuildAction<std::vector<EntityID>>::CLEAR_VALUE;
                }
                {
                    WaitSet ws;
                    if (_located_for_coordinate.ki.try_get(kv.first, ws))
                        ws.for_each([&next_ready](EntityID waiter) {
                            next_ready.try_emplace(waiter);
                        });
                }
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
            
            for (auto candidate = kv.second.load_acquire();
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
                // Deliver a requested EntityID block to the requester's
                // installed successor: base = this tick's cursor plus the
                // requester's exclusive prefix over the ready set.  The
                // lookup is false for keys not in the ready set (an entity
                // installed by another's transaction, e.g. a spawn) and for
                // ready entities that requested nothing, so a held ticket is
                // never clobbered.  Single writer: this leaf action is the
                // unique committer of kv.first, and the successor is not yet
                // published.
                int64_t cumulant = 0;
                if (try_lookup_cumulant(_ready, kv.first, cumulant)) {
                    result.first.value->_free_entity_id = _entity_id_source + cumulant;
                }
                {
                    WaitSet ws;
                    if (_entity_for_entity_id.ki.try_get(kv.first, ws))
                        ws.for_each([&next_ready](EntityID waiter) {
                            next_ready.try_emplace(waiter);
                        });
                }
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
        = [next_time, &next_ready]
        (const std::pair<Time, Atomic<const Transaction::Node*>>& kv)
        -> Coroutine::Future<ParallelRebuildAction<Set>> {
            ParallelRebuildAction<Set> result{};
            if (kv.first == next_time) {
                result.tag = ParallelRebuildAction<Set>::NONE;
                const Transaction::Node* head = kv.second.load_relaxed();
                for (; head; head = head->_next) {
                    using std::get;
                    EntityID entity_id = get<EntityID>(head->_desired);
                    // State and Condition are bit-compatible
                    if (head->resolve() & head->_operation) {
                        next_ready.try_emplace(entity_id);
                    }
                }
            } else {
                assert(kv.first > next_time);
                result.tag = ParallelRebuildAction<Set>::MERGE_VALUE;
                // new_waiting_on_time.try_get(kv.first, result.value);
                const Transaction::Node* head = kv.second.load_relaxed();
                for (; head; head = head->_next) {
                    using std::get;
                    EntityID entity_id = get<EntityID>(head->_desired);
                    // State and Condition are bit-compatible
                    if (head->resolve() & head->_operation)
                        result.value.set({kv.first, entity_id});
                }
            }
            co_return result;
        };
        
        Coroutine::Nursery nursery;
        
        co_await nursery.fork(new_value_for_coordinate,
                              coroutine_parallel_rebuild2_unified(_term_for_coordinate,
                                                         context._verb_value_for_coordinate,
                                                         value_for_coordinate_action));
        
        co_await nursery.fork(new_entity_id_for_coordinate,
                              coroutine_parallel_rebuild2_unified(_entity_id_for_coordinate,
                                                         context._verb_entity_id_for_coordinate,
                                                         action_for_entity_id_for_coordinate));

        co_await nursery.fork(new_located_for_coordinate,
                              coroutine_parallel_rebuild2_unified(_located_for_coordinate,
                                                         context._verb_located_for_coordinate,
                                                         action_for_located_for_coordinate));

        co_await nursery.fork(new_entity_for_entity_id,
                              coroutine_parallel_rebuild2_unified(_entity_for_entity_id,
                                                         context._verb_entity_for_entity_id,
                                                         action_for_entity_for_entity_id));
        
        co_await nursery.fork(next_waiting_on_time,
                              coroutine_parallel_rebuild(next_waiting_on_time,
                                                         context._wait_on_time,
                                                         action_for_waiting_on_time));

        co_await nursery.join();

        // -- completion barrier --

        // Terrain has no transaction channel yet; the persistent map is
        // carried over unchanged (an O(1) structural share, not a copy).

        co_return new World{
            next_time,
            _entity_id_source + entity_id_requests,
            FrozenSkiplistSet<ReadyKey, ReadyKeyCompare, ScanDiscipline>(std::move(next_ready)),
            new_entity_id_for_coordinate,
            new_located_for_coordinate,
            new_entity_for_entity_id,
            new_value_for_coordinate,
            _terrain_for_coordinate,
            next_waiting_on_time
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

// Thus we don't want to store it as a map of Key -> (Term, Set) because
// the Set storage is almost always wasted

// (We could store Term + indirection Term x Set)

// The value and the waitset change together
// (OR, we record that a change happened and clear/use the waitset in the
// next phase)

// Either way, we want to handle the mutations together.
// This means we have a single modification map for the key that will
// include combinations of write, erase, and wait
