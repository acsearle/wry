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

namespace wry::sim {
    
    void Source::notify(TransactionContext* context) const {
        printf("%s\n", __PRETTY_FUNCTION__);
        Transaction* tx = Transaction::make(context, this, 2);
        Value _ = {};
        if (!tx->try_read_value_for_coordinate(this->_location, _))
            tx->try_write_value_for_coordinate(this->_location, this->_of_this);
        tx->wait_on_value_for_coordinate(this->_location, Transaction::Condition::ALWAYS);
    }
    
    void Sink::notify(TransactionContext* context) const {
        Transaction* tx = Transaction::make(context, this, 2);
        Value x = {};
        if (tx->try_read_value_for_coordinate(this->_location, x))
            tx->try_write_value_for_coordinate(this->_location, value_make_empty());
        tx->wait_on_value_for_coordinate(this->_location, Transaction::Condition::ALWAYS);
    }
    
    inline EntityID spawner_new_entity_from_prototype() { return {}; }
    
    void Spawner::notify(TransactionContext* context) const {
        Transaction* tx = Transaction::make(context, this, 10);
        EntityID a = tx->read_entity_id_for_coordinate(this->_location);
        if (!a) {
            Machine* machine = new Machine;
            /*
            machine->_old_location = _location;
            machine->_new_location = _location;
            machine->_old_time = world_time(world);
            machine->_new_time = world_time(world);
            entity_add_to_world(machine, world);
            set_world_coordinate_occupant(world, _location, machine);
            did_write_world_coordinate(world, _location);
            did_read_world_entity(world, this);
             */
            EntityID b = spawner_new_entity_from_prototype();
            tx->write_entity_for_entity_id(b, machine);
            tx->write_entity_id_for_coordinate(this->_location, b);
            // tx->wait_on_next(b);
        }
        tx->wait_on_entity_id_for_coordinate(this->_location, Transaction::Condition::ALWAYS);
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
        
        // Propose to write the incremented value back to the location
        transaction->try_write_value_for_coordinate(this->_location, value + 1);
        
        // If the transaction succeeds, run again in 120 ticks (= 1 second)
        transaction->on_commit_sleep_for(120);
        
        // If the transaction fails, try again on next tick
        transaction->on_abort_retry();
        
    }
        
    
} // namespace wry::sim
