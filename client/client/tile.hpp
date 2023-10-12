//
//  tile.hpp
//  client
//
//  Created by Antony Searle on 9/10/2023.
//

#ifndef tile_hpp
#define tile_hpp

#include "sim.hpp"
#include "array.hpp"
#include "entity.hpp"

namespace wry::sim {
    
    // todo: we squander lots of memory here; there will be many more
    // tiles than machines, so having multiple queue headers inline is
    // wasteful; we should employ some sparse strategy
    
    // "infinite": procedural terrain
    // explored: just terrain
    // common: stuff
    // rare: waiters
    
    // - separate tables for values?
    // - machine-intrusive linked-list queues (but, each machine can be in
    //   several queues)
    // - tagged pointers; common values inline; point out to more complex
    //   values; recycle the pointee as the values and queues vary
    // - tiles hotswap themselves with more complex implementations as they
    //   acquire dependents
    //
    
    // empty tiles are infinite
    // tiles are common
    // locks are rare
    // contested locks are rarer
    // observers are rare
    
    
    
    struct Tile {
        
        Value _value;
        array<Entity*> _lock_queue; // mutex
        array<Entity*> _wait_queue; // condition variable
        
        bool is_locked() const {
            return !_lock_queue.empty();
        }
        
        bool _not_in_queue(Entity* p) const {
            for (Entity* q : _lock_queue)
                if (q == p)
                    return false;
            return true;
        }
        
        // rather than have enqueue return a bool, have it call
        // Entity::wake_location_locked immediately?  Is this ever not the
        // caller's intent?  If it is not the caller's intent, they have try_lock
        
        bool enqueue(Entity* p) {
            assert(p);
            assert(_not_in_queue(p));
            bool was_empty = _lock_queue.empty();
            _lock_queue.push_back(p);
            return was_empty;
        }
        
        // should be last statement in wake_on_location_changed or wake_on_time
        // suspend for lock, resume for lock, resume with lock?
        void suspend_for_lock(Entity* p, World& w, Coordinate self) {
            assert(p);
            assert(_not_in_queue(p));
            bool was_empty = _lock_queue.empty();
            _lock_queue.push_back(p);
            if (was_empty)
                p->wake_location_locked(w, self);
            // caller should itself return
        }
        
        bool try_lock(Entity* p) {
            assert(p);
            assert(_not_in_queue(p));
            bool was_empty = _lock_queue.empty();
            if (was_empty)
                _lock_queue.push_back(p);
            return was_empty;
        }
        
        void unlock(World&, Entity* p, Coordinate self);
        
        void wait_on(Entity* p) {
            _wait_queue.push_back(p);
        }
        
        void notify_all(World& w, Coordinate self);
        
    };
    
    
} // namespace wry::sim

#endif /* tile_hpp */
