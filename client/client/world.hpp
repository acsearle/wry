//
//  world.hpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#ifndef world_hpp
#define world_hpp

#include <set>
#include <map>
#include <unordered_map>

#include "sim.hpp"
#include "simd.hpp"
#include "table.hpp"
#include "tile.hpp"
#include "utility.hpp"

namespace wry::sim {
    
   
    
    struct World {
        
        // Current simulation time.  Waits are specfied against this clock.
        Time _tick = 0;
        // Time increases by two each step so we can use the timestamp lsb to
        // distinguish read and write states
        
        // Sparse 2D grid of passive items in the world
        Table<Coordinate, Tile> _tiles;
        // TODO: Hash tables may move other items when insertion occurs, either
        //       by rehashing or Robin Hood redistribution. This is a bit
        //       awkward.
        //     - Consider chunking to reduce overheads

        // List of all entities in no particular order
        Array<Entity*> _entities;
        //     - For ownership
        //     - For drawing
        // TODO: Store for coordinate queries
        
        // A fair queue (per time) of entities to wake up at a given time
        std::multimap<Time, Entity*> _waiting_on_time;
        // TODO: node-based container is slowish, consider
        //       - priority_queue<pair<Time, InsertionOrder>, Entity*>
        //       - StablePriorityQueue
        
        
        Table<Coordinate, Array<Entity*>> _waiting_on_coordinate;

        // A fair queue of entities that are ready to execute immediately
        Array<Entity*> _ready;
        // They may originate from
        // - awoken at the current time
        // - awoken by a write to an observed tile
        // - retrying after a conflicted operation
                
        // It seems clear that entities need to wait on many things, and maybe
        // several parameters of the same thing
        //
        // Example: "TURN LEFT", but LEFT is obstructed
        // - Wait for obstruction ahead to clear
        // - Wait for instruction under entity to change
        // - Wait for timeout to take alternative action
        
        // For robustness and simplicity, it seems good to allow spurious
        // wakeups, and multiple appearances of entities in any queue, rather
        // than enforcing uniqueness
        
        // Entities waiting on tiles to change are enqueued on those tiles,
        // which is too much per-tile overhead.  This is a rather awkward
        // many-to-many scenario.
        
        // We can store these instead in
        // Table<Coordinate, Array<Entity*>>
        // on the assumption that the number of entities is small and we don't
        // need high performance removal of them entities

        // As the simulation scales up the world becomes database like so it
        // is fair to consider using some sort of DB, but the use of opcodes
        // to drive complex behaviors including secondary lookups and writes
        // seems ill-fitted to a SQL sort of workflow, unless we can break all
        // possible ops into some sort of recipe of steps
        
        // Algorithm:
        //
        // Increase time
        //
        // Move all entities waiting on the new time into the ready queue
        //
        // For each entity in the ready queue, execute it.  The order of
        // their execution is unspecified but deterministic and fair-ish.
        //
        // An entity will typically
        // - read some things (self, tiles, other entities...)
        // - decide on a course of action, posibly looping back to more reads
        // - maybe write some things
        // - maybe re-enqueue itself to wait on times or writes (or entities?)
        //
        // Transactional and regional restrictions are in place
        // - Reads and writes can only be within some local neighbourhood
        //   - This restricts the speed of light and makes parallelism possible
        // - Entities read and write as-if in atomic transactions.
        // - Entities cannot write to memory a committed transaction has read
        //   from in this step.
        // - Entities cannot read from memory a committed transaction has
        //   written to this step.
        //
        // This means that of the ready Entities
        // - All entities that write will operate as if they went first
        // - All entities that retry will do so because they conflict with a
        //   committed transaction
        // - The set of entities that complete is
        //   - maximal, in the sense that no further transaction can be done
        //   - not optimal, meaning that there may exist a different subset
        //     with more entities that could have comitted.  greedy?  can we
        //     say anything about quality?
        //   - not fair, in the sense that a transaction may fail because it
        //     conflicts with a transaction further down the list
        //   - deterministic, in that any client stepping the same system will
        //     make the same choices - there is no platform defined behavior
        //     at work.  There may be pseudorandomness from a seed in the state.
        //     Note particularly that hash tables, pointer addresses, etc. do
        //     expose platform implementation details so these must be avoided;
        //     we must never rely on hash table iteration order, dor example.
        
        // For a single thread, we can just process the entities in order and
        // be fair
        
        // For multiple threads, we can use locality to partition space.
        // By column, for example, split into columns 0-15 and 16-31, then
        // in two parallel jobs do the work for entities in 4-11 and 20-27
        // (which will only read and write within a radius of 4 tiles).  When
        // both have completed (via an atomic), launch the job for entities in
        // 12-19.  This is anisotropic for "ribbon" bases so we may actually
        // want a slightly more complicated 2d partitioning.  Care is needed
        // to choose data structures that can allow writes in these dijoint
        // regions without corrupting global state; naive hash tables notably
        // can't do this
        
        
        // Reused working array for step()
        Array<Entity*> _step_ready;
                
        void step() {
            
            assert(!(_tick & 1));
            _tick += 2;
                        
            for (;;) {
                if (_waiting_on_time.empty())
                    break;
                auto p = _waiting_on_time.begin();
                assert(p->first >= _tick);
                if (p->first != _tick)
                    break;
                _ready.push_back(p->second);
                _waiting_on_time.erase(p);
            }
            
            assert(_step_ready.empty()); // empty but usually with some capacity to reuse
            using std::swap;
            swap(_ready, _step_ready);
            while (!_step_ready.empty()) {
                Entity* p = _step_ready.front();
                _step_ready.pop_front();
                p->notify(*this);
            }
            
                        
        }
        
    };
    
