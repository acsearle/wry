//
//  spawner.cpp
//  client
//
//  Created by Antony Searle on 11/10/2023.
//

#include "machine.hpp"
#include "spawner.hpp"
#include "world.hpp"
#include "transaction.hpp"
#include "utility.hpp"

namespace wry {
    
    int64_t Source::notify(TransactionContext* context) const {
        // printf("%s\n", __PRETTY_FUNCTION__);
        Transaction* tx = Transaction::make(context, this, 2);
        Term _ = {};
        if (!tx->try_read_value_for_coordinate(this->_location, _))
            tx->write_value_for_coordinate(this->_location, this->_of_this);
        tx->wait_on_value_for_coordinate(this->_location, Transaction::Operation::WAIT_ALWAYS);
        return 0;
    }
    
    int64_t Sink::notify(TransactionContext* context) const {
        Transaction* tx = Transaction::make(context, this, 2);
        Term x = {};
        if (tx->try_read_value_for_coordinate(this->_location, x))
            tx->write_value_for_coordinate(this->_location, term_make_empty());
        tx->wait_on_value_for_coordinate(this->_location, Transaction::Operation::WAIT_ALWAYS);
        return 0;
    }
        
    int64_t Spawner::notify(TransactionContext* context) const {
        Transaction* tx = Transaction::make(context, this, 10);

        // Do we have an EntityID to name a new object?
        if (_free_entity_id.data == 0) {
            // Install a copy that will have a new _free_entity_id written into it
            Spawner* next_this = make_mutable_clone();
            tx->write_entity_for_entity_id(this->_entity_id, next_this);
            tx->wait_on_time(context->next_now());
            return 1;
        }

        // Do we have an unoccupied cell to spawn at?

        EntityID a = {};
        (void) tx->try_read_entity_id_for_coordinate(this->_location, a);
        if (!a) {
            Machine* machine = new Machine;
            machine->_entity_id = this->_free_entity_id;
            machine->_free_entity_id.data = 0;
            machine->_old_location = _location;
            machine->_new_location = _location;
            machine->_old_time = context->now();
            machine->_new_time = context->now();
            EntityID b = machine->_entity_id;
            // printf("Made new EntityID for Coordinate %lld\n", b.data);
            tx->write_entity_for_entity_id(b, machine);
            tx->write_entity_id_for_coordinate(this->_location, b);
            {
                // the new machine is located here, alongside this Spawner
                // (and any other non-occupying residents)
                WaitSet located;
                (void) tx->try_read_located_for_coordinate(this->_location, located);
                located.set(b);
                // TODO: This should be a nonexclusive merge
                tx->write_located_for_coordinate(this->_location, located);
            }
            tx->write_entity_id_for_time(context->next_now(), b);

            // Install a copy that will have a new _free_entity_id written into it
            auto* next_this = make_mutable_clone();
            next_this->_free_entity_id.data = 0;
            tx->write_entity_for_entity_id(this->_entity_id, next_this);
            tx->wait_on_time(context->next_now());
            return 1;
        }

        // We have a free_id, the cell is blocked, nothing to do except wait
        tx->wait_on_entity_id_for_coordinate(this->_location, Transaction::Operation::WAIT_ALWAYS);
        return 0;
    }
    
    int64_t Counter::notify(TransactionContext* context) const {

        // A counter increments the value at its location
                
        // Read the value at the location
        Term value = term_make_zero(); // Unchanged if there is no value at the location yet
        (void) context->try_read_value_for_coordinate(this->_location, value);

        // Create a transaction
        size_t max_items = 3;
        Transaction* transaction = Transaction::make(context,
                                                     this,
                                                     max_items);
        
        printf("Counter is incrementing\n");

        // Propose to write the incremented value back to the location
        // (unless matter is parked on us; matter is never overwritten)
        if (!value.is_matter())
            transaction->write_value_for_coordinate(this->_location, value + 1);
        
        // If the transaction succeeds, run again in 120 ticks (= 1 second)
        transaction->on_commit_sleep_for(1);
        
        // If the transaction fails, try again on next tick
        transaction->on_abort_retry();
        return 0;
    }
    
    int64_t Evenator::notify(TransactionContext* context) const {
        // An evenator reads the value at its loccation, and increments it if it is odd
        
        Term value = term_make_zero(); // Unchanged if there is no value at the location yet
        (void) context->try_read_value_for_coordinate(this->_location, value);
        
        // Create a transaction
        size_t max_items = 3;
        Transaction* transaction = Transaction::make(context,
                                                     this,
                                                     max_items);
        
        if (!value.is_matter() && (value.as_int64_t() & 1)) {
            // printf("Evenator is incrementing\n");
            transaction->write_value_for_coordinate(this->_location,
                                                    value + 1,
                                                    Transaction::Operation::WRITE_ON_COMMIT
                                                    | Transaction::Operation::WAIT_ON_COMMIT
                                                    );
            transaction->on_abort_retry();
        } else {
            // printf("Evenator is watching\n");
            transaction->wait_on_value_for_coordinate(this->_location, Transaction::Operation::WAIT_ALWAYS);
        }


        return 0;

    }
        
    
} // namespace wry::sim
