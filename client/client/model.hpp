//
//  model.hpp
//  client
//
//  Created by Antony Searle on 8/7/2023.
//

#ifndef model_hpp
#define model_hpp

#include <unordered_map>
#include <memory>
#include <mutex>


#include "array.hpp"
#include "entity.hpp"
#include "hash.hpp"
#include "machine.hpp"
#include "simd.hpp"
#include "string.hpp"
#include "table.hpp"
#include "world.hpp"

namespace wry {
    
    struct machine;
    struct world;
    
    // we can't use simd_int2 because the equality operator is vectorized
    
    struct coordinate {
        i32 x;
        i32 y;
        bool operator==(const coordinate&) const = default;
    };
    
    struct Value {
        i64 discriminant;
        i64 data;
        
        explicit operator bool() const { return discriminant; }
        bool operator!() const { return !discriminant; }

        
    };
    
    enum DISCRIMINANT : i64 {
        DISCRIMINANT_ZERO   = 0, // empty, null etc.
        DISCRIMINANT_OPCODE = 1,
        DISCRIMINANT_NUMBER = 2,
    };
    
    enum OPCODE : i64 {
        
        OPCODE_NOOP,
        OPCODE_SKIP,
        OPCODE_HALT,
        
        OPCODE_TURN_NORTH, // _heading:2 = 0   // clockwise
        OPCODE_TURN_EAST,  // _heading:2 = 1
        OPCODE_TURN_SOUTH, // _heading:2 = 2
        OPCODE_TURN_WEST,  // _heading:2 = 3
        
        OPCODE_TURN_RIGHT, // ++_heading
        OPCODE_TURN_LEFT,  // --_heading
        OPCODE_TURN_BACK,  // _heading += 2;
        
        OPCODE_BRANCH_RIGHT, // _heading += pop()
        OPCODE_BRANCH_LEFT, // _heading -= pop();
        
        OPCODE_LOAD,     // push([_location])
        OPCODE_STORE,    // [_location] = pop()
        OPCODE_EXCHANGE, // push(exchange([_location], pop())
        
        OPCODE_HEADING_LOAD, // push(_heading)
        OPCODE_HEADING_STORE, // _heading = pop()
        
        OPCODE_LOCATION_LOAD,  // push(_location)
        OPCODE_LOCATION_STORE, // _location = pop()
        
        OPCODE_DROP,      // pop()
        OPCODE_DUPLICATE, // a = pop(); push(a); push(a)
        OPCODE_OVER,      // a = pop(); b = pop(); push(b); push(a); push(b);
        OPCODE_SWAP,      // a = pop(); b = pop(); push(a); push(b);
                
        OPCODE_IS_ZERO,           // 010
        OPCODE_IS_POSITIVE,       // 001
        OPCODE_IS_NEGATIVE,       // 100
        OPCODE_IS_NOT_ZERO,       // 101
        OPCODE_IS_NOT_POSITIVE,   // 110
        OPCODE_IS_NOT_NEGATIVE,   // 011
        
        OPCODE_LOGICAL_NOT,
        OPCODE_LOGICAL_AND,
        OPCODE_LOGICAL_OR,
        OPCODE_LOGICAL_XOR,
        
        OPCODE_BITWISE_NOT,
        OPCODE_BITWISE_AND,
        OPCODE_BITWISE_OR,
        OPCODE_BITWISE_XOR,
        
        OPCODE_BITWISE_SPLIT,
        OPCODE_POPCOUNT,
        
        OPCODE_NEGATE,    // push(-pop())
        OPCODE_ABS,       // push(abs(pop()))
        OPCODE_SIGN,      // push(sign(pop()))
        
        OPCODE_ADD,       // a = pop(); b = pop(); push(b + a)
        OPCODE_SUBTRACT,  // a = pop(); b = pop(); push(b - a)
                
        OPCODE_EQUAL,         // a = pop(); b = pop(); push(b == a)
        OPCODE_NOT_EQUAL,     // a = pop(); b = pop(); push(b != a)
        OPCODE_LESS_THAN,     // a = pop(); b = pop(); push(b - a)
        OPCODE_GREATER_THAN,
        OPCODE_LESS_THAN_OR_EQUAL_TO,
        OPCODE_GREATER_THAN_OR_EQUAL_TO, // a <= b
        OPCODE_COMPARE, // sign(a - b)
        
    };
    
    enum HEADING : u64 {
        HEADING_NORTH = 0,
        HEADING_EAST = 1,
        HEADING_SOUTH = 2,
        HEADING_WEST = 3,
        HAEDING_MASK = 3
    };
        
    inline ulong hash(coordinate xy) {
        return hash_combine(&xy, sizeof(xy));
    }
    
    struct world;
    
    struct machine {
        
        coordinate _location = { 0, 0};
        i64 _heading = 0;
        array<Value> _stack;
        i64 _state = OPCODE_NOOP;
        coordinate _previous_location = { 0, 0 };
        coordinate _next_location = { 0, 0 };

        array<coordinate> _lock_enqueued;
        array<coordinate> _wait_enqueued;
        
