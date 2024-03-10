//
//  spawner.cpp
//  client
//
//  Created by Antony Searle on 11/10/2023.
//

#include "machine.hpp"
#include "spawner.hpp"
#include "world.hpp"

namespace wry::sim {
    
    void Source::notify(World* world) {
        if (peek_world_coordinate_occupant(world, _location)
            || peek_world_coordinate_value(world, _location).d)
        {
            // wait until unoccupied and empty
            entity_wait_on_world_coordinate(this, world, _location);
            return;
        }
        if (can_write_world_coordinate(world, _location)
            && can_read_world_entity(world, this))
        {
            set_world_coordinate_value(world, _location, _of_this);
            did_write_world_coordinate(world, _location);
            did_read_world_entity(world, this);
        }
        entity_ready_on_world(this, world);
    }
    
    void Sink::notify(World* world) {
        if (peek_world_coordinate_occupant(world, _location)
            || !peek_world_coordinate_value(world, _location).d)
        {
            entity_wait_on_world_coordinate(this, world, _location);
            return;
        }
        if (can_write_world_coordinate(world, _location)
            && can_read_world_entity(world, this)) 
        {
            clear_world_coordinate_value(world, _location);
            did_write_world_coordinate(world, _location);
            did_read_world_entity(world, this);
        }
        entity_ready_on_world(this, world);
    }
    
    void Spawner::notify(World* world) {
        if (peek_world_coordinate_occupant(world, _location)) {
            // wait until unoccupied
            entity_wait_on_world_coordinate(this, world, _location);
            return;
        }
        if (can_write_world_coordinate(world, _location)
            && can_read_world_entity(world, this)) 
        {
            Machine* machine = new Machine;
            machine->_old_location = _location;
            machine->_new_location = _location;
            machine->_old_time = world_time(world);
            machine->_new_time = world_time(world);
            entity_add_to_world(machine, world);
            set_world_coordinate_occupant(world, _location, machine);
            did_write_world_coordinate(world, _location);
            did_read_world_entity(world, this);
        }
        entity_ready_on_world(this, world);
    }
    
} // namespace wry::sim
