//
//  model.cpp
//  client
//
//  Created by Antony Searle on 8/7/2023.
//

#include "model.hpp"

namespace wry {
    
    void machine::step(world& w) {
        
        Value a = {};
        Value b = {};
        
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
                a = w.get(_location);
                if (a.discriminant == DISCRIMINANT_OPCODE) {
                    _state = a.data;
                } else {
                    _state = OPCODE_NOOP;
                }
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
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    _heading += a.data;
                }
                break;
            case OPCODE_BRANCH_LEFT:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    _heading -= a.data;
                }
                break;
            case OPCODE_HEADING_LOAD:
                a = {DISCRIMINANT_NUMBER, _heading};
                push(a);
                break;
            case OPCODE_HEADING_STORE:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    _heading = a.data;
                }
                break;
                
                // manipulate location
                
           
                // manipulate stack
                
            case OPCODE_DROP:
                pop();
                break;
            case OPCODE_DUPLICATE:
                a = peek();
                push(a);
                break;
            case OPCODE_OVER:
                a = pop();
                b = pop();
                push(b);
                push(a);
                push(b);
                break;
            case OPCODE_SWAP:
                b = pop();
                a = pop();
                push(b);
                push(a);
                break;
                
                // arithmetic / logic
                
            case OPCODE_IS_NOT_ZERO:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    a.data = a.data != 0;
                    push(a);
                }
                break;
                
            case OPCODE_LOGICAL_NOT:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    a.data = !a.data;
                    push(a);
                }
                break;
            case OPCODE_LOGICAL_AND:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.data = a.data && b.data;
                    push(a);
                }
                break;
            case OPCODE_LOGICAL_OR:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.data = a.data || b.data;
                    push(a);
                }
                break;
            case OPCODE_LOGICAL_XOR:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.data = !a.data != !b.data;
                    push(a);
                }
                break;
            case OPCODE_BITWISE_NOT:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    a.data = ~a.data;
                    push(a);
                }
                break;
            case OPCODE_BITWISE_AND:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.data = a.data & b.data;
                    push(a);
                }
                break;
            case OPCODE_BITWISE_OR:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.data = a.data | b.data;
                    push(a);
                }
                break;
            case OPCODE_BITWISE_XOR:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.data = a.data ^ b.data;
                    push(a);
                }
                break;
                                
            case OPCODE_BITWISE_SPLIT:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    u64 x = a.data & b.data;
                    u64 y = a.data ^ b.data;
                    a.data = x;
                    b.data = y;
                    push(a);
                    push(b);
                }
                break;
            case OPCODE_POPCOUNT:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    a.data = __builtin_popcountll(a.data);
                    push(a);
                }
                break;
                
            case OPCODE_NEGATE:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    a.data = - a.data;
                    push(a);
                }
                break;
            case OPCODE_ABS:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    a.data = abs((i64) a.data);
                    push(a);
                }
                break;
            case OPCODE_SIGN:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    a.data = (0 < (i64) a.data) - ((i64) a.data < 0);
                    push(a);
                }
                break;
            case OPCODE_EQUAL:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.data = a.data == b.data;
                    push(a);
                }
                break;
            case OPCODE_NOT_EQUAL:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.data = a.data != b.data;
                    push(a);
                }
                break;
            case OPCODE_LESS_THAN:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.data = (i64) a.data < (i64) b.data;
                    push(a);
                }
                break;
            case OPCODE_GREATER_THAN:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.data = (i64) a.data > (i64) b.data;
                    push(a);
                }
                break;
            case OPCODE_LESS_THAN_OR_EQUAL_TO:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.data = (i64) a.data <= (i64) b.data;
                    push(a);
                }
                break;
            case OPCODE_GREATER_THAN_OR_EQUAL_TO:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.data = (i64) a.data >= (i64) b.data;
                    push(a);
                }
                break;
            case OPCODE_COMPARE:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.data = ((i64) a.data < (i64) b.data) - ((i64) b.data < (i64) a.data);
                    push(a);
                }
                break;

                
            case OPCODE_ADD:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.discriminant |= b.discriminant;
                    a.data += b.data;
                    push(a);
                }
                break;
            case OPCODE_SUBTRACT:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.discriminant |= b.discriminant;
                    a.data -= b.data;
                    push(a);
                }
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
