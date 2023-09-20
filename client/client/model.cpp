//
//  model.cpp
//  client
//
//  Created by Antony Searle on 8/7/2023.
//

#include "model.hpp"

namespace wry {
    
    void machine::step(world& w) {
        
        ulong a = 0, b = 0;
        long c = 0, d = 0;
        
        // complete the current operation
        
        switch (_state) {
                
            case OPCODE_HALT:
                // we should not be stepping halted machines
                __builtin_trap();
                return;
                
            case OPCODE_SKIP:
                _state = OPCODE_NOOP;
                break;
                
            case OPCODE_LOAD:
                push(w.get(_location));
                _state = OPCODE_NOOP;
                break;
                
            case OPCODE_STORE:
                w.set(_location, pop());
                _state = OPCODE_NOOP;
                break;
                
            case OPCODE_EXCHANGE:
                a = pop();
                push(w.get(_location));
                w.set(_location, a);
                _state = OPCODE_NOOP;
                break;
                
                // for all other states we load the next instruction
                
            default:
                _state = w.get(_location);
                break;
                
        }
        
        // begin the next instruction
        
        switch (_state) {
                
            case OPCODE_HALT:
                w._halted.push_back(this);
                return;
                
                // manipulate heading
                
            case OPCODE_TURN_NORTH:
                _heading = 0;
                break;
            case OPCODE_TURN_EAST:
                _heading = 1;
                break;
            case OPCODE_TURN_SOUTH:
                _heading = 2;
                break;
            case OPCODE_TURN_WEST:
                _heading = 3;
                break;
            case OPCODE_TURN_RIGHT:
                ++_heading;
                break;
            case OPCODE_TURN_LEFT:
                --_heading;
                break;
            case OPCODE_TURN_BACK:
                _heading += 2;
                break;
            case OPCODE_BRANCH_RIGHT:
                a = pop();
                _heading += a;
                break;
            case OPCODE_BRANCH_LEFT:
                a = pop();
                _heading -= a;
                break;
            case OPCODE_HEADING_LOAD:
                a = _heading;
                push(a);
                break;
            case OPCODE_HEADING_STORE:
                a = pop();
                _heading = a;
                break;
                
                // manipulate location
                
            case OPCODE_LOCATION_LOAD:
                a = _location;
                push(a);
                break;
            case OPCODE_LOCATION_STORE:
                a = pop();
                _location = a;
                return;
                
                // manipulate stack
                
            case OPCODE_DROP:
                pop();
                break;
            case OPCODE_DUPLICATE:
                a = peek();
                push(a);
                break;
            case OPCODE_OVER:
                a = 0;
                if (_stack.size() >= 2)
                    a = *(_stack.end() - 2);
                push(a);
                break;
            case OPCODE_SWAP:
                b = pop();
                a = pop();
                push(b);
                push(a);
                break;
                
                // arithmetic / logic
                
            case OPCODE_NEGATE:
                a = pop();
                a = -a;
                push(a);
                break;
            case OPCODE_ABS:
                c = (long) pop();
                c = abs(c);
                push(c);
                break;
            case OPCODE_SIGN:
                c = (long) pop();
                c = (0 < c) - (c < 0);
                push(c);
                break;
            case OPCODE_EQUAL:
                d = pop();
                c = pop();
                push(c == d);
                break;
            case OPCODE_NOT_EQUAL:
                d = pop();
                c = pop();
                push(c != d);
                break;
            case OPCODE_LESS_THAN:
                d = pop();
                c = pop();
                push(c < d);
                break;
            case OPCODE_GREATER_THAN:
                d = pop();
                c = pop();
                push(c > d);
                break;
            case OPCODE_LESS_THAN_OR_EQUAL_TO:
                d = pop();
                c = pop();
                push(c <= d);
                break;
            case OPCODE_GREATER_THAN_OR_EQUAL_TO:
                d = pop();
                c = pop();
                push(c >= d);
                break;
            case OPCODE_COMPARE:
                d = pop();
                c = pop();
                push((c < d) - (d < c));
                break;
                
                // no action
                // no action on this location
                
            default:
                break;
                
        }
        
        // move forward
        
        {
            simd_int2 xy;
            memcpy(&xy, &_location, 8);
            switch (_heading & 3) {
                case 0:
                    ++xy.y;
                    break;
                case 1:
                    ++xy.x;
                    break;
                case 2:
                    --xy.y;
                    break;
                case 3:
                    --xy.x;
                    break;
            }
            memcpy(&_location, &xy, 8);
        }
        
        // schedule arrival
        
        w._waiting_on_time.emplace(w._tick + 64, this);
        
        
    }

} // namespace wry
