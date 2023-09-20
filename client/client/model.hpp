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
    
    enum OPCODE : ulong {
        
        OPCODE_NOOP = 0,
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
        
        OPCODE_NOT,       // push(~pop())
        
        OPCODE_AND,       // push(pop() & pop())
        OPCODE_OR,
        OPCODE_XOR,
        
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
    
    struct machine {
        
        ulong _state = 0;
        ulong _location = 0;
        ulong _heading = 0;
        array<ulong> _stack;
        
        void push(ulong x) {
            assert(_stack.empty() || _stack.front());
            if (x || _stack.size())
                _stack.push_back(x);
        }
        
        ulong pop() {
            assert(_stack.empty() || _stack.front());
            ulong result = 0;
            if (!_stack.empty()) {
                result = _stack.back();
                _stack.pop_back();
            }
            return result;
        }
        
        ulong peek() {
            assert(_stack.empty() || _stack.front());
            ulong result = 0;
            if (!_stack.empty()) {
                result = _stack.back();
            }
            return result;
        }
        
        void step(world& w);
        
        
                
    };
    
    
    struct world {
        
        ulong _tick = 0;
        table<ulong, ulong> _tiles;

        std::multimap<ulong, machine*> _waiting_on_time;
        table<ulong, array<machine*>> _waiting_on_location;
        array<machine*> _halted;
        
        ulong get(ulong xy) {
            auto p = _tiles.find(xy);
            if (p == _tiles.end())
                return 0;
            ulong result = p->second;
            assert(result != 0);
            return result;
        }
        
        void set(int x, int y, ulong desired) {
            set(simd_make_int2(x, y), desired);
        }
        
        void set(simd_int2 xy, ulong desired) {
            ulong z = 0;
            memcpy(&z, &xy, 8);
            set(z, desired);
        }
        
        void set(ulong xy, ulong desired) {
            auto p = _tiles.find(xy);
            if (p == _tiles.end()) {
                if (desired != 0) {
                    _tiles.insert(std::make_pair(xy, desired));
                }
            } else {
                if (desired) {
                    p->second = desired;
                } else {
                    _tiles.erase(p);
                }
            }
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
        
        // simulation state
        
        world _world;

        model() {
            _console.emplace_back("\"Behold, a [console]!\"");
            _console.emplace_back("");
            
            
            _world.set(simd_make_int2(0, 1), OPCODE_TURN_RIGHT);
            _world.set(simd_make_int2(2, 1), OPCODE_TURN_RIGHT);
            _world.set(simd_make_int2(2, -1), OPCODE_TURN_RIGHT);
            _world.set(simd_make_int2(0, -1), OPCODE_TURN_RIGHT);
            
            _world._waiting_on_time.emplace(128, new machine);

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
