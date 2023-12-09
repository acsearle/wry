//
//  machine.hpp
//  client
//
//  Created by Antony Searle on 20/9/2023.
//

#ifndef machine_hpp
#define machine_hpp

#include "array.hpp"
#include "sim.hpp"
#include "entity.hpp"
#include "debug.hpp"

namespace wry::sim {
    
    struct Machine : Entity {
        
        enum {
            TRAVELLING,
            WAITING_FOR_OLD,
            WAITING_FOR_NEW,
            WAITING_FOR_DESIRED,
        };
        
        i64 _state2 = WAITING_FOR_NEW;
        i64 _on_arrival = OPCODE_NOOP;
        array<Value> _stack;
        
        i64 _heading = 0;
        Coordinate _old_location = { 0, 0 };
        Coordinate _new_location = { 0, 0 };
        Time _old_time = 0;
        Time _new_time = 0;
        Coordinate _desired_location = { 0, 0};

        void push(Value x) {
            _stack.push_back(x);
        }
        
        Value pop() {
            if (!_stack.empty()) {
                Value result{std::move(_stack.back())};
                _stack.pop_back();
                return result;
            } else {
                return Value{};
            }
        }
        
        Value peek() const {
            if (!_stack.empty()) {
                return _stack.back();
            } else {
                return Value{};
            }
        }
        
        std::pair<Value, Value> pop2() {
            Value z = pop();
            Value y = pop();
            return {y, z};
        }
        
        std::pair<Value, Value> peek2() const {
            switch (_stack.size()) {
                case 0:
                    return std::pair<Value, Value>{Value{}, Value{}};
                case 1:
                    return std::pair<Value, Value>{Value{}, _stack.back()};
                default:
                    return std::pair<Value, Value>{_stack._end[-2], _stack._end[-1]};
            }
        }
        
        void pop2push1(Value x) {            
            if (!_stack.empty()) {
                _stack.pop_back();
            }
            if (_stack.empty()) {
                _stack.push_back(std::move(x));
            } else {
                _stack.back() = std::move(x);
            }
        }
        
        virtual void notify(World&);
        
        void _schedule_arrival(World& w);
                
    };
    
} // namespace wry::sim

#endif /* machine_hpp */
