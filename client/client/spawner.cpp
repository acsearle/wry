//
//  spawner.cpp
//  client
//
//  Created by Antony Searle on 11/10/2023.
//

#include "machine.hpp"
#include "spawner.hpp"
#include "world.hpp"
#include "context.hpp"

namespace wry::sim {
    
    void Source::notify(TransactionContext* context) const {
        printf("%s\n", __PRETTY_FUNCTION__);
        Transaction* tx = Transaction::make(context, this, 2);
        Value x = tx->read_value_for_coordinate(this->_location);
        if (x.is_Empty())
            tx->write_value_for_coordinate(this->_location, this->_of_this);
        tx->wait_on_value_for_coordinate(this->_location);
    }
    
    void Sink::notify(TransactionContext* context) const {
        Transaction* tx = Transaction::make(context, this, 2);
        Value x = tx->read_value_for_coordinate(this->_location);
        if (!x.is_Empty())
            tx->write_value_for_coordinate(this->_location, value_make_empty());
        tx->wait_on_value_for_coordinate(this->_location);
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
            tx->write_ready(b);
        }
        tx->wait_on_entity_id_for_coordinate(this->_location);
    }
    
    void Counter::notify(TransactionContext* context) const {
        Transaction* transaction = Transaction::make(context, this, 10);
        Value x = value_make_zero();
        context->try_read_value_for_coordinate(this->_location, x);
        printf("Counter reads %lld\n", x.as_int64_t());
        x = value_make_integer_with(x.as_int64_t() + 1);
        transaction->write_value_for_coordinate(this->_location, x);
        transaction->wait_on_time(context->_world->_tick + 120);
    }
        
    
} // namespace wry::sim
