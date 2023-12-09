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
    
    void Spawner::notify(World& w) {
        
        auto& our_tile = w._tiles[_location];
        if (our_tile._transaction.can_read(w._tick)) {
            if (!our_tile._occupant) {
                if (our_tile._transaction.can_write(w._tick)) {
                    Machine* q = new Machine;
                    q->_old_location = _location;
                    q->_new_location = _location;
                    q->_heading = HEADING_NORTH;
                    w._entities.push_back(q);
                    our_tile._occupant = q;
                    while (!our_tile._observers.empty()) {
                        w._ready.push_back(our_tile._observers.front());
                        our_tile._observers.pop_front();
                    }
                    our_tile._observers.push_back(this);
                    our_tile._transaction.did_write(w._tick);
                    w._ready.push_back(q);
                } else {
                    // conflict: retry
                    w._ready.push_back(this);
                }
            } else {
                // bad state; wait
                our_tile._observers.push_back(this);
            }
        } else {
            // conflict: retry
            w._ready.push_back(this);
        }

      
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
     auto& our_tile = w._tiles[_location];
     assert(!our_tile._lock_queue.empty() && (our_tile._lock_queue.front() == this));
     our_tile._lock_queue.front() = q;
     // rejoin the lock queue for when the new machine clears our tile
     our_tile._lock_queue.push_back(this);
     }
     {
     q->_heading = HEADING_NORTH;
     q->_desired_location = _location;
     ++(q->_desired_location.y);
     auto& desired_tile = w._tiles[q->_desired_location];
     // launch the machine into the desired location lock queue
     desired_tile.suspend_for_lock(q, w, q->_desired_location);
     // the machine may have run inside the function above, starting its
     // departure from our tile.  we will be awoken again when it leaves
     }
    void Source::wake_location_locked(World& w, Coordinate xy) {
        assert(xy == _location);
        auto& a = w._tiles[xy];
        if (!a._value) {
            a._value = _of_this;
            a.notify_all(w, xy);
        }
        a.unlock(w, this, xy);
        a.wait_on(this);
    }
    
    void Source::wake_location_changed(World& w, Coordinate xy) {
        assert(xy == _location);
        auto& a = w._tiles[xy];
        
        if (a._value == _of_this)
            a.wait_on(this);
        else
            a.suspend_for_lock(this, w, xy);
    }
    
    // is it worth breaking out these three responses into separate base classes?
    void Source::wake_time_elapsed(World&, Time) {
        assert(false);
    }

    void Sink::wake_location_locked(World& w, Coordinate xy) {
        assert(xy == _location);
        auto& a = w._tiles[xy];
        a._value = {0, 0};
        a.notify_all(w, xy);
        a.unlock(w, this, xy);
        a.wait_on(this);
    }

    void Sink::wake_location_changed(World& w, Coordinate xy) {
        assert(xy == _location);
        assert(false);
    }
    void Sink::wake_time_elapsed(World&, Time) {
        assert(false);
    }

     */
    
    
} // namespace wry::sim