        struct cold {
            u64 _persistent_id;
            string _name;
            // ...
        };
        cold* _cold = nullptr;
        
        
        
        void push(Value x) {
            if (x)
                _stack.push_back(x);
        }
        
        Value pop() {
            Value result = {};
            if (!_stack.empty()) {
                result = _stack.back();
                _stack.pop_back();
            }
            return result;
        }
        
        Value peek() {
            Value result = {};
            if (!_stack.empty()) {
                result = _stack.back();
            }
            return result;
        }
        
        std::pair<Value, Value> pop2() {
            Value z = pop();
            Value y = pop();
            return {y, z};
        }
        
        void step(world& w);
        
        void notify_of(coordinate xy) {
            auto pos = std::find(_wait_enqueued.begin(), _wait_enqueued.end(), xy);
            assert(pos != _wait_enqueued.end());
            _wait_enqueued.erase(pos);
            _lock_enqueued.push_back(xy);
        }
        
        
                
    };
    
    struct tile {
                
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
        
        
        // todo: tiles (or tile chunks) should use basic_table and customize
        // their layout
        
        Value _value;
        array<machine*> _lock_queue; // mutex
        array<machine*> _wait_queue; // condition variable
        
        bool is_locked() const {
            return !_lock_queue.empty();
        }

        bool _not_in_queue(machine* p) const {
            for (machine* q : _lock_queue)
                if (q == p)
                    return false;
            return true;
        }
        
        bool enqueue(machine* p) {
            assert(p);
            assert(_not_in_queue(p));
            bool was_empty = _lock_queue.empty();
            _lock_queue.push_back(p);
            return was_empty;
        }
        
        bool try_lock(machine* p) {
            assert(p);
            assert(_not_in_queue(p));
            bool was_empty = _lock_queue.empty();
            if (was_empty)
                _lock_queue.push_back(p);
            return was_empty;
        }
        
        void unlock(machine* p, world& w);
        
        void wait_on(machine* p) {
            _wait_queue.push_back(p);
        }
        
        void notify_all(coordinate self) {
            while (!_wait_queue.empty()) {
                // todo: should we have multiple CVs for different conditions?
                machine* p = _wait_queue.front();
                _wait_queue.pop_front();
                _lock_queue.push_back(p);
                p->notify_of(self);
            }
        }
        
    };
    
    
    struct world {
        
        ulong _tick = 0;
        table<coordinate, tile> _tiles;

        std::multimap<ulong, machine*> _waiting_on_time;
        array<machine*> _halted;
        array<machine*> _waiting_on_locks;
        
        Value get(coordinate xy) {
            return _tiles[xy]._value;
        }
        
        void set(coordinate xy, Value value) {
            _tiles[xy]._value = value;
        }
                
        void step() {
            
            ++_tick;
            
            for (;;) {
                auto p = _waiting_on_time.begin();
                if (p == _waiting_on_time.end())
                    break;
                assert(p->first >= _tick);
                if (p->first != _tick)
                    break;
                machine* q = p->second;
                _waiting_on_time.erase(p);
                assert(q);
                q->step(*this);
                // q may reschedule itself at some later time
                // which invalidates the iterator for some containers
            }
                        
        }
        
    };
    
    
    inline void tile::unlock(machine* p, world& w) {
        
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
        w._waiting_on_time.emplace(w._tick + 1, p);
    }
   

    struct model {
        
        // local state
        
        std::mutex _mutex;

        array<string> _console;
        std::multimap<std::chrono::steady_clock::time_point, string> _logs;
        
        bool _console_active = false;        
        bool _show_jacobian = false;
        bool _show_points = false;
        bool _show_wireframe = false;

        simd_float2 _looking_at = {};
        simd_float2 _mouse = {};
        simd_float4 _mouse4 = {};
        
        // simulation state
        
        world _world;

        model() {
            
            _console.emplace_back("\"Behold, a [console]!\"");
            _console.emplace_back("");
            
            
            _world.set({0, 1}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({1, 1}, {DISCRIMINANT_OPCODE, OPCODE_LOAD});
            _world.set({2, 1}, {DISCRIMINANT_NUMBER, 1});
            _world.set({3, 1}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({3, 0}, {DISCRIMINANT_OPCODE, OPCODE_ADD});
            _world.set({3, -1}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({0, -1}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            // _world.set(simd_make_int2(1, -1), OPCODE_HALT);
            
            _world._waiting_on_time.emplace(64, new machine);
            _world._waiting_on_time.emplace(64, new machine);
            _world._waiting_on_time.emplace(64, new machine);
            _world._waiting_on_time.emplace(64, new machine);
            _world._waiting_on_time.emplace(64, new machine);
            _world._waiting_on_time.emplace(64, new machine);
            _world._waiting_on_time.emplace(64, new machine);


        }
        
        ~model() {
            fprintf(stderr, "%s\n", __PRETTY_FUNCTION__);
        }
        
        void append_log(string_view v,
                        std::chrono::steady_clock::duration endurance = std::chrono::seconds(5)) {
            _logs.emplace(std::chrono::steady_clock::now() + endurance, v);
        }
        
        
        
    };
    
} // namespace wry

#endif /* model_hpp */
