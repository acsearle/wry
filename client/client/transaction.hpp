//
//  transaction.hpp
//  client
//
//  Created by Antony Searle on 3/12/2024.
//

#ifndef transaction_hpp
#define transaction_hpp

#include "concurrent_map.hpp"
#include "atomic.hpp"
#include "entity.hpp"
#include "garbage_collected.hpp"
#include "persistent_map.hpp"

namespace wry::sim {

    // SAFETY: None
    template<size_t N = 8>
    struct ExternallyDiscriminatedVariant {
        alignas(N) unsigned char _data[N];
        template<typename T>
        ExternallyDiscriminatedVariant& operator=(const T& value) {
            static_assert((sizeof(T) <= N) && (alignof(T) <= N));
            std::memcpy(_data, &value, sizeof(value));
            return *this;
        }
    };
    
    template<typename T, size_t N>
    T get(const ExternallyDiscriminatedVariant<N>& x) {
        static_assert((sizeof(T) <= 8) && (alignof(T) <= 8));
        T result;
        std::memcpy(&result, x._data, sizeof(T));
        return result;
    }
    
    struct Transaction : GarbageCollected {
        
        // A ready entity is notfied
        // In the notification, it can read from the current world state and
        // propose a set of writes to make produce a new world state
        // This proposal is a transaction, and all or none will succeed.
        // Each entity has a unique priority (hash of entity ID salted with
        // time)
        // Transactions abort if another transaction with a conflicting write
        // commits.  Transactions may only commit after proving all conflicting
        // higher priority transactions have aborted, because their writes to
        // other locations conflicted with even higher priority transactions.
        // At least one transaction commits, because one entity has the unique
        // highest priority.
        // The common case is for transactions to not conflict at all.
        // It is rare for there to be long chains of dependencies.
        // Transactions can mostly be resolved in parallel.
        // Entities whose transactions that are aborted can choose to try again
        // next tick, that is they can requeue the entity as ready.
        // Transactions that commit can also choose to requeue themselves to do
        // something else next tick, or on a change, etc.
        // Because the priority ranking of entities is pseudorandomized every
        // tick, an entity can't starve indefinitely.
        
        // A transaction that wants to write to a location (entity, etc.) looks
        // up that thing in an address-stable concurrent map structure
        // (such as a skiplist) that maps to an atomic pointer.  If the entry
        // does not exist, the transaction races to create a null pointer there.
        // The transaction then atomically prepends a link to itself to the
        // list.  This list contains all transactions that want to write to the
        // location.  Orthogonally, each transaction contains a list of all the
        // locations it wants to write to.  By following pointers around we
        // can navigate the whole structure.
        
        // We now build the new state.
        // We traverse the old world state maps and the write location maps for
        // each kind of thing (location, entity, ...).  Once we find a subtree
        // with no writes we can just keep it.  Once we find a leaf write, we
        // resolve which transaction, if any, wins.  To do this, we follow the
        // transaction list for the location, until we prove that one commits
        // or all abort.  This recursively navigates the graph of transactions
        // resolving their state, always terminating because only higher
        // priority transactions affect the state of lower priority transactions,
        // and on average quickly, because conflicts are statistically rare
        // anyway.  Once we made (or found) a committed transaction for the
        // location, we know its value in the new world state map, and can
        // build up that part of the new map.
        //
        // Writes are rare.  Most locations do not change value on a given tick.
        // Transaction conflicts are rarer.  Most transactions commit.
        // The new state is mostly the old state.  We can build the new state
        // in parallel quickly.
        
        // One problem with efficient implementation is that we need to store
        // the written value for each write location, which is of variable size
        // in principle, and we want to avoid doing an allocation for every
        // written location for every transaction.  The array of Nodes used
        // below means we have to have a fixed or max size value to write.
        // - Is this actually a problem?  Is it uncommon enough we just use an
        // indirection when needed?
        //
        // The transaction itself and all these nodes and values could be
        // embedded in the pseudostack of a coroutine implementing "notify",
        // giving a very flexible structure.
        //
        // We don't want to bake only a few transaction subtypes since the
        // system should be open to extension at runtime
        
