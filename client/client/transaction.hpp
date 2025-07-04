//
//  transaction.hpp
//  client
//
//  Created by Antony Searle on 3/12/2024.
//

#ifndef transaction_hpp
#define transaction_hpp

#include "atomic.hpp"
#include "entity.hpp"
#include "garbage_collected.hpp"
#include "PersistentMap.hpp"

namespace wry::sim {
    
    struct Transaction : GarbageCollected {
        
        enum State {
            INITIAL,
            COMMITTED,
            ABORTED,
        };
        
        struct Node {
            const Node* _next;
            int _wants_write;
            const Transaction* _parent;
            const Atomic<const Transaction::Node*>* _head;
            
            State resolve() const {
                return _parent->resolve();
            }
            
            uint64_t priority() const {
                return entity_get_priority(_parent->_entity);
            }
            
        };
        
        Context* _context;
        const Entity* _entity;
        mutable Atomic<State> _state;
        
        size_t _count;
        Node _nodes[0];
        
        Transaction(Context* context, const Entity* entity, size_t count)
        : _context(context)
        , _entity(entity)
        , _state(INITIAL)
        , _count(count) {}
        
        virtual void _garbage_collected_scan() const override {}
        
        static void* operator new(size_t basic, size_t extra) {
            return GarbageCollected::operator new(basic + extra * sizeof(Node));
        }
        
        static Transaction* make(Context* context, const Entity* entity, size_t count) {
            return new(count) Transaction(context, entity, count);
        }
        
        const Entity* read_entity_for_entity_id(EntityID);
        
        Value read_value_for_coordinate(Coordinate) { return {}; }
        EntityID read_entity_id_for_coordinate(Coordinate) { return {}; }

        void write_entity_for_entity_id(EntityID, const Entity*) {}
        void write_value_for_coordinate(Coordinate, Value) {}
        void write_entity_id_for_coordinate(Coordinate, EntityID) {}
        void write_ready(EntityID) {}
        
        void fail_and_wait_on_value_for_coordinate(Coordinate) {}
        void fail_and_wait_on_entity_id_for_coordinate(Coordinate) {}
        void succeed_or_wait_on_value_for_coordinate(Coordinate) {}

        void wait_on_entity_id_for_coordinate(Coordinate) {}
        void wait_on_value_for_coordinate(Coordinate) {}
        void wait_on_entity_for_entity_id(EntityID) {}
        void wait_on_time(Time) {}

        State resolve() const {
            State observed;
            observed = _state.load(Ordering::RELAXED);
            if (observed != INITIAL) {
                // already resolved
                return observed;
            }
            // we are in a race to resolve ourself and our collisions
            uint64_t priority = entity_get_priority(_entity);
            for (size_t i = 0; i != _count; ++i) {
                auto head = _nodes[i]._head->load(Ordering::RELAXED);
                auto wants_write = _nodes[i]._wants_write;
                for (; head; head = head->_next) {
                    if ((wants_write || head->_wants_write) && (head->priority() < priority)) {
                        // other transaction conflicts with us and outranks us
                        observed = head->resolve();
                        assert(observed != INITIAL);
                        if (observed == COMMITTED) {
                            // other transaction aborts us
                            observed = _state.exchange(ABORTED, Ordering::RELAXED);
                            // INITIAL: we won the race
                            // ABORTED: another thread beat us
                            // COMMITTED: another thread came to the opposite conclusion!
                            assert(observed != COMMITTED);
                            return ABORTED;
                        }
                        // else, other transaction ABORTED and we may proceed
                    }
                }
            }
            observed = _state.exchange(COMMITTED, Ordering::RELAXED);
            // INITIAL: we won the race
            // COMMITTED: another thread beat us
            // ABORTED: another thread came to the opposite conclusion!
            assert(observed != ABORTED);
            return COMMITTED;
            
        };
        
    };
    
}


#endif /* transaction_hpp */
