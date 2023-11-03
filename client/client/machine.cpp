//
//  machine.cpp
//  client
//
//  Created by Antony Searle on 20/9/2023.
//

#include "machine.hpp"
#include "world.hpp"
#include "debug.hpp"

namespace wry::sim {
    
    void Machine::_schedule_arrival(World& w) {
        // schedule arrival
        _old_location = _new_location;
        _old_time = w._tick;
        _new_location = _desired_location;
        _new_time = w._tick + 64;
        w._waiting_on_time.emplace(_new_time, (Entity*) this);
    }
    
    void Machine::wake_location_locked(World& w, Coordinate xy) {
        
        //printf("%d %d\n", xy.x, xy.y);

        precondition(_desired_location == xy);
        _schedule_arrival(w);
        
    }
    
    
    void Machine::wake_time_elapsed(World &w, Time now) {
        
        precondition(now == _new_time);
        
        Value a = {};
        Value b = {};
        
        Tile& old_tile = w._tiles[_old_location];
        Tile& new_tile = w._tiles[_new_location];
        
        old_tile.unlock(w, (Entity*) this, _old_location);
        
        // complete the previous operation
        
        switch (_state) {
                
            case OPCODE_HALT:
                // we should not be stepping halted machines
                __builtin_trap();
                return;
                
            case OPCODE_SKIP:
                _state = OPCODE_NOOP;
                break;
                
            case OPCODE_LOAD:
                DUMP(new_tile._value.discriminant);
                DUMP(new_tile._value.value);
                push(new_tile._value);
                _state = OPCODE_NOOP;
                break;
                
            case OPCODE_STORE:
                new_tile._value = pop();
                _state = OPCODE_NOOP;
                break;
                
            case OPCODE_EXCHANGE:
                a = pop();
                push(new_tile._value);
                new_tile._value = a;
                _state = OPCODE_NOOP;
                break;
                
                // for all other states we load the next instruction
                
            default:
                a = new_tile._value;
                if (a.discriminant == DISCRIMINANT_OPCODE) {
                    _state = a.value;
                } else {
                    _state = OPCODE_NOOP;
                }
                break;
                
        }
        
        // begin the next instruction
        
        switch (_state) {
                
            case OPCODE_HALT:
                // we are unscheduled everywhere
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
                    _heading += a.value;
                }
                break;
            case OPCODE_BRANCH_LEFT:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    _heading -= a.value;
                }
                break;
            case OPCODE_HEADING_LOAD:
                a = {DISCRIMINANT_NUMBER, _heading};
                push(a);
                break;
            case OPCODE_HEADING_STORE:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    _heading = a.value;
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
                    a.value = a.value != 0;
                    push(a);
                }
                break;
                
            case OPCODE_LOGICAL_NOT:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    a.value = !a.value;
                    push(a);
                }
                break;
            case OPCODE_LOGICAL_AND:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.value = a.value && b.value;
                    push(a);
                }
                break;
            case OPCODE_LOGICAL_OR:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.value = a.value || b.value;
                    push(a);
                }
                break;
            case OPCODE_LOGICAL_XOR:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.value = !a.value != !b.value;
                    push(a);
                }
                break;
            case OPCODE_BITWISE_NOT:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    a.value = ~a.value;
                    push(a);
                }
                break;
            case OPCODE_BITWISE_AND:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.value = a.value & b.value;
                    push(a);
                }
                break;
            case OPCODE_BITWISE_OR:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.value = a.value | b.value;
                    push(a);
                }
                break;
            case OPCODE_BITWISE_XOR:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.value = a.value ^ b.value;
                    push(a);
                }
                break;
                
            case OPCODE_BITWISE_SPLIT:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    u64 x = a.value & b.value;
                    u64 y = a.value ^ b.value;
                    a.value = x;
                    b.value = y;
                    push(a);
                    push(b);
                }
                break;
            case OPCODE_POPCOUNT:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    a.value = __builtin_popcountll(a.value);
                    push(a);
                }
                break;
                
            case OPCODE_NEGATE:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    a.value = - a.value;
                    push(a);
                }
                break;
            case OPCODE_ABS:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    a.value = abs((i64) a.value);
                    push(a);
                }
                break;
            case OPCODE_SIGN:
                a = pop();
                if (a.discriminant == DISCRIMINANT_NUMBER) {
                    a.value = (0 < (i64) a.value) - ((i64) a.value < 0);
                    push(a);
                }
                break;
            case OPCODE_EQUAL:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.value = a.value == b.value;
                    push(a);
                }
                break;
            case OPCODE_NOT_EQUAL:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.value = a.value != b.value;
                    push(a);
                }
                break;
            case OPCODE_LESS_THAN:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.value = (i64) a.value < (i64) b.value;
                    push(a);
                }
                break;
            case OPCODE_GREATER_THAN:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.value = (i64) a.value > (i64) b.value;
                    push(a);
                }
                break;
            case OPCODE_LESS_THAN_OR_EQUAL_TO:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.value = (i64) a.value <= (i64) b.value;
                    push(a);
                }
                break;
            case OPCODE_GREATER_THAN_OR_EQUAL_TO:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.value = (i64) a.value >= (i64) b.value;
                    push(a);
                } else {
                    b = pop();
                    a = pop();
                }
                break;
            case OPCODE_COMPARE:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.value = ((i64) a.value < (i64) b.value) - ((i64) b.value < (i64) a.value);
                    push(a);
                } else {
                    push(a);
                    push(b);
                }
                break;
                
                
            case OPCODE_ADD:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.discriminant |= b.discriminant;
                    a.value += b.value;
                    push(a);
                } else {
                    push(a);
                    push(b);
                }
                break;
            case OPCODE_SUBTRACT:
                b = pop();
                a = pop();
                if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                    a.discriminant |= b.discriminant;
                    a.value -= b.value;
                    push(a);
                } else {
                    push(a);
                    push(b);
                }
                break;
                
            case OPCODE_FLIP_FLOP:
                ++_heading;
                new_tile._value = { DISCRIMINANT_OPCODE, OPCODE_FLOP_FLIP };
                break;
            case OPCODE_FLOP_FLIP:
                --_heading;
                new_tile._value = { DISCRIMINANT_OPCODE, OPCODE_FLIP_FLOP };
                break;

                
                // no action
                // no action on this location
                
            default:
                break;
                
        }
        
        // move forward
        
        {
            _desired_location = _new_location;
            switch (_heading & 3) {
                case 0:
                    ++_desired_location.y;
                    break;
                case 1:
                    ++_desired_location.x;
                    break;
                case 2:
                    --_desired_location.y;
                    break;
                case 3:
                    --_desired_location.x;
                    break;
            }
            
            auto& desired_tile = w._tiles[_desired_location];
            desired_tile.suspend_for_lock(this, w, _desired_location);
            
        }
        
    }
    
    
    
    void Machine::wake_location_changed(World&, Coordinate) {
        precondition(false);
    }

    
} // namespace wry::sim