        enum State {
            INITIAL,
            COMMITTED,
            ABORTED,
        };
        
        enum Condition {
            NEVER = 0,
            ON_COMMIT = 1,
            ON_ABORT = 2,
            ALWAYS = 3
        };
        
        struct Node {
            const Node* _next;
            const Transaction* _parent;
            const Atomic<const Transaction::Node*>* _head;
            ExternallyDiscriminatedVariant<> _desired;
            Condition _condition;
            
            State resolve() const {
                return _parent->resolve();
            }
            
            State abort() const {
                return _parent->abort();
            }
            
            // seems dubious to commit at the Node level
                        
            uint64_t priority() const;
            
        };
        
        TransactionContext* _context = nullptr;
        const Entity* _entity = nullptr;
        mutable Atomic<State> _state{};
        
        size_t _capacity = 0;
        size_t _size = 0;
        Node _nodes[0];
                
        virtual void _garbage_collected_enumerate_fields(TraceContext*) const override {}
        
        static void* operator new(size_t basic, size_t extra) {
            return GarbageCollected::operator new(basic + extra * sizeof(Node));
        }
        
        Transaction(TransactionContext* context, const Entity* entity, size_t capacity)
        : _context(context)
        , _entity(entity)
        , _state(INITIAL)
        , _capacity(capacity) {}
        
        ~Transaction() {
            //printf("%s\n", __PRETTY_FUNCTION__);
        }
        
        static Transaction* make(TransactionContext* context, const Entity* entity, size_t count) {
            return new(count) Transaction(context, entity, count);
        }
        
        const Entity* read_entity_for_entity_id(EntityID);        
        void write_entity_for_entity_id(EntityID, const Entity*);
        void wait_on_entity_for_entity_id(EntityID, Condition);

        bool try_read_value_for_coordinate(Coordinate xy, Value& victim) const;
        void write_value_for_coordinate(Coordinate, Value);
        void wait_on_value_for_coordinate(Coordinate, Condition);

        EntityID read_entity_id_for_coordinate(Coordinate) { return {}; }
        void write_entity_id_for_coordinate(Coordinate, EntityID);
        void wait_on_entity_id_for_coordinate(Coordinate, Condition);

        void wait_on_time(Time, Condition);

        State resolve() const;
        State abort() const;
        State commit() const;
        
    }; // Transaction
    
    struct TransactionContext {
        
        const World* _world = nullptr;
        
        
        uint64_t entity_get_priority(const Entity*);
        
        bool try_read_value_for_coordinate(Coordinate, Value&);
        bool try_read_entity_id_for_coordinate(Coordinate, EntityID&);
        bool try_read_entity_for_entity_id(EntityID, const Entity*&);

        
        template<typename Key>
        using Map = ConcurrentMap<Key, Atomic<const Transaction::Node*>>;

        // "write" to a map-key is exclusive; at most one transaction will
        // COMMIT and write its value, all others will ABORT.
                
        Map<Coordinate> _write_entity_id_for_coordinate;
        Map<Coordinate> _write_value_for_coordinate;
        Map<EntityID> _write_entity_for_entity_id;
        
        // "wait on" (a write to) a key is nonexclusive.  A transaction can
        // choose to write if it COMMITs, or ABORTs, or both.
        
        Map<Coordinate> _wait_on_value_for_coordinate;
        Map<Coordinate> _wait_on_entity_id_for_coordinate;
        Map<EntityID> _wait_on_entity_for_entity_id;
        
        // Wait in other ways
        
        Map<Time> _wait_on_time; // Run at a future time
        // Retry is a special case of schedule, but also a very common case
        // and also it gets deleted from the schedule right away
                
    };
    
    
   
    
} // namespace wry::sim


#endif /* transaction_hpp */
