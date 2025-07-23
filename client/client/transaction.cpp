//
//  transaction.cpp
//  client
//
//  Created by Antony Searle on 3/12/2024.
//

#include "transaction.hpp"
#include "world.hpp"

namespace wry::sim {
    
    bool TransactionContext::try_read_value_for_coordinate(Coordinate xy, Value& v) {
        return this->_world->_value_for_coordinate.try_get(xy, v);
    }
    
    uint64_t TransactionContext::entity_get_priority(const Entity* entity) {
        uint64_t priority = entity->_entity_id.data ^ this->_world->_tick;
        // printf("looked up priority %llu\n", priority);
        return priority;
    }

    
    uint64_t Transaction::Node::priority() const {
        return _parent->_context->entity_get_priority(_parent->_entity);
    }
    
    bool Transaction::try_read_value_for_coordinate(Coordinate xy, Value& victim) const {
        return _context->try_read_value_for_coordinate(xy, victim);
    }
    
    template<typename Key, typename T>
    void transaction_write_generic(Transaction* self,
                                   ConcurrentMap<Key, Atomic<const Transaction::Node*>>* map,
                                   Key key,
                                   T desired) {
        Transaction::Node* node = self->_nodes + (self->_size)++;
        node->_parent = self;
        node->_desired = desired;
        node->_condition = Transaction::ON_COMMIT; // Unused for exclusive writes
        node->_next = nullptr;
        // Race to initialize the atomic linked list with our desired value
        auto [iterator, flag] = map->try_emplace(key, node);
        // We always get back its address
        Atomic<const Transaction::Node*>& head = iterator->second;
        node->_head = &head;
        if (!flag) {
            // If we lost the race to construct the atomic, we need to atomically
            // insert our node

            // ORDER: We don't need to ACQUIRE or even RELEASE here because we
            // don't follow any pointers until after the thread barrier that
            // separates setting up all transactions from resolving all transactions
            node->_next = head.load(Ordering::RELAXED);
            while (!head.compare_exchange_weak(node->_next,
                                               node,
                                               Ordering::RELAXED,
                                               Ordering::RELAXED))
                ;
        }
    }
    


    
    void Transaction::write_entity_for_entity_id(EntityID key, const Entity* desired) {
        transaction_write_generic(this,
                                  &(_context->_write_entity_for_entity_id),
                                  key,
                                  desired);
    }

    void Transaction::write_value_for_coordinate(Coordinate key, Value desired) {
       transaction_write_generic(this,
                                           &(_context->_write_value_for_coordinate),
                                           key,
                                           desired);
    }

    void Transaction::write_entity_id_for_coordinate(Coordinate key, EntityID desired) {
        transaction_write_generic(this,
                                  &(_context->_write_entity_id_for_coordinate),
                                  key,
                                  desired);
    }

    
    
    template<typename Key>
    void transaction_wait_on_generic(Transaction* self,
                                     ConcurrentMap<Key, Atomic<const Transaction::Node*>>* map,
                                     Key key,
                                     Transaction::Condition condition) {
        Transaction::Node* node = self->_nodes + (self->_size)++;
        node->_parent = self;
        node->_desired = self->_entity->_entity_id;
        node->_condition = condition;
        node->_next = nullptr;
        // Race to initialize the atomic linked list with our desired value
        auto [iterator, flag] = map->try_emplace(key, node);
        // We always get back its address
        Atomic<const Transaction::Node*>& head = iterator->second;
        node->_head = nullptr; // We don't set the head for non-exclusive targets
        if (!flag) {
            // If we lost the race to construct the atomic, we need to atomically
            // insert our node
            
            // ORDER: We don't need to ACQUIRE or even RELEASE here because we
            // don't follow any pointers until after the thread barrier that
            // separates setting up all transactions from resolving all transactions
            node->_next = head.load(Ordering::RELAXED);
            while (!head.compare_exchange_weak(node->_next,
                                               node,
                                               Ordering::RELAXED,
                                               Ordering::RELAXED))
                ;
        }
        
        
    }

    
    void Transaction::wait_on_value_for_coordinate(Coordinate key, Transaction::Condition condition) {
        transaction_wait_on_generic(this,
                                    &(_context->_wait_on_value_for_coordinate),
                                    key,
                                    condition);
    }
    
    void Transaction::wait_on_entity_id_for_coordinate(Coordinate key, Transaction::Condition condition) {
        transaction_wait_on_generic(this,
                                    &(_context->_wait_on_entity_id_for_coordinate),
                                    key,
                                    condition);
    }
    
    void Transaction::wait_on_entity_for_entity_id(EntityID key, Transaction::Condition condition) {
        transaction_wait_on_generic(this,
                                    &(_context->_wait_on_entity_for_entity_id),
                                    key,
                                    condition);
    }

    void Transaction::wait_on_time(Time key, Transaction::Condition condition) {
        transaction_wait_on_generic(this,
                                    &(_context->_wait_on_time),
                                    key,
                                    condition);
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
            // Head is not present if the node is for a nonexclusive container
            if (_nodes[i]._head) {
                // ORDER: Transactions are resolved after the barrier
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
