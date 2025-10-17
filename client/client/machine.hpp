//
//  machine.hpp
//  client
//
//  Created by Antony Searle on 20/9/2023.
//

#ifndef machine_hpp
#define machine_hpp

#include <vector>

#include "contiguous_deque.hpp"
#include "sim.hpp"
#include "entity.hpp"
#include "debug.hpp"
#include "HeapArray.hpp"
#include "opcode.hpp"
#include "persistent_stack.hpp"

namespace wry {
    
    inline void garbage_collected_scan(std::vector<Value> const& values) {
        for (Value const& value : values)
            garbage_collected_scan(value);
    }
        
    struct Machine : Entity {
        
        enum {
            PHASE_TRAVELLING,
            PHASE_WAITING_FOR_OLD,
            PHASE_WAITING_FOR_NEW,
        } _phase = PHASE_WAITING_FOR_NEW;
        
        i64 _on_arrival = OPCODE_NOOP;
        
        // Array<Value> _stack;
        // GCArray<Scan<Value>> _stack;
        // std::vector<Value> _stack;
        // TODO: What stack implementation?
        PersistentStack<Value> _stack;
        

        // The _old_* and _new_* states represent the beginning and end states
        // of travelling.  They are used by the visualization as a lerp
        
        i64 _old_heading = HEADING_NORTH;
        i64 _new_heading = HEADING_NORTH;
        Coordinate _old_location = { 0, 0 };
        Coordinate _new_location = { 0, 0 };
        Time _old_time = 0;
        Time _new_time = 0;
        
        Machine* make_mutable_clone() const {
            return new Machine(*this);
        }
        
        void push(Value x) {
            if (!value_is_null(x))
                _stack.push(x);
        }
        
        Value pop() {
            return _stack.pop_else(Value{});
        }
        
        Value peek() const {
            return _stack.peek_else(Value{});
        }
        
        std::pair<Value, Value> pop2() {
            Value z = pop();
            Value y = pop();
            return {y, z};
        }
        
        std::pair<Value, Value> peek2() const {
            std::pair<Value, Value> result = {};
            if (_stack._head) {
                result.second = _stack._head->_payload;
                if (_stack._head->_next)
                    result.first = _stack._head->_next->_payload;
            }
            return result;
        }
        
        void pop2push1(Value x) {
            _stack.pop();
            _stack.pop();
            _stack.push(x);
        }
        
        virtual void notify(TransactionContext* context) const override;

        void _schedule_arrival(World* world);
        
        virtual void _garbage_collected_scan() const override {
            garbage_collected_scan(_stack);
        }
                        
    };
    
} // namespace wry::sim

#endif /* machine_hpp */
