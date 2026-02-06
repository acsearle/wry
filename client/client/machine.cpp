//
//  machine.cpp
//  client
//
//  Created by Antony Searle on 20/9/2023.
//

#include "machine.hpp"
#include "world.hpp"
#include "debug.hpp"
#include "transaction.hpp"

namespace wry {
    
    void Machine::notify(TransactionContext* context) const {

        Transaction* tx = Transaction::make(context, this, 10);

        Value a = {};
        Value b = {};

        Machine* new_this = make_mutable_clone();
        assert(new_this->_entity_id == _entity_id);
        
        //printf("Machine::notify()\n");

        switch (_phase) {
                
            case PHASE_TRAVELLING: {
                // Machine is travelling from old location to new location
                assert(_old_location != _new_location);
                if ((context->_world->_time - _new_time) < 0) {
                    // This was a spurious wakeup
                    printf("EntityID %lld experienced spurious wakeup\n", _entity_id.data);
                    return;
                }
                new_this->_phase = PHASE_WAITING_FOR_OLD;
            } [[fallthrough]];
                
            case PHASE_WAITING_FOR_OLD: {
                assert(_old_location != _new_location);
                EntityID occupant = {};
                (void) tx->try_read_entity_id_for_coordinate(_old_location, occupant);
                assert(occupant == this->_entity_id);
                tx->write_entity_id_for_coordinate(_old_location, EntityID{0});
                new_this->_old_time = _new_time;
                new_this->_old_location = _new_location;
                new_this->_old_heading = _new_heading;
                new_this->_phase = PHASE_WAITING_FOR_NEW;
            } [[fallthrough]];
                
            case PHASE_WAITING_FOR_NEW: {
                
                assert(new_this->_old_location == _new_location);
                
                // TODO: at the moment we have _on_arrivial and next action to
                // coordinate stuff.  Can we instead use the top of the stack
                // itself as the instruction slot and current machine state?
                
                EntityID occupant = {};
                (void) tx->try_read_entity_id_for_coordinate(_new_location, occupant);
                assert(occupant == this->_entity_id);

                // now we need to work out what other cells are needed
                
                Value new_value = {};
                i64 next_action = OPCODE_NOOP;
                switch (_on_arrival) {
                    case OPCODE_SKIP:
                        // we ignore the current value entirely
                        break;
                    case OPCODE_STORE:
                        // ignore the current value, but STORE should eventually
                        // check that we can actually ovewrite the value (it's
                        // not "matter", or read only, or whatever)
                        break;
                    case OPCODE_LOAD:
                    case OPCODE_EXCHANGE:
                        // we load [_new_location] but don't execute it
                        // new_value = peek_world_coordinate_value(world, _new_location);
                        (void) tx->try_read_value_for_coordinate(_new_location, new_value);
                        break;
                    default:
                        // we load [_new_location] and may execute it
                        (void) tx->try_read_value_for_coordinate(_new_location, new_value);
                        if (new_value.is_opcode())
                            next_action = new_value.as_opcode();
                        break;
                }
            
                // bail out for the trivial case of halt
                
                if (next_action == OPCODE_HALT) {
                    // we don't need to do any further processing
                    new_this->_on_arrival = OPCODE_NOOP;
                    tx->write_entity_for_entity_id(this->_entity_id, new_this);
                    tx->wait_on_value_for_coordinate(new_this->_new_location);
                    printf("EntityID %lld proposes to HALT\n", _entity_id.data);
                    return;
                }
                // work out where we will go next
                                
                i64 next_heading = _new_heading;
                switch (next_action) {

                    default:
                        // go straight
                        break;
                        
                        // other opcodes may change the direction
                    case OPCODE_TURN_NORTH:
                        next_heading = 0;
                        break;
                    case OPCODE_TURN_EAST:
                        next_heading = 1;
                        break;
                    case OPCODE_TURN_SOUTH:
                        next_heading = 2;
                        break;
                    case OPCODE_TURN_WEST:
                        next_heading = 3;
                        break;
                    case OPCODE_TURN_LEFT:
                    case OPCODE_FLOP_FLIP:
                        --next_heading;
                        break;
                    case OPCODE_TURN_RIGHT:
                    case OPCODE_FLIP_FLOP:
                        ++next_heading;
                        break;
                    case OPCODE_TURN_BACK:
                        next_heading += 2;
                        break;
                    case OPCODE_BRANCH_LEFT:
                        a = peek();
                        if (a.is_int64_t())
                            next_heading -= a.as_int64_t();
                        break;
                    case OPCODE_BRANCH_RIGHT:
                        a = peek();
                        if (a.is_int64_t())
                            next_heading += a.as_int64_t();
                        break;
                    case OPCODE_HEADING_LOAD:
                        a = peek();
                        if (a.is_int64_t())
                            next_heading = a.as_int64_t();
                        break;
                }
                
                Coordinate next_location = _new_location;
                switch (next_heading & 3) {
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
                
                occupant = {};
                (void) tx->try_read_entity_id_for_coordinate(next_location, occupant);
                tx->write_entity_for_entity_id(this->_entity_id, new_this);
                if (occupant) {
                    assert(occupant != _entity_id);
                    // occupied; wait
                    tx->wait_on_entity_id_for_coordinate(next_location);
                    printf("EntityID %lld proposes to WAIT on next_location\n", _entity_id.data);
                    tx->describe();
                    return;
                }
                tx->write_entity_id_for_coordinate(next_location, this->_entity_id);
                
                                
                

                switch (_on_arrival) {
                    case OPCODE_SKIP:
                        break;
                    case OPCODE_LOAD:
                        new_this->push(new_value);
                        break;
                    case OPCODE_STORE:
                        // assert(wants_write_new_tile);
                        // set_world_coordinate_value(world, _new_location, pop());
                        tx->write_value_for_coordinate(_new_location, new_this->pop());
                        break;
                    case OPCODE_EXCHANGE:
                        a = new_this->pop();
                        // TODO: should push(...) itself discard nothings, always?
                        new_this->push(new_value);
                        tx->write_value_for_coordinate(_new_location, a);
                        break;
                    default:
                        // To avoid explicit loads everywhere, when we run over
                        // something that
                        // - is not an opcode
                        // - is copyable, aka immaterial, sybmbolic, numeric?
                        // we pick it up.  Good idea?
                        if (new_value.is_int64_t()) {
                            new_this->push(new_value);
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
                        
                        // all of these opcodes just manipulate the entity
                        // state
                        
                    case OPCODE_BRANCH_LEFT:
                    case OPCODE_BRANCH_RIGHT:
                    case OPCODE_HEADING_STORE:
                        if (new_this->peek().is_int64_t())
                            new_this->pop();
                        break;
                        
                    case OPCODE_HEADING_LOAD:
                        new_this->push(Value(_new_heading));
                        break;
                        
                    case OPCODE_DROP:
                        new_this->pop();
                        break;
                    case OPCODE_DUPLICATE:
                        new_this->push(new_this->peek());
                        break;
                    case OPCODE_OVER:
                        std::tie(b, a) = new_this->peek2();
                        new_this->push(b);
                        break;
                    case OPCODE_SWAP:
                        a = new_this->pop();
                        b = new_this->pop();
                        new_this->push(a);
                        new_this->push(b);
                        break;
                        
                        // arithmetic / logic
                        
                    case OPCODE_IS_NOT_ZERO:
                        a = new_this->peek();
                        if (a.is_int64_t()) {
                            a = a.as_int64_t() != 0;
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                        
                    case OPCODE_LOGICAL_NOT:
                        a = new_this->peek();
                        if (a.is_int64_t()) {
                            a = !a.as_int64_t();
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_LOGICAL_AND:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() && b.as_int64_t();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_LOGICAL_OR:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() || b.as_int64_t();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_LOGICAL_XOR:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = !a.as_int64_t() != !b.as_int64_t();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_BITWISE_NOT:
                        a = new_this->peek();
                        if (a.is_int64_t()) {
                            a = ~a.as_int64_t();
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_BITWISE_AND:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() & b.as_int64_t();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_BITWISE_OR:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() | b.as_int64_t();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_BITWISE_XOR:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() ^ b.as_int64_t();
                            new_this->pop2push1(a);
                        }
                        break;
                        
                    case OPCODE_BITWISE_SPLIT:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            i64 x = a.as_int64_t() & b.as_int64_t();
                            i64 y = a.as_int64_t() ^ b.as_int64_t();
                            a = x;
                            b = y;
                            new_this->pop();
                            new_this->pop();
                            new_this->push(a);
                            new_this->push(b);
                        }
                        break;
                        
                    case OPCODE_SHIFT_RIGHT:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() >> b.as_int64_t();
                            new_this->pop2push1(a);
                        }
                        break;
                        
                    case OPCODE_POPCOUNT:
                        a = peek();
                        if (a.is_int64_t()) {
                            a = __builtin_popcountll(a.as_int64_t());
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                        
                    case OPCODE_NEGATE:
                        a = new_this->peek();
                        if (a.is_int64_t()) {
                            a = -a.as_int64_t();
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_ABS:
                        a = new_this->peek();
                        if (a.is_int64_t()) {
                            a = abs(a.as_int64_t());
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_SIGN:
                        a = new_this->peek();
                        if (a.is_int64_t()) {
                            a = (0 < a.as_int64_t()) - (a.as_int64_t() < 0);
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_EQUAL:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() == b.as_int64_t();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_NOT_EQUAL:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() != b.as_int64_t();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_LESS_THAN:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() < b.as_int64_t();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_GREATER_THAN:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() > b.as_int64_t();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_LESS_THAN_OR_EQUAL_TO:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() <= b.as_int64_t();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_GREATER_THAN_OR_EQUAL_TO:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() >= b.as_int64_t();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_COMPARE:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = (a.as_int64_t() < b.as_int64_t()) - (b.as_int64_t() < (i64) a.as_int64_t());
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_ADD:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() + b.as_int64_t();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_SUBTRACT:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() - b.as_int64_t();
                            new_this->pop2push1(a);
                        }
                        break;
                        
                        // these are unusal self-modifying opcodes
                    case OPCODE_FLIP_FLOP:
                        tx->write_value_for_coordinate(
                                                   _new_location,
                                                   value_make_opcode(OPCODE_FLOP_FLIP));
                        break;
                    case OPCODE_FLOP_FLIP:
                        //assert(wants_write_new_tile);
                        tx->write_value_for_coordinate(
                                                   _new_location,
                                                   value_make_opcode(OPCODE_FLIP_FLOP));
                        break;
                                                
                } // switch (next_action)
                
                new_this->_on_arrival = next_action;
                new_this->_new_heading = next_heading;
                new_this->_new_location = next_location;
                new_this->_old_time = context->_world->_time;
                new_this->_new_time = context->_world->_time + 64;
                new_this->_phase = PHASE_TRAVELLING;
                tx->wait_on_time(new_this->_new_time);
                tx->on_abort_retry();
                printf("EntityID %lld proposes to WAIT on new time\n", _entity_id.data);
                tx->describe();
                return;
            }
                
            default:
                __builtin_unreachable();
                
        } // switch (_phase)

    }
 
    
} // namespace wry::sim
