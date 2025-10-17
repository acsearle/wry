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

namespace wry {
    
    void Source::notify(TransactionContext* context) const {
        printf("%s\n", __PRETTY_FUNCTION__);
        Transaction* tx = Transaction::make(context, this, 2);
        Value _ = {};
        if (!tx->try_read_value_for_coordinate(this->_location, _))
            tx->write_value_for_coordinate(this->_location, this->_of_this);
        tx->wait_on_value_for_coordinate(this->_location, Transaction::Operation::WAIT_ALWAYS);
    }
    
    void Sink::notify(TransactionContext* context) const {
        Transaction* tx = Transaction::make(context, this, 2);
        Value x = {};
        if (tx->try_read_value_for_coordinate(this->_location, x))
            tx->write_value_for_coordinate(this->_location, value_make_empty());
        tx->wait_on_value_for_coordinate(this->_location, Transaction::Operation::WAIT_ALWAYS);
    }
        
    void Spawner::notify(TransactionContext* context) const {
        Transaction* tx = Transaction::make(context, this, 10);
        EntityID a = {};
        (void) tx->try_read_entity_id_for_coordinate(this->_location, a);
        printf("Read EntityID for Coordinate %lld\n", a.data);
        if (!a) {
            Machine* machine = new Machine;
            machine->_old_location = _location;
            machine->_new_location = _location;
            machine->_old_time = context->_world->_time;
            machine->_new_time = context->_world->_time;
            EntityID b = machine->_entity_id;
            printf("Made new EntityID for Coordinate %lld\n", b.data);
            tx->write_entity_for_entity_id(b, machine);
            tx->write_entity_id_for_coordinate(this->_location, b);
            tx->write_entity_id_for_time(context->_world->_time + 1, b);
        }
        tx->wait_on_entity_id_for_coordinate(this->_location, Transaction::Operation::WAIT_ALWAYS);
    }
    
    void Counter::notify(TransactionContext* context) const {
        
        // A counter increments the value at its location
                
        // Read the value at the location
        Value value = value_make_zero(); // Unchanged if there is no value at the location yet
        (void) context->try_read_value_for_coordinate(this->_location, value);

        // Create a transaction
        size_t max_items = 3;
        Transaction* transaction = Transaction::make(context,
                                                     this,
                                                     max_items);
        
        printf("Counter is incrementing\n");
        
        // Propose to write the incremented value back to the location
        transaction->write_value_for_coordinate(this->_location, value + 1);
        
        // If the transaction succeeds, run again in 120 ticks (= 1 second)
        transaction->on_commit_sleep_for(1);
        
        // If the transaction fails, try again on next tick
        transaction->on_abort_retry();
        
    }
    
    void Evenator::notify(TransactionContext* context) const {
        // An evenator reads the value at its loccation, and increments it if it is odd
        
        Value value = value_make_zero(); // Unchanged if there is no value at the location yet
        (void) context->try_read_value_for_coordinate(this->_location, value);
        
        // Create a transaction
        size_t max_items = 3;
        Transaction* transaction = Transaction::make(context,
                                                     this,
                                                     max_items);
        
        if (value.as_int64_t() & 1) {
            printf("Evenator is incrementing\n");
            transaction->write_value_for_coordinate(this->_location,
                                                    value + 1,
                                                    Transaction::Operation::WRITE_ON_COMMIT
                                                    | Transaction::Operation::WAIT_ON_COMMIT
                                                    );
            transaction->on_abort_retry();
        } else {
            printf("Evenator is watching\n");
            transaction->wait_on_value_for_coordinate(this->_location, Transaction::Operation::WAIT_ALWAYS);
        }


        
        
    }
        
    
} // namespace wry::sim
