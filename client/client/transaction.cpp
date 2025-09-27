//
//  transaction.cpp
//  client
//
//  Created by Antony Searle on 3/12/2024.
//

#include "transaction.hpp"
#include "world.hpp"

namespace wry {
    
    bool TransactionContext::try_read_value_for_coordinate(Coordinate xy, Value& v) {
        return this->_world->_value_for_coordinate.try_get(xy, v);
    }
    
    uint64_t TransactionContext::entity_get_priority(const Entity* entity) {
        uint64_t priority = entity->_entity_id.data ^ this->_world->_time;
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
    void transaction_verb_generic(Transaction* self,
                                   ConcurrentMap<Key, Atomic<const Transaction::Node*>>* map,
                                   Key key,
                                   T desired,
                                   Transaction::Operation operation) {
        Transaction::Node* node = self->_nodes + (self->_size)++;
        node->_parent = self;
        node->_desired = desired;
        node->_operation = operation;
        node->_next = nullptr;
        // Race to initialize the atomic linked list with our desired value
        auto [iterator, flag] = map->try_emplace(key, node);
        // We always get back its address
        Atomic<Transaction::Node const*>& head = iterator->second;
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
        transaction_verb_generic(this,
                                 &(_context->_verb_entity_for_entity_id),
                                 key,
                                 desired,
                                 Operation::WRITE_ON_COMMIT);
    }

    void Transaction::write_value_for_coordinate(Coordinate key, Value desired) {
        transaction_verb_generic(this,
                                 &(_context->_verb_value_for_coordinate),
                                 key,
                                 desired,
                                 Operation::WRITE_ON_COMMIT);
    }

    void Transaction::write_entity_id_for_coordinate(Coordinate key, EntityID desired) {
        transaction_verb_generic(this,
                                  &(_context->_verb_entity_id_for_coordinate),
                                  key,
                                  desired,
                                  Operation::WRITE_ON_COMMIT);
    }

    
    /*
    template<typename Key>
    void transaction_wait_on_generic(Transaction* self,
                                     ConcurrentMap<Key, Atomic<const Transaction::Node*>>* map,
                                     Key key,
                                     Transaction::Operation operation) {
        Transaction::Node* node = self->_nodes + (self->_size)++;
        node->_parent = self;
        node->_desired = self->_entity->_entity_id;
        node->_operation = operation;
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
     */

    
    void Transaction::wait_on_value_for_coordinate(Coordinate key, Transaction::Operation operation) {
        transaction_verb_generic<Coordinate, Value>(this,
                                 &(_context->_verb_value_for_coordinate),
                                 key,
                                 {},
                                 operation);
    }
    
    void Transaction::wait_on_entity_id_for_coordinate(Coordinate key, Transaction::Operation operation) {
        transaction_verb_generic<Coordinate, EntityID>(this,
                                                       &(_context->_verb_entity_id_for_coordinate),
                                                       key,
                                                       {},
                                                       operation);
    }
    
    void Transaction::wait_on_entity_for_entity_id(EntityID key, Transaction::Operation operation) {
        transaction_verb_generic<EntityID, Entity const*>(this,
                                                          &(_context->_verb_entity_for_entity_id),
                                                          key,
                                                          {},
                                                          operation);
    }

    void Transaction::wait_on_time(Time key, Transaction::Operation operation) {
        // Can't schedule things for past or present
        assert(key > _context->_world->_time);
        transaction_verb_generic(this,
                                    &(_context->_wait_on_time),
                                    key,
                                    _entity->_entity_id,
                                    operation);
    }

    void Transaction::on_commit_sleep_for(uint64_t ticks) {
        assert(ticks > 0);
        wait_on_time(_context->_world->_time + ticks, Transaction::Operation::WAIT_ON_COMMIT);
    }

    void Transaction::on_abort_retry() {
        wait_on_time(_context->_world->_time + 1, Transaction::Operation::WAIT_ON_ABORT);
    }
    
    
    


    
    
    Transaction::State Transaction::resolve() const {
        State observed;
        observed = _state.load(Ordering::RELAXED);
        if (observed != INITIAL) {
            // printf("    Transaction for EntityID %llu is already resolved\n", _entity->_entity_id.data);
            // The transaction has already been resolved.
            return observed;
        }
        // We are in a race to resolve ourself and our dependencies
        uint64_t priority = _context->entity_get_priority(_entity);
        // For each of our proposed actions
        for (size_t i = 0; i != _size; ++i) {
            // If our action is exclusive
            if (_nodes[i]._operation & Operation::WRITE_ON_COMMIT) {
                // Get the head of the list of actions on this key
                // ORDER: Transaction mutations happen-before the completion
                // barrier happens-before transaction resolution
                const Node* head = _nodes[i]._head->load(Ordering::RELAXED);
                // Consider each action
                for (; head; head = head->_next) {
                    // If that transaction is higher priority than us
                    //   AND
                    // it proposes a write operation
                    if ((head->_operation & Operation::WRITE_ON_COMMIT)
                        && (head->priority() < priority))
                    {
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
                    // else, the transaction is lower priority (or it is our
                    // own entry in the list, and the same priority).  We don't
                    // need to resolve it, and eagerly attempting to do so
                    // would cause a cyclic dependency.
                }
            }
        }
        return commit();
        
    }

    Transaction::State Transaction::abort() const {
        State prior = _state.exchange(State::ABORTED, Ordering::RELAXED);
        assert(prior != State::COMMITTED);
        if (prior != State::ABORTED)
            printf("ABORTED transaction for EntityID %llu\n", _entity->_entity_id.data);
         if (prior == State::ABORTED)
             printf("    Redundant ABORT for EntityID %llu\n", _entity->_entity_id.data);
        return ABORTED;
    }

    Transaction::State Transaction::commit() const {
        State prior = _state.exchange(State::COMMITTED, Ordering::RELAXED);
        assert(prior != State::ABORTED);
        if (prior != State::COMMITTED)
            printf("COMMITTED transaction for EntityID %llu\n", _entity->_entity_id.data);
         if (prior == State::COMMITTED)
             printf("    Redundant COMMIT for EntityID %llu\n", _entity->_entity_id.data);
        return COMMITTED;
    }

} // namespace wry::sim
