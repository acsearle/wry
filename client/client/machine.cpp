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
    
    void Machine::notify(World& w) {
        
        Value a = {};
        Value b = {};
        
        switch (_phase) {
                
            case PHASE_TRAVELLING: {
                if (w._tick < _new_time) {
                    // TODO: assert that we are registered at _new_time
                    return;
                }
                _phase = PHASE_WAITING_FOR_OLD;
            } [[fallthrough]];
                
            case PHASE_WAITING_FOR_OLD: {
                assert(_old_location != _new_location);
                Tile& old_tile = w._tiles[_old_location];
                assert(old_tile._occupant == this);
                if (!old_tile._transaction.can_write(w._tick)) {
                    // congestion: retry
                    w._ready.push(this);
                    return;
                }
                old_tile._occupant = nullptr;
                old_tile._transaction.did_write(w._tick);
                w.notify_by_coordinate(_old_location);
                _old_location = _new_location;
                _old_time = w._tick;
                _phase = PHASE_WAITING_FOR_NEW;
            } [[fallthrough]];
                
            case PHASE_WAITING_FOR_NEW: {
                
                Tile& new_tile = w._tiles[_new_location];
                assert(new_tile._occupant == this);
                                
                // check required access to the tile
                
                bool wants_read_new_tile = false;
                bool wants_write_new_tile = false;
                
                switch (_on_arrival) {
                        
                    case OPCODE_SKIP:
                        // does not access the tile
                        break;
                        
                    default:
                        // reads the tile
                        if (!new_tile._transaction.can_read(w._tick)) {
                            w._ready.push(this);
                            return;
                        }
                        wants_read_new_tile = true;
                        break;
                        
                    case OPCODE_STORE:
                    case OPCODE_EXCHANGE:
                        // writes the tile
                        if (!new_tile._transaction.can_write(w._tick)) {
                            w._ready.push(this);
                            return;
                        }
                        wants_write_new_tile = true;
                        break;
                }
                
                // the tile is not subject to conflict
                
                // now we need to work out what other cells are needed
                
                Value new_value;
                i64 next_action = OPCODE_NOOP;
                switch (_on_arrival) {
                    case OPCODE_SKIP:
                    case OPCODE_LOAD:
                    case OPCODE_STORE:
                    case OPCODE_EXCHANGE:
                        break;
                    default:
                        new_value = new_tile._value;
                        if (new_value.discriminant == DISCRIMINANT_OPCODE) {
                            next_action = new_value.value;
                        }
                }

                // bail out for the trivial case of halt
                
                if (next_action == OPCODE_HALT) {
                    // we don't need to do any further processing
                    assert(wants_read_new_tile);
                    assert(!wants_write_new_tile);
                    new_tile._transaction.did_read(w._tick);
                    _on_arrival = OPCODE_NOOP;
                    // don't wait on anything (except, implicitly, this cell)
                    return;
                }
                
                // upgrade our permissions for self-modifying opcodes
                
                switch (next_action) {
                    case OPCODE_FLIP_FLOP:
                    case OPCODE_FLOP_FLIP:
                        if (!new_tile._transaction.can_write(w._tick)) {
                            w._ready.push(this);
                            return;
                        }
                        wants_write_new_tile = true;
                        break;
                    default:
                        break;
                }
                
                // work out where we will go next
                
                i64 new_heading = _heading;
                switch (next_action) {

                    default:
                        // go straight
                        break;
                        
                        // other opcodes may change the direction
                    case OPCODE_TURN_NORTH:
                        new_heading = 0;
                        break;
                    case OPCODE_TURN_EAST:
                        new_heading = 1;
                        break;
                    case OPCODE_TURN_SOUTH:
                        new_heading = 2;
                        break;
                    case OPCODE_TURN_WEST:
                        new_heading = 3;
                        break;
                    case OPCODE_TURN_LEFT:
                    case OPCODE_FLOP_FLIP:
                        --new_heading;
                        break;
                    case OPCODE_TURN_RIGHT:
                    case OPCODE_FLIP_FLOP:
                        ++new_heading;
                        break;
                    case OPCODE_TURN_BACK:
                        new_heading += 2;
                        break;
                    case OPCODE_BRANCH_LEFT:
                        a = peek();
                        if (a.discriminant == DISCRIMINANT_NUMBER)
                            new_heading -= a.value;
                        break;
                    case OPCODE_BRANCH_RIGHT:
                        a = peek();
                        if (a.discriminant == DISCRIMINANT_NUMBER)
                            new_heading += a.value;
                        break;
                    case OPCODE_HEADING_LOAD:
                        a = peek();
                        if (a.discriminant == DISCRIMINANT_NUMBER)
                            new_heading = a.value;
                        break;
                }
                
                Coordinate next_location = _new_location;
                switch (new_heading & 3) {
                    case 0:
                        ++next_location.y;
                        break;
                    case 1:
                        ++next_location.x;
                        break;
                    case 2:
                        --next_location.y;
                        break;
                    case 3:
                        --next_location.x;
                        break;
                }
                
                // try to claim it
                
                auto& next_tile = w._tiles[next_location];                
                if (!next_tile._transaction.can_write(w._tick)) {
                    // conflict; retry
                    w._ready.push(this);
                    return;
                }
                if (next_tile._occupant) {
                    assert(next_tile._occupant != this);
                    // occupied; wait
                    w.wait_on_coordinate(next_location, this);
                    // we choose to interpret this as a failed transaction
                    // which may speed up the release of the other tile
                    return;
                }
                
                // the transaction will now succeed
                
                next_tile._occupant = this;
                next_tile._transaction.did_write(w._tick);
                w.notify_by_coordinate(next_location);
                
                // we need to reload new_tile in case accessing next_tile
                // invalidated the reference
                
                auto& new_tile2 = w._tiles[_new_location];
                
                assert(new_tile2._occupant == this);
                if (wants_write_new_tile) {
                    new_tile2._transaction.did_write(w._tick);
                    w.notify_by_coordinate(_new_location);
                } else if (wants_read_new_tile) {
                    new_tile2._transaction.did_read(w._tick);
                }

                switch (_on_arrival) {
                    case OPCODE_SKIP:
                        break;
                    case OPCODE_LOAD:
                        push(new_tile2._value);
                        break;
                    case OPCODE_STORE:
                        new_tile._value = pop();
                        break;
                    case OPCODE_EXCHANGE:
                        a = pop();
                        if (new_tile2._value.discriminant)
                            push(new_tile2._value);
                        new_tile2._value = a;
                        break;
                    default:
                        new_value = new_tile2._value;
                        if (new_value.discriminant == DISCRIMINANT_NUMBER) {
                            push(new_value);
                        }
                        break;
                }
                
                switch (next_action) {
                        
                    default:
                        // default: no action
                        break;
                        
                    case OPCODE_HALT:
                        // we should have early-out before here
                        abort();
                        
                    case OPCODE_BRANCH_LEFT:
                    case OPCODE_BRANCH_RIGHT:
                    case OPCODE_HEADING_STORE:
                        if (peek().discriminant == DISCRIMINANT_NUMBER)
                            pop();
                        break;
                        
                    case OPCODE_HEADING_LOAD:
                        push({DISCRIMINANT_NUMBER, _heading});
                        break;
                        
                    case OPCODE_DROP:
                        pop();
                        break;
                    case OPCODE_DUPLICATE:
                        push(peek());
                        break;
                    case OPCODE_OVER:
                        std::tie(b, a) = peek2();
                        push(b);
                        break;
                    case OPCODE_SWAP:
                        a = pop();
                        b = pop();
                        push(a);
                        push(b);
                        break;
                        
                        // arithmetic / logic
                        
                    case OPCODE_IS_NOT_ZERO:
                        a = peek();
                        if (a.discriminant == DISCRIMINANT_NUMBER) {
                            a.value = a.value != 0;
                            pop(); push(a);
                        }
                        break;
                        
                    case OPCODE_LOGICAL_NOT:
                        a = peek();
                        if (a.discriminant == DISCRIMINANT_NUMBER) {
                            a.value = !a.value;
                            pop(); push(a);
                        }
                        break;
                    case OPCODE_LOGICAL_AND:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.value = a.value && b.value;
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_LOGICAL_OR:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.value = a.value || b.value;
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_LOGICAL_XOR:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.value = !a.value != !b.value;
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_BITWISE_NOT:
                        a = peek();
                        if (a.discriminant == DISCRIMINANT_NUMBER) {
                            a.value = ~a.value;
                            pop(); push(a);
                        }
                        break;
                    case OPCODE_BITWISE_AND:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.value = a.value & b.value;
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_BITWISE_OR:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.value = a.value | b.value;
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_BITWISE_XOR:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.value = a.value ^ b.value;
                            pop2push1(a);
                        }
                        break;
                        
                    case OPCODE_BITWISE_SPLIT:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            u64 x = a.value & b.value;
                            u64 y = a.value ^ b.value;
                            a.value = x;
                            b.value = y;
                            pop();
                            pop();
                            push(a);
                            push(b);
                        }
                        break;
                        
                    case OPCODE_SHIFT_RIGHT:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.value = a.value >> b.value;
                            pop2push1(a);
                        }
                        break;
                        
                    case OPCODE_POPCOUNT:
                        a = peek();
                        if (a.discriminant == DISCRIMINANT_NUMBER) {
                            a.value = __builtin_popcountll(a.value);
                            pop(); push(a);
                        }
                        break;
                        
                    case OPCODE_NEGATE:
                        a = peek();
                        if (a.discriminant == DISCRIMINANT_NUMBER) {
                            a.value = - a.value;
                            pop(); push(a);
                        }
                        break;
                    case OPCODE_ABS:
                        a = peek();
                        if (a.discriminant == DISCRIMINANT_NUMBER) {
                            a.value = abs((i64) a.value);
                            pop(); push(a);
                        }
                        break;
                    case OPCODE_SIGN:
                        a = peek();
                        if (a.discriminant == DISCRIMINANT_NUMBER) {
                            a.value = (0 < (i64) a.value) - ((i64) a.value < 0);
                            pop(); push(a);
                        }
                        break;
                    case OPCODE_EQUAL:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.value = a.value == b.value;
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_NOT_EQUAL:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.value = a.value != b.value;
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_LESS_THAN:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.value = (i64) a.value < (i64) b.value;
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_GREATER_THAN:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.value = (i64) a.value > (i64) b.value;
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_LESS_THAN_OR_EQUAL_TO:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.value = (i64) a.value <= (i64) b.value;
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_GREATER_THAN_OR_EQUAL_TO:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.value = (i64) a.value >= (i64) b.value;
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_COMPARE:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.value = ((i64) a.value < (i64) b.value) - ((i64) b.value < (i64) a.value);
                            pop2push1(a);
                        }
                        break;
                        
                        
                    case OPCODE_ADD:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.discriminant |= b.discriminant;
                            a.value += b.value;
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_SUBTRACT:
                        std::tie(a, b) = peek2();
                        if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                            a.discriminant |= b.discriminant;
                            a.value -= b.value;
                            pop2push1(a);
                        }
                        break;
                        
                    case OPCODE_FLIP_FLOP:
                        new_tile2._value = { DISCRIMINANT_OPCODE, OPCODE_FLOP_FLIP };
                        break;
                    case OPCODE_FLOP_FLIP:
                        new_tile2._value = { DISCRIMINANT_OPCODE, OPCODE_FLIP_FLOP };
                        break;
                                                
                } // switch (next_action)
                
                _on_arrival = next_action;
                _heading = new_heading;
                _new_location = next_location;
                _old_time = w._tick;
                _new_time = w._tick + 128;
                _phase = PHASE_TRAVELLING;
                w._waiting_on_time.emplace(_new_time, this);
                return;
            }
                
            default:
                __builtin_unreachable();
                
        } // switch (_phase)
        
    }
 
    
} // namespace wry::sim
