//
//  transaction.cpp
//  client
//
//  Created by Antony Searle on 3/12/2024.
//

#include "transaction.hpp"
#include "world.hpp"

namespace wry {
    
    bool TransactionContext::try_read_value_for_coordinate(Coordinate key, Value& victim) {
        return this->_world->_value_for_coordinate.try_get(key, victim);
    }
    
    bool TransactionContext::try_read_entity_id_for_coordinate(Coordinate key, EntityID& victim) {
        return this->_world->_entity_id_for_coordinate.try_get(key, victim);
    }

    bool TransactionContext::try_read_entity_for_entity_id(EntityID key, Entity const*& victim) {
        return this->_world->_entity_for_entity_id.try_get(key, victim);
    }

    
    uint64_t TransactionContext::entity_get_priority(const Entity* entity) {
        uint64_t priority = 0;
        priority = hash_combine(&entity->_entity_id.data, 8);
        priority = hash_combine(&this->_world->_time, 8, priority);
        // printf("looked up priority %llu for entity %llu\n", priority, entity->_entity_id.data);
        return priority;
    }

    
    uint64_t Transaction::Node::priority() const {
        return _parent->_context->entity_get_priority(_parent->_entity);
    }
    
    bool Transaction::try_read_value_for_coordinate(Coordinate key, Value& victim) const {
        return _context->try_read_value_for_coordinate(key, victim);
    }

    bool Transaction::try_read_entity_id_for_coordinate(Coordinate key, EntityID& victim) const {
        return _context->try_read_entity_id_for_coordinate(key, victim);
    }

    bool Transaction::try_read_entity_for_entity_id(EntityID key, Entity const*& victim) const {
        return _context->try_read_entity_for_entity_id(key, victim);
    }

    template<typename Key, typename T>
    void transaction_verb_generic(Transaction* self,
                                   ConcurrentMap<Key, Atomic<const Transaction::Node*>>* map,
                                   Key key,
                                   T desired,
                                   int operation) {
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
    


    
    auto Transaction::
    write_entity_for_entity_id(EntityID key,
                               const Entity* desired,
                               int operation)
    -> void {
        transaction_verb_generic(this,
                                 &(_context->_verb_entity_for_entity_id),
                                 key,
                                 desired,
                                 operation);
    }

    auto Transaction::
    write_value_for_coordinate(Coordinate key,
                               Value desired,
                               int operation)
    -> void {
        transaction_verb_generic(this,
                                 &(_context->_verb_value_for_coordinate),
                                 key,
                                 desired,
                                 operation);
    }

    auto Transaction::
    write_entity_id_for_coordinate(Coordinate key,
                                   EntityID desired,
                                   int operation)
    -> void {
        transaction_verb_generic(this,
                                  &(_context->_verb_entity_id_for_coordinate),
                                  key,
                                  desired,
                                  Operation::WRITE_ON_COMMIT);
    }
    
    auto Transaction::
    wait_on_value_for_coordinate(Coordinate key,
                                 int operation)
    -> void {
        transaction_verb_generic<Coordinate, Value>(this,
                                 &(_context->_verb_value_for_coordinate),
                                 key,
                                 {},
                                 operation);
    }
    
    auto Transaction::
    wait_on_entity_id_for_coordinate(Coordinate key,
                                     int operation)
    -> void {
        transaction_verb_generic<Coordinate, EntityID>(this,
                                                       &(_context->_verb_entity_id_for_coordinate),
                                                       key,
                                                       {},
                                                       operation);
    }
    
    auto Transaction::wait_on_entity_for_entity_id(EntityID key, int operation)
    -> void {
        transaction_verb_generic<EntityID, Entity const*>(this,
                                                          &(_context->_verb_entity_for_entity_id),
                                                          key,
                                                          {},
                                                          operation);
    }

    auto Transaction::
    write_entity_id_for_time(Time key, EntityID value, int operation) -> void {
        assert(key > _context->_world->_time);
        transaction_verb_generic(this,
                                 &(_context->_wait_on_time),
                                 key,
                                 value,
                                 operation);
    }

    
    auto Transaction::
    wait_on_time(Time key,
                 int operation) -> void {
        // Can't schedule things for past or present
        assert(key > _context->_world->_time);
        transaction_verb_generic(this,
                                    &(_context->_wait_on_time),
                                    key,
                                    _entity->_entity_id,
                                    operation);
    }

    auto Transaction::
    on_commit_sleep_for(uint64_t ticks)
    -> void {
        assert(ticks > 0);
        wait_on_time(_context->_world->_time + ticks, WAIT_ON_COMMIT);
    }

    auto Transaction::on_abort_retry()
    -> void {
        wait_on_time(_context->_world->_time + 1, WAIT_ON_ABORT);
    }
    
    
    Transaction::State Transaction::resolve() const {
        // Check if the transaction was already resolved
        State observed = _state.load(Ordering::RELAXED);
        if (observed != INITIAL)
            return observed;
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
            printf("EntityID %llu ABORTED\n", _entity->_entity_id.data);
        if (prior == State::ABORTED)
            printf("EntityID %llu REDUNDANT ABORTED\n", _entity->_entity_id.data);
        return ABORTED;
    }

    Transaction::State Transaction::commit() const {
        State prior = _state.exchange(State::COMMITTED, Ordering::RELAXED);
        assert(prior != State::ABORTED);
//        if (prior != State::COMMITTED)
//            printf("COMMITTED transaction for EntityID %llu\n", _entity->_entity_id.data);
        if (prior == State::COMMITTED)
            printf("EntityID %llu REDUNDANT COMMIT\n", _entity->_entity_id.data);
        return COMMITTED;
    }
    
    void Transaction::describe() const {
        if (_entity) {
            printf("EntityID %llu {", _entity->_entity_id.data);
            for (std::size_t i = 0; i != _size; ++i) {
                static char const* str[] = {
                    "WAIT_NEVER",
                    "WAIT_ON_COMMIT",
                    "WAIT_ON_ABORT",
                    "WAIT_ALWAYS",
                    "WRITE_ON_COMMIT",
                };
                printf("\n    %s,", str[_nodes[i]._operation]);
            }
            printf("}\n");
        }
    }

} // namespace wry::sim
