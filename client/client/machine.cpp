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
    
    /*
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
     */
    
    void Machine::notify(World& w) {
        
        Value a = {};
        Value b = {};
        bool did_read_new_tile = false;
        bool did_write_new_tile = false;
        
        for (;;) {
            switch (_state2) {
                case TRAVELLING:
                    if (w._tick < _new_time) {
                        // spurious wake?  keep going
                        w._waiting_on_time.emplace(_new_time, this);
                        return;
                    }
                    _state2 = WAITING_FOR_OLD;
                    [[fallthrough]];
                
                case WAITING_FOR_OLD:
                    
                    // releasing the old location is an unconditional write to
                    // a location we have sole ownership of
                    //
                    // does it ever make sense to be delayed by somebody in the
                    // releasing of the cell?
                    //
                    // if somebody observes it before and after the write, then
                    // what?
                    //
                    //
                    // ordering AB:
                    // tick
                    // A sees cell is owned, flags read, waits on it
                    // B sees cell was read, retries
                    // tick
                    // B releases cell, schedules A
                    // tick
                    // A sees cell is unowned
                    //
                    // ordering BA:
                    // tick
                    // B releases cell, marks written
                    // A can't read cell, schedules A
                    // tick
                    // A sees cell is unowned
                    //
                    // vs (nontransactional)
                    //
                    // ordering AB:
                    // tick
                    // A sees cell is owned, flags read, waits on it
                    // B releases cell despite read flag, schedules A
                    // tick
                    // A sees cell is unowned
                    //
                    // ordering BA:
                    // tick
                    // B releases cell
                    // A sees cell is unowned
                    
                    // perhaps the key insight is that an unconditional release
                    // write changes the cell from "owned" to "inacessible"
                    // so ordering information is only leaked if the reaction
                    // to those two states differs
                    
                    assert(_old_location != _new_location);
                {
                    Tile& old_tile = w._tiles[_old_location];
                    if (old_tile._transaction.can_write(w._tick)) {
                        assert(old_tile._occupant == this);
                        old_tile._occupant = nullptr;
                        old_tile._transaction.did_write(w._tick);
                        while (!old_tile._observers.empty()) {
                            w._ready.push_back(old_tile._observers.front());
                            old_tile._observers.pop_front();
                        }
                        _old_location = _new_location;
                        _old_time = w._tick;
                    } else {
                        // congestion: retry
                        w._ready.push_back(this);
                        return;
                    }
                }
                    _state2 = WAITING_FOR_NEW;
                    [[fallthrough]];
                    
                case WAITING_FOR_NEW: {
                    
                    // starting here, if we want atomic processing, we have to
                    // hold off actually doing anything until we have worked out
                    // what the next location would be, and if we can claim it
                    
                    Tile& new_tile = w._tiles[_new_location];
                    assert(new_tile._occupant == this);
                    
                    i64 tx_on_arrival = _on_arrival;
                    int tx_n_pop = 0;
                    array<Value> tx_pushes;
                    Value tx_write_new_tile;
                    i64 tx_heading = _heading;

                    
                    switch (_on_arrival) {
                            
                            // Handle unusual states where we don't treat the
                            // cell as an opcode
                                                        
                        case OPCODE_SKIP:
                            // ignore the contents of the cell
                            tx_on_arrival = OPCODE_NOOP;
                            break;
                            
                        case OPCODE_LOAD:
                            // copy the cell onto the stack
                            if (!new_tile._transaction.can_read(w._tick)) {
                                w._ready.push_back(this);
                                return;
                            }
                            did_read_new_tile = true;
                            tx_pushes.push_back(new_tile._value);
                            tx_on_arrival = OPCODE_NOOP;
                            break;
                            
                        case OPCODE_STORE:
                            // overwrite the cell
                            if (!new_tile._transaction.can_write(w._tick)) {
                                w._ready.push_back(this);
                                return;
                            }
                            did_write_new_tile = true;
                            tx_write_new_tile = peek();
                            ++tx_n_pop;
                            tx_on_arrival = OPCODE_NOOP;
                            break;
                            
                        case OPCODE_EXCHANGE:
                            // exchange the cell with the top of the stack
                            if (!new_tile._transaction.can_write(w._tick)) {
                                w._ready.push_back(this);
                                return;
                            }
                            did_write_new_tile = true;
                            tx_write_new_tile = peek();
                            ++tx_n_pop;
                            tx_pushes.push_back(new_tile._value);
                            tx_on_arrival = OPCODE_NOOP;
                            break;
                            
                            // for all other states we load the next instruction
                            
                        case OPCODE_HALT:
                            // if we woke up from a HALT state, maybe the HALT
                            // instruction under us was overwritten; we need to
                            // re-read it, which is just the default action
                            [[fallthrough]];
                            
                        default:
                            // read the cell and, if it is an opcode, use it as
                            // the next instruction
                            
                            // if it is NOT an opcode, should we copy it onto
                            // the stack?  That would be concise!
                            // (we need to distinguish between number zero and
                            // other kinds of null in that case)
                            
                            // then we would have 1 2 ADD instead of
                            // LOAD 1 LOAD 2 ADD
                            
                            // instead of default ignore and explicit load, this
                            // gives us default load and explicit SKIP
                            
                            if (!new_tile._transaction.can_read(w._tick)) {
                                w._ready.push_back(this);
                                return;
                            }
                            did_read_new_tile = true;
                            a = new_tile._value;
                            if (a.discriminant == DISCRIMINANT_OPCODE) {
                                tx_on_arrival = a.value;
                            } else {
                                tx_on_arrival = OPCODE_NOOP;
                            }
                            break;
                    } // switch (_on_arrival)
                    
                    switch (tx_on_arrival) {
                            
                        case OPCODE_HALT:
                            break;
                            
                            // manipulate heading
                            
                        case OPCODE_TURN_NORTH:
                            tx_heading = 0;
                            break;
                        case OPCODE_TURN_EAST:
                            tx_heading = 1;
                            break;
                        case OPCODE_TURN_SOUTH:
                            tx_heading = 2;
                            break;
                        case OPCODE_TURN_WEST:
                            tx_heading = 3;
                            break;
                        case OPCODE_TURN_RIGHT:
                            ++tx_heading;
                            break;
                        case OPCODE_TURN_LEFT:
                            --tx_heading;
                            break;
                        case OPCODE_TURN_BACK:
                            tx_heading += 2;
                            break;
                        case OPCODE_BRANCH_RIGHT:
                            a = peek();
                            ++tx_n_pop;
                            if (a.discriminant == DISCRIMINANT_NUMBER) {
                                tx_heading += a.value;
                            }
                            break;
                        case OPCODE_BRANCH_LEFT:
                            a = peek();
                            ++tx_n_pop;
                            if (a.discriminant == DISCRIMINANT_NUMBER) {
                                tx_heading -= a.value;
                            }
                            break;
                        case OPCODE_HEADING_LOAD:
                            a = {DISCRIMINANT_NUMBER, _heading};
                            tx_pushes.push_back(a);
                            break;
                        case OPCODE_HEADING_STORE:
                            a = peek();
                            ++tx_n_pop;
                            if (a.discriminant == DISCRIMINANT_NUMBER) {
                                tx_heading = a.value;
                            }
                            break;
                            
                            // manipulate location
                            
                            
                            // manipulate stack
                            
                        case OPCODE_DROP:
                            a = peek();
                            ++tx_n_pop;
                            break;
                        case OPCODE_DUPLICATE:
                            a = peek();
                            ++tx_n_pop;
                            tx_pushes.push_back(a);
                            tx_pushes.push_back(a);
                            break;
                        case OPCODE_OVER:
                            std::tie(b, a) = peek2();
                            tx_n_pop += 2;
                            tx_pushes.push_back(b);
                            tx_pushes.push_back(a);
                            tx_pushes.push_back(b);
                            break;
                        case OPCODE_SWAP:
                            std::tie(b, a) = peek2();
                            tx_n_pop += 2;
                            tx_pushes.push_back(a);
                            tx_pushes.push_back(b);
                            break;
                            
                            // arithmetic / logic
                            
                        case OPCODE_IS_NOT_ZERO:
                            a = peek();
                            ++tx_n_pop;
                            if (a.discriminant == DISCRIMINANT_NUMBER) {
                                a.value = a.value != 0;
                                tx_pushes.push_back(a);
                            }
                            break;
                            
                        case OPCODE_LOGICAL_NOT:
                            a = peek();
                            ++tx_n_pop;
                            if (a.discriminant == DISCRIMINANT_NUMBER) {
                                a.value = !a.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_LOGICAL_AND:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.value = a.value && b.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_LOGICAL_OR:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.value = a.value || b.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_LOGICAL_XOR:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.value = !a.value != !b.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_BITWISE_NOT:
                            a = peek();
                            ++tx_n_pop;
                            if (a.discriminant == DISCRIMINANT_NUMBER) {
                                a.value = ~a.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_BITWISE_AND:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.value = a.value & b.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_BITWISE_OR:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.value = a.value | b.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_BITWISE_XOR:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.value = a.value ^ b.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                            
                        case OPCODE_BITWISE_SPLIT:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                u64 x = a.value & b.value;
                                u64 y = a.value ^ b.value;
                                a.value = x;
                                b.value = y;
                                tx_pushes.push_back(a);
                                tx_pushes.push_back(b);
                            }
                            break;
                            
                        case OPCODE_SHIFT_RIGHT:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.value = a.value >> b.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                            
                        case OPCODE_POPCOUNT:
                            a = peek();
                            ++tx_n_pop;
                            if (a.discriminant == DISCRIMINANT_NUMBER) {
                                a.value = __builtin_popcountll(a.value);
                                tx_pushes.push_back(a);
                            }
                            break;
                            
                        case OPCODE_NEGATE:
                            a = peek();
                            ++tx_n_pop;
                            if (a.discriminant == DISCRIMINANT_NUMBER) {
                                a.value = - a.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_ABS:
                            a = peek();
                            ++tx_n_pop;
                            if (a.discriminant == DISCRIMINANT_NUMBER) {
                                a.value = abs((i64) a.value);
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_SIGN:
                            a = peek();
                            ++tx_n_pop;
                            if (a.discriminant == DISCRIMINANT_NUMBER) {
                                a.value = (0 < (i64) a.value) - ((i64) a.value < 0);
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_EQUAL:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.value = a.value == b.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_NOT_EQUAL:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.value = a.value != b.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_LESS_THAN:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.value = (i64) a.value < (i64) b.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_GREATER_THAN:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.value = (i64) a.value > (i64) b.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_LESS_THAN_OR_EQUAL_TO:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.value = (i64) a.value <= (i64) b.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_GREATER_THAN_OR_EQUAL_TO:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.value = (i64) a.value >= (i64) b.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_COMPARE:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.value = ((i64) a.value < (i64) b.value) - ((i64) b.value < (i64) a.value);
                                tx_pushes.push_back(a);
                            }
                            break;
                            
                            
                        case OPCODE_ADD:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.discriminant |= b.discriminant;
                                a.value += b.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                        case OPCODE_SUBTRACT:
                            std::tie(a, b) = peek2();
                            tx_n_pop += 2;
                            if ((a.discriminant | b.discriminant) == DISCRIMINANT_NUMBER) {
                                a.discriminant |= b.discriminant;
                                a.value -= b.value;
                                tx_pushes.push_back(a);
                            }
                            break;
                            
                        case OPCODE_FLIP_FLOP:
                            if (!new_tile._transaction.can_write(w._tick)) {
                                w._ready.push_back(this);
                                return;
                            }
                            ++tx_heading;
                            tx_write_new_tile = { DISCRIMINANT_OPCODE, OPCODE_FLOP_FLIP };
                            did_write_new_tile = true;
                            break;
                        case OPCODE_FLOP_FLIP:
                            if (!new_tile._transaction.can_write(w._tick)) {
                                w._ready.push_back(this);
                                return;
                            }
                            --tx_heading;
                            tx_write_new_tile = { DISCRIMINANT_OPCODE, OPCODE_FLIP_FLOP };
                            did_write_new_tile = true;
                            break;
                            
                        default:
                            // no action
                            break;
                            
                    } // switch (tx_on_arrival)
                    
                    if (tx_on_arrival != OPCODE_HALT) {
                        
                        auto tx_desired_location = _new_location;
                        switch (tx_heading & 3) {
                            case 0:
                                ++tx_desired_location.y;
                                break;
                            case 1:
                                ++tx_desired_location.x;
                                break;
                            case 2:
                                --tx_desired_location.y;
                                break;
                            case 3:
                                --tx_desired_location.x;
                                break;
                        }
                        
                        auto& desired_tile = w._tiles[tx_desired_location];
                        // desired_tile.suspend_for_lock(this, w, _desired_location);
                        
                        if (!desired_tile._transaction.can_write(w._tick)) {
                            // conflict: retry
                            w._ready.push_back(this);
                            return;
                        }
                        if (desired_tile._occupant) {
                            assert(desired_tile._occupant != this);
                            // busy: wait
                            desired_tile._observers.push_back(this);
                            // desired_tile._transaction.did_read(w._tick);
                            return;
                        }
                        
                        
                        // begin committing
                        
                        
                        // available, occupy
                        assert(!desired_tile._occupant);
                        desired_tile._occupant = this;
                        desired_tile._transaction.did_write(w._tick);
                        while (!desired_tile._observers.empty()) {
                            w._ready.push_back(desired_tile._observers.front());
                            desired_tile._observers.pop_front();
                        }
                        
                        _desired_location = tx_desired_location;
                        
                    }
                    
                    
                    
                    // new_tile may be dangling here?
                    auto& new_tile2 = w._tiles[_new_location];
                    
                    if (did_write_new_tile) {
                        new_tile2._value = tx_write_new_tile;
                        new_tile2._transaction.did_write(w._tick);
                        while (!new_tile2._observers.empty()) {
                            w._ready.push_back(new_tile2._observers.front());
                            new_tile2._observers.pop_front();
                        }
                    } else if (did_read_new_tile) {
                        new_tile2._transaction.did_read(w._tick);
                    }
                    
                    // commit writes to machine state
                    
                    _on_arrival = tx_on_arrival;
                    _heading = tx_heading;
                    _stack.erase(_stack.end() - tx_n_pop, _stack.end());
                    _stack.insert(_stack.end(), tx_pushes.begin(), tx_pushes.end());
                    
                    if (_on_arrival == OPCODE_HALT) {
                        _state2 = WAITING_FOR_NEW;
                        return;
                    }
                        
                                        
                    // start moving
                    _new_location = _desired_location;
                    _new_time = w._tick + 128;
                    _old_time = w._tick;
                    w._waiting_on_time.emplace(_new_time, this);
                    _state2 = TRAVELLING;
                    return;
                }
                default:
                    abort();
            } // switch (_state2)
        };
        
        
        // This method performs multiple transactions in order ; they might all
        // complete in one tick or be spread out over many as the old, new and
        // next cell are contested
        
        // We should break these into methods, and we should keep track of
        // where we are in the sequence rather than trying to infer it
        
        // - release old cell
        // - read new cell opcode and do something
        //   - multiple times if exit is blocked and value changes under us
        //   - how do we handle
        //     - same value was written under us
        //     - ABA change before we can react to it
        //     - some operations are idempotent, some are not
        //     - operations that don't change the heading?
        //   - is the operation atomic with claiming the desired cell, i.e. we
        //     commit to doing something as we leave?  this seems counter-intuitive
        //     espectailly since the operation we perform determines the output
        // - claim desired cell and begin moving into it
        
        // Awoken while moving?
                        
        // Awoken at new location?
        
        
        
        // We are here because
        // - we arrived at new_Location
        // - [new_location] changed while we were stuck on it
        // - [desired_location] changed while we were waiting to enter it
        
        // Complete the previous operation
        

        // begin the next instruction
        

        
        
        // move forward
        
        
        
    }
    
    
    
    /*
    void Machine::wake_location_changed(World&, Coordinate) {
        precondition(false);
    }
     */

    
} // namespace wry::sim
