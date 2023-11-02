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
        
        i64 _state = OPCODE_NOOP;
        array<Value> _stack;
        
        i64 _heading = 0;
        Coordinate _old_location = { 0, 0 };
        Coordinate _new_location = { 0, 0 };
        Time _old_time = 0;
        Time _new_time = 0;
        Coordinate _desired_location = { 0, 0};

      
        
        
        void push(Value x) {
            DUMP(x.discriminant);
            DUMP(x.value);
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
        
        virtual void wake_location_locked(World&, Coordinate);
        virtual void wake_location_changed(World&, Coordinate);
        virtual void wake_time_elapsed(World&, Time);
        
        void _schedule_arrival(World& w);
                
    };
    
} // namespace wry::sim

#endif /* machine_hpp */
