//
//  transaction.cpp
//  client
//
//  Created by Antony Searle on 3/12/2024.
//

#include "transaction.hpp"
#include "world.hpp"

namespace wry::sim {
    
    uint64_t Transaction::Node::priority() const {
        return _parent->_context->entity_get_priority(_parent->_entity);
    }
    
    bool TransactionContext::try_read_value_for_coordinate(Coordinate xy, Value& v) {
        return this->_world->_value_for_coordinate.try_get(xy, v);
    }
    
    uint64_t TransactionContext::entity_get_priority(const Entity* entity) {
        uint64_t priority = entity->_entity_id.data ^ this->_world->_tick;
        // printf("looked up priority %llu\n", priority);
        return priority;
    }
    
    void Transaction::write_value_for_coordinate(Coordinate xy, Value v) {
        this->_context->_transactions_for_coordinate.access([&] (auto& m) {
            auto q = _nodes + _size++;
            q->_parent = this;
            q->_desired = v;
            auto* p = &(m.try_emplace(xy, nullptr).first->second);
            q->_head = p;
            q->_next = p->load(Ordering::ACQUIRE);
            while (!p->compare_exchange_weak(q->_next, q, Ordering::RELEASE, Ordering::ACQUIRE))
                ;
        }
                                                            );
    }
    

    
    
    Transaction::State Transaction::resolve() const {
        State observed;
        observed = _state.load(Ordering::RELAXED);
        if (observed != INITIAL) {
            // printf("    Transaction for EntityID %llu is already resolved\n", _entity->_entity_id.data);
            // already resolved
            return observed;
        }
        // we are in a race to resolve ourself and our collisions
        // TODO: we need to get some global seed into this
        uint64_t priority = _context->entity_get_priority(_entity);
        // for each of our proposed writes
        for (size_t i = 0; i != _size; ++i) {
            const Node* head = _nodes[i]._head->load(Ordering::RELAXED);
            // for each transaction proposing a conflicting write
            for (; head; head = head->_next) {
                // if that transaction is higher priority than us
                if (head->priority() < priority) {
                    // A higher priority transaction conflicts with us.  We
                    // must resolve it, to see if it aborts us, or is aborted by
                    // a third even higher priority transaction on some other
                    // collision
                    observed = head->resolve();
                    assert(observed != INITIAL);
                    if (observed == COMMITTED) {
                        // other transaction aborts us
                        return abort();
                    }
                    // else, other transaction ABORTED and we may continue resolving
                }
            }
        }
        return commit();
        
    }

    Transaction::State Transaction::abort() const {
        State prior = _state.exchange(State::ABORTED, Ordering::RELAXED);
        assert(prior != State::COMMITTED);
        //if (prior != State::ABORTED)
          //  printf("ABORTED transaction for EntityID %llu\n", _entity->_entity_id.data);
        // if (prior == State::ABORTED)
            // printf("    Redundant ABORT for EntityID %llu\n", _entity->_entity_id.data);
        return ABORTED;
    }

    Transaction::State Transaction::commit() const {
        State prior = _state.exchange(State::COMMITTED, Ordering::RELAXED);
        assert(prior != State::ABORTED);
        //if (prior != State::COMMITTED)
          //  printf("COMMITTED transaction for EntityID %llu\n", _entity->_entity_id.data);
        // if (prior == State::COMMITTED)
            // printf("    Redundant COMMIT for EntityID %llu\n", _entity->_entity_id.data);
        return COMMITTED;
    }

} // namespace wry::sim
