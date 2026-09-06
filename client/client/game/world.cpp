//
//  world.cpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#include <map>

#include "transaction.hpp"
#include "world.hpp"

#include "test.hpp"

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

                ConcurrentSkiplistMap<EntityID, ReadyValue, DefaultKeyService<EntityID>, ScanDiscipline> mut_ready;

                // Copy the EntityIDs waiting on now to the _ready skiplist
                waiting_on_now.for_each([this, &mut_ready] (std::pair<Time, EntityID> x) {
                    assert(x.first == _time);
                    (void) mut_ready.try_emplace(x.second);
                });

                _ready = freeze(mut_ready);

                // HACK: _ready is now populated, _waiting_on_time is now pruned

            }
        }
    }

    // Fused dispatch-and-accumulate over the frozen ready set.
    //
    // A frozen skiplist is the left-child/right-sibling encoding of a tree:
    // each node is discovered exactly once, at its own top level, by the
    // frame whose interval [self, bound) contains it (Node::for_each_child).
    // One frame per node (and one for the head): spawn a child frame for
    // each child, weigh our own entity while the children run, join, then
    // prefix-sum.  A child of height h covers a LATER key interval than
    // every child of lower height, so the height-h child's subtree is
    // preceded within this frame by self plus the children of height below
    // h -- that is the value written into its `n`.  The frame returns its
    // subtree total; the head frame's return is the tick's total, which
    // advances the World's EntityID cursor.
    //
    // `n` and `requested` are written exactly once, by the parent frame
    // (n) and the owning frame (requested), and are complete only once the
    // notify nursery in step() has joined; try_lookup_cumulant reads them
    // in the rebuild, on the far side of that barrier.  Two frames never
    // touch the same field.
    //
    // The lookup that consumes `n` adds it for every node it ENTERS on the
    // way down.  A search enters each node on its path from that node's
    // left-child/right-sibling parent, at the node's own top level, so the
    // per-frame prefixes telescope to the absolute exclusive prefix; the
    // head is never entered and needs no `n`.
    //
    // Generic over the node type and the weighing so the rank test below
    // can drive it with synthetic weights on an epoch-allocated map; the
    // world instantiates it for the GC ready map with Entity::notify.
    // `weigh` is taken by value: a coroutine parameter must own what it
    // uses after its first suspension.
    using ReadyMap = FrozenSkiplistMap<EntityID, ReadyValue, DefaultKeyService<EntityID>, ScanDiscipline>;
    using ReadyNode = ReadyMap::Node;

    template<typename Node, typename Weigh>
    [[nodiscard]] Coroutine::Future<int64_t> rank_frame(Node const* _Nonnull self,
                                                        std::type_identity_t<Node> const* _Nullable bound,
                                                        Weigh weigh,
                                                        bool is_head) {
        Coroutine::Nursery nursery;

        // results[0] is our own weight; results[h] receives the height-h
        // child's subtree total (or stays zero for a height with no child).
        // TODO: memory waste; worst case is much bigger than likely cases
        Coroutine::Outcome<int64_t> outcomes[Node::MAX_HEIGHT + 1] = {};
        self->for_each_child(bound, [&] (Node const* _Nonnull child, Node const* _Nullable child_bound) {
            nursery.soon(outcomes[child->_height.nonatomic_load()],
                         rank_frame(child, child_bound, weigh, false));
        });
        // Our own contribution, while the children run.
        int64_t results[Node::MAX_HEIGHT + 1] = {};
        if (!is_head) {
            results[0] = weigh(self->_key.first);
            self->_key.second.requested = results[0];
        }
        // Wait for results
        co_await nursery.join();
        // Accumulate
        for (size_t i = 0; i != self->_height.nonatomic_load(); ++i) {
            results[i + 1] = results[i] + (co_await outcomes[i + 1]);
        }
        // Write back
        self->for_each_child(bound, [&] (Node const* _Nonnull child, Node const* _Nullable) {
            child->_key.second.n = results[child->_height.nonatomic_load() - 1];
        });
        // Kick the total up to the next level
        co_return results[self->_height.nonatomic_load()];
    }

    [[nodiscard]] Coroutine::Future<int64_t> notify_and_accumulate(ReadyMap const& ready,
                                                                   TransactionContext* _Nonnull context) {
        return rank_frame(ready._head, nullptr, [context](EntityID id) -> int64_t {
            Entity const* entity = nullptr;
            bool flag = context->try_read_entity_for_entity_id(id, entity);
            assert(flag && entity);
            return entity->notify(context);
        }, true);
    }

    // The exclusive prefix (over ready-set order) of `id`'s requests, if `id`
    // is ready and requested anything this tick.  False for absent ids and
    // for zero requesters: the caller must leave a held ticket alone in both
    // cases.
    template<typename Map>
    [[nodiscard]] bool try_lookup_cumulant(Map const& ready,
                                           EntityID id,
                                           int64_t& victim) {
        int64_t n = 0;
        auto it = ready.find(id, [&n](auto const& kv) {
            assert(kv.second.n >= 0);
            assert(kv.second.requested >= 0);
            n += kv.second.n;
        });
        if (it == ready.end())
            return false;
        victim = n;
        return it->second.requested > 0;
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
        ConcurrentSkiplistMap<EntityID, ReadyValue, DefaultKeyService<EntityID>, ScanDiscipline> next_ready;

        // Mutable:
        // waiting_on_next_time contains all EntityIDs to notify at next_time
        // next_waiting_on_time contains all EntityIDs to notify after next time
        // next_ready _will_ contain all EntityIDs to notify at next_time

        // TODO: We don't need to create waiting_on_next_time, we just need a
        // masked for_each on _waiting_on_time

        Coroutine::Outcome<int64_t> entity_id_requests;
        {
            Coroutine::Nursery nursery;

            // For each EntityID ready now, look up the Entity and notify it.
            // On notification, entities will typically examine the World and
            // may propose a Transaction to change it, and may request some
            // number of new EntityIDs; the traversal augments the ready set
            // with the cumulant of those requests (see notify_and_accumulate).
            co_await nursery.fork(entity_id_requests,
                                  notify_and_accumulate(_ready, &context));

            // For each EntityID ready next_time, copy it into next_ready
            Coroutine::Outcome<void> _;
            co_await nursery.fork(_, waiting_on_next_time
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

        Coroutine::Outcome<WaitableMap<Coordinate, Term>> new_value_for_coordinate;
        Coroutine::Outcome<WaitableMap<Coordinate, EntityID>> new_entity_id_for_coordinate;
        Coroutine::Outcome<WaitableMap<Coordinate, WaitSet>> new_located_for_coordinate;
        Coroutine::Outcome<WaitableMap<EntityID, Entity const*>> new_entity_for_entity_id;
        Coroutine::Outcome<Set> new_next_waiting_on_time;

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
                                                         freeze(context._verb_value_for_coordinate),
                                                         value_for_coordinate_action));
        
        co_await nursery.fork(new_entity_id_for_coordinate,
                              coroutine_parallel_rebuild2_unified(_entity_id_for_coordinate,
                                                         freeze(context._verb_entity_id_for_coordinate),
                                                         action_for_entity_id_for_coordinate));

        co_await nursery.fork(new_located_for_coordinate,
                              coroutine_parallel_rebuild2_unified(_located_for_coordinate,
                                                         freeze(context._verb_located_for_coordinate),
                                                         action_for_located_for_coordinate));

        co_await nursery.fork(new_entity_for_entity_id,
                              coroutine_parallel_rebuild2_unified(_entity_for_entity_id,
                                                         freeze(context._verb_entity_for_entity_id),
                                                         action_for_entity_for_entity_id));
        
        co_await nursery.fork(new_next_waiting_on_time,
                              coroutine_parallel_rebuild(next_waiting_on_time,
                                                         freeze(context._wait_on_time),
                                                         action_for_waiting_on_time));

        co_await nursery.join();

        // -- completion barrier --

        // Terrain has no transaction channel yet; the persistent map is
        // carried over unchanged (an O(1) structural share, not a copy).

        co_return new World{
            next_time,
            _entity_id_source + (co_await entity_id_requests),
            freeze(next_ready),
            (co_await new_entity_id_for_coordinate),
            (co_await new_located_for_coordinate),
            (co_await new_entity_for_entity_id),
            (co_await new_value_for_coordinate),
            _terrain_for_coordinate,
            (co_await new_next_waiting_on_time)
        };
        
    } // World::step


    // Rank oracle for the deterministic-EntityID machinery: random ready
    // maps with synthetic weights (some zero), the frame accumulate run
    // exactly as step() runs it, then every lookup checked against the
    // brute-force exclusive prefix over EntityID order.  Also pins the two
    // contract points a save/load round trip cannot see: absent ids and
    // zero requesters must report false, so a held ticket is never
    // clobbered.  Epoch-allocated so the test needs only the epoch floor,
    // held in this frame across the co_await; the world's ScanDiscipline
    // instantiation differs only in the slot type.
    define_test("ready_rank") {
        using Map = ConcurrentSkiplistMap<EntityID, ReadyValue, DefaultKeyService<EntityID>, EpochDiscipline>;
        epoch::Epoch guard = pin_global_epoch();
        for (int iter = 0; iter != 100; ++iter) {
            Map m;
            std::map<EntityID, int64_t> weights; // the oracle; std::map order == ready-set order
            int n = std::rand() % 64;
            for (int i = 0; i != n; ++i) {
                EntityID id{1 + (uint64_t)(std::rand() % 256)};
                int64_t w = (std::rand() % 3) ? (std::rand() % 5) : 0;
                if (weights.try_emplace(id, w).second)
                    (void) m.try_emplace(id);
            }
            auto f = freeze(m);
            int64_t total = co_await rank_frame(f._head, nullptr, [&weights](EntityID id) -> int64_t {
                return weights.at(id);
            }, true);
            int64_t sum = 0;
            for (auto const& [id, w] : weights)
                sum += w;
            assert(total == sum);
            int64_t prefix = 0;
            for (auto const& [id, w] : weights) {
                int64_t cumulant = -1;
                bool dealt = try_lookup_cumulant(f, id, cumulant);
                assert(dealt == (w > 0));
                if (dealt)
                    assert(cumulant == prefix);
                prefix += w;
            }
            for (int i = 0; i != 8; ++i) {
                EntityID id{1 + (uint64_t)(std::rand() % 256)};
                if (weights.contains(id))
                    continue;
                int64_t cumulant = -1;
                assert(!try_lookup_cumulant(f, id, cumulant));
            }
            if (!(iter & 15))
                mutator_repin();
        }
        unpin_global_epoch(guard);
        co_return;
    };

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