    inline void Tile::notify_occupant(World &w) {
        if (_occupant) {
            w._ready.push_back(_occupant);
        }
    }
    
    inline void Tile::notify_observers(World &w) {
        while (!_observers.empty()) {
            w._ready.push_back(_observers.front());
            _observers.pop_front();
        }
    }

    
    
} // namespace wry::sim

/*
 
namespace wry {

    // coordinates are stored as a u64 representing the interleaved bits of
    // two i32s.  we can use the low bits to directly index the chunk storage
    // and achieve z-order locality.  we can use the high bits as the hash
    // table key.  we can perform arithmetic by some masking, we do not need to
    // convert back to orthogonal representations.
    
    // however, this is an implementation detail, we should strive to expose it
    // little and test if it makes any performance difference
    
    struct coordinate {
        
        // ~mask selects bits 0, 2, 4, ... which contains x
        //  mask selects bits 1, 3, 5, ... which contains y
        static constexpr uint64_t mask = 0xAAAA'AAAA'AAAA'AAAA;
        
        uint64_t data;
        
        // when an integer is represented as a subsequence of bits, we can
        // perform addition [subtraction] by setting the uninvolved bits to
        // one [zero] so that the carrys [borrows] propagate up to the next
        // representation bit
        
        coordinate xinc() { return coordinate { simd_bitselect((data |  mask) + 1, data, mask) }; }
        coordinate xdec() { return coordinate { simd_bitselect((data & ~mask) - 1, data, mask) }; }
        coordinate yinc() { return coordinate { simd_bitselect(data, (data | ~mask) + 1, mask) }; }
        coordinate ydec() { return coordinate { simd_bitselect(data, (data &  mask) - 1, mask) }; }
        
        coordinate sum(coordinate other) {
            return coordinate {
                simd_bitselect((data |  mask) + (other.data & ~mask),
                          (data | ~mask) + (other.data &  mask),
                          mask)
            };
        }
        
        coordinate difference(coordinate other) {
            return coordinate {
                simd_bitselect((data & ~mask) - (other.data & ~mask),
                          (data &  mask) - (other.data &  mask),
                          mask)
            };
        }
        
        simd_int2 deinterleave() {
            simd_int2 yx = {};
            uint64_t z = morton2_reverse(data);
            std::memcpy(&yx, &z, 8);
            return yx;
        }
        
        static coordinate interleave(simd_int2 yx) {
            uint64_t z = {};
            std::memcpy(&z, &yx, 8);
            return coordinate { z };
        }
        
    }; // coordinate
    
    
    // 16 bytes, 16 x 16 chunk
    union chunk {
        simd_ulong2 m[16][16];
        simd_ulong2 v[256];
        
    };
    
    struct entity;
    
    struct tile {
        
    };
    
    
    struct world {
        
        // world services
        
        // chunk* chunk_by_location(simd_int2 yx);
        // entity* entity_by_location(simd_int2 yx);
        
        simd_ulong2 tile_read(simd_int2 xy);
        void tile_write(simd_int2 xy, simd_ulong2 expected, simd_ulong2 desired, entity* receiver);
        void tile_wait(simd_int2 xy, entity* receiver);
        
        void entity_schedule(entity* receiver);
        void entity_schedule_when(std::uint64_t, entity* receiver);
        
        // like atomics,
        
        
        
        
        
        table<uint64_t, chunk*> _map;
        simd_ulong2& operator()(simd_int2 yx) {
            uint64_t a;
            std::memcpy(&a, &yx, 8);
            uint64_t b = a & 0xFFFF'FFF0'FFFF'FFF0;
            auto& r = _map[b];
            if (!r) {
                auto p = new chunk;
                for (int j = 0; j != 256; ++j) {
                    *(p->v + j) = simd_make_ulong2(std::rand() & 0x5F, 0);
                }
                r = p;
            }
            return r->m[yx.y & 0xF][yx.x & 0xF];
        }
        
    };
    
    
    
    struct simulation {
        
        using time_type = std::int64_t;
        
        using coordinate_type = simd_int2;
        
        void wait_on_time(void*, time_type);
        void wait_on_coordinate(void*, coordinate_type);
        
    };
    
}
 
 inline void Tile::unlock(World& w, Entity* p, Coordinate self) {
 
 // we are executing, so we should be the first lock in the queue
 
 // assert(!_lock_queue.empty());
 if (_lock_queue.empty())
 return;
 
 // remove ourself from the queue
 // we should occur exactly once at front
 
 // assert(_lock_queue.front() == p);
 if (_lock_queue.front() != p) {
 _lock_queue.erase(std::remove_if(_lock_queue.begin(), _lock_queue.end(), [=](auto&& x) {
 return x == p;
 }), _lock_queue.end());
 return;
 }
 _lock_queue.pop_front();
 
 // notify the new front of the queue to run next cycle
 if (_lock_queue.empty())
 return;
 p = _lock_queue.front();
 w._location_locked.emplace_back(self, p);
 }
 
 inline void Tile::notify_all(World& w, Coordinate self) {
 while (!_wait_queue.empty()) {
 Entity* p = _wait_queue.front();
 _wait_queue.pop_front();
 w._location_changed.emplace_back(self, p);
 }
 }
 
 */


#endif /* world_hpp */
