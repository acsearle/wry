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
        auto& our_tile = world->_tiles[_location];
        if (our_tile._occupant || our_tile._value.discriminant) {
            // wait until state changes
            entity_wait_on_world_coordinate(this, world, _location);
            return;
        }
        // if (!our_tile._transaction.can_write(world_time(world))) {
        if (!can_write_world_coordinate(world, _location)) {
            entity_ready_on_world(this, world);
            return;
        }
        our_tile._value = _of_this;
        // our_tile._transaction.did_write(world_time(world));
        did_write_world_coordinate(world, _location);
        entity_ready_on_world(this, world);
    }
    
    void Sink::notify(World* world) {
        auto& our_tile = world->_tiles[_location];
        if (our_tile._occupant || !our_tile._value.discriminant) {
            // wait until state changes
            entity_wait_on_world_coordinate(this, world, _location);
            return;
        }
        // if (!our_tile._transaction.can_write(world_time(world))) {
        if (!can_write_world_coordinate(world, _location)) {
            entity_ready_on_world(this, world);
            return;
        }
        our_tile._value = Value{DISCRIMINANT_NONE, 0};
        // our_tile._transaction.did_write(world_time(world));
        did_write_world_coordinate(world, _location);
        entity_ready_on_world(this, world);
    }
    
    void Spawner::notify(World* world) {
        
        auto& our_tile = world->_tiles[_location];
        if (our_tile._occupant) {
            return (void) entity_wait_on_world_coordinate(this, world, _location);
        }
        // if (our_tile._transaction.can_write(world_time(world))) {
        if (can_write_world_coordinate(world, _location)) {
            Machine* q = new Machine;
            q->_old_location = _location;
            q->_new_location = _location;
            q->_heading = HEADING_NORTH;
            world->_entities.push_back(q);
            our_tile._occupant = q;
            did_write_world_coordinate(world, _location);
            entity_ready_on_world(q, world);
        } else {
            // conflict
        }
        // either way, we want to try again next turn
        entity_ready_on_world(this, world);
    }
    
    /*

     // Make a new Machine
     Machine* q = new Machine;
     w._all_entities.push_back(q);
     {
     // Configure machine to be at _location (which Spawner owns)
     q->_old_location = _location;
     q->_new_location = _location;
     // transfer our lock ownership to the new machine
     auto& our_tile = world->_tiles[_location];
     assert(!our_tile._lock_queue.empty() && (our_tile._lock_queue.front() == this));
     our_tile._lock_queue.front() = q;
     // rejoin the lock queue for when the new machine clears our tile
     our_tile._lock_queue.push_back(this);
     }
     {
     q->_heading = HEADING_NORTH;
     q->_desired_location = _location;
     ++(q->_desired_location.y);
     auto& desired_tile = world->_tiles[q->_desired_location];
     // launch the machine into the desired location lock queue
     desired_tile.suspend_for_lock(q, w, q->_desired_location);
     // the machine may have run inside the function above, starting its
     // departure from our tile.  we will be awoken again when it leaves
     }
    void Source::wake_location_locked(World* world, Coordinate xy) {
        assert(xy == _location);
        auto& a = world->_tiles[xy];
        if (!a._value) {
            a._value = _of_this;
            a.notify_all(w, xy);
        }
        a.unlock(w, this, xy);
        a.wait_on(this);
    }
    
    void Source::wake_location_changed(World* world, Coordinate xy) {
        assert(xy == _location);
        auto& a = world->_tiles[xy];
        
        if (a._value == _of_this)
            a.wait_on(this);
        else
            a.suspend_for_lock(this, w, xy);
    }
    
    // is it worth breaking out these three responses into separate base classes?
    void Source::wake_time_elapsed(World*, Time) {
        assert(false);
    }

    void Sink::wake_location_locked(World* world, Coordinate xy) {
        assert(xy == _location);
        auto& a = world->_tiles[xy];
        a._value = {0, 0};
        a.notify_all(w, xy);
        a.unlock(w, this, xy);
        a.wait_on(this);
    }

    void Sink::wake_location_changed(World* world, Coordinate xy) {
        assert(xy == _location);
        assert(false);
    }
    void Sink::wake_time_elapsed(World*, Time) {
        assert(false);
    }

     */
    
    
} // namespace wry::sim
