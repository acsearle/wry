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
#include "context.hpp"

namespace wry::sim {
    
    void Machine::notify(Context* context) const {
#if 0
        
        Transaction* tx = Transaction::make(context, this, 10);

        
        //// if somebody else has acted on our state this turn, we can't actually
        //// do anything and must retry
        
        //if (!can_write_world_entity(world, this)) {
            //entity_ready_on_world(this, world);
        //}
        
        //// as we fall through the switch statement we can execute several
        //// different combinations of transactions, and must runtime track
        //// our responsibilities
        
        // bool did_read_entity = false;
        // bool did_write_entity = false;
                
        Value a = {};
        Value b = {};

        // read our own state (should this be implicit in tx creation?)
        tx->read_entity_for_entity_id(this->_entity_id);
        
        Machine* new_this = nullptr;

        switch (_phase) {
                
            case PHASE_TRAVELLING: {
                assert(_old_location != _new_location);
                if ((world_time(context->world) - _new_time) < 0)
                    // this was a spurious wakeup; for example, the cells under
                    // changed, or somebody mutated our payload, but we don't
                    // care until we arrive
                    // TODO: we could assert here that we are still going to
                    // be awakened at _new_time
                    return;
                new_this->_phase = PHASE_WAITING_FOR_OLD;
                //did_write_entity = true;
            } [[fallthrough]];
                
            case PHASE_WAITING_FOR_OLD: {
                assert(_old_location != _new_location);
                // Entity* occupant = peek_world_coordinate_occupant(world, _old_location);
                EntityID occupant = tx->read_entity_id_for_coordinate(_old_location);
                assert(occupant == this->_entity_id);
                if (!can_write_world_coordinate(world, _old_location)) {
                    if (did_write_entity) {
                        did_write_world_entity(world, this);
                    }
                    entity_ready_on_world(this, world);
                    return;
                }
                clear_world_coordinate_occupant(world, _old_location);
                did_write_world_coordinate(world, _old_location);
                _old_time = _new_time;
                _old_location = _new_location;
                _old_heading = _new_heading;
                _phase = PHASE_WAITING_FOR_NEW;
                did_write_entity = true;
            } [[fallthrough]];
                
            case PHASE_WAITING_FOR_NEW: {
                
                assert(_old_location == _new_location);
                
                // TODO: at the moment we have _on_arrivial and next action to
                // coordinate stuff.  Can we instead use the top of the stack
                // itself as the instruction slot and current machine state?
                
                Entity* occupant = peek_world_coordinate_occupant(world, _new_location);
                assert(occupant == this);

                // check required access to the tile
                
                bool wants_read_new_tile = false;
                bool wants_write_new_tile = false;
                
                switch (_on_arrival) {
                        
                    case OPCODE_SKIP:
                        // does not access the tile
                        break;
                        
                    default:
                        // reads the tile
                        if (!can_read_world_coordinate(world, _new_location)) {
                            if (did_write_entity) {
                                did_write_world_entity(world, this);
                            }
                            entity_ready_on_world(this, world);
                            return;
                        }
                        wants_read_new_tile = true;
                        break;
                        
                    case OPCODE_STORE:
                    case OPCODE_EXCHANGE:
                        // writes the tile
                        if (!can_write_world_coordinate(world, _new_location)) {
                            if (did_write_entity) {
                                did_write_world_entity(world, this);
                            }
                            entity_ready_on_world(this, world);
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
                        new_value = peek_world_coordinate_value(world, _new_location);
                        break;
                    default:
                        // we load [_new_location] and may execute it
                        new_value = peek_world_coordinate_value(world, _new_location);
                        if (new_value.is_opcode())
                            next_action = new_value.as_opcode();
                        break;
                }
            
                // bail out for the trivial case of halt
                
                if (next_action == OPCODE_HALT) {
                    // we don't need to do any further processing
                    assert(wants_read_new_tile);
                    assert(!wants_write_new_tile);
                    did_read_world_coordinate(world, _new_location);
                    _on_arrival = OPCODE_NOOP;
                    did_write_world_entity(world, this);
                    // don't wait on anything (except, implicitly, for the value
                    // of the tile under us to change)
                    return;
                }
                
                // upgrade our permissions for self-modifying opcodes
                
                switch (next_action) {
                    case OPCODE_FLIP_FLOP:
                    case OPCODE_FLOP_FLIP:
                        if (!can_write_world_coordinate(world, _new_location)) {
                            if (did_write_entity) {
                                did_write_world_entity(world, this);
                            }
                            entity_ready_on_world(this, world);
                            return;
                        }
                        wants_write_new_tile = true;
                        break;
                    default:
                        break;
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
                
                // try to claim it
                
                if (!can_write_world_coordinate(world, next_location)) {
                    if (did_write_entity) {
                        did_write_world_entity(world, this);
                    }
                    entity_ready_on_world(this, world);
                    return;
                }
                occupant = peek_world_coordinate_occupant(world, next_location);
                if (occupant) {
                    assert(occupant != this);
                    // occupied; wait
                    if (did_write_entity) {
                        did_write_world_entity(world, this);
                    }
                    entity_wait_on_world_coordinate(this, world, next_location);
                    // we choose to interpret this as a failed transaction
                    // which may speed up the release of the other tile
                    return;
                }
                
                // the transaction will now succeed
                
                set_world_coordinate_occupant(world, next_location, this);
                did_write_world_coordinate(world, next_location);
                did_write_world_entity(world, this);

                // we need to reload new_tile in case accessing next_tile
                // invalidated the reference
                
                if (wants_write_new_tile) {
                    did_write_world_coordinate(world, _new_location);
                } else if (wants_read_new_tile) {
                    did_read_world_coordinate(world, _new_location);
                }

                switch (_on_arrival) {
                    case OPCODE_SKIP:
                        break;
                    case OPCODE_LOAD:
                        push(new_value);
                        break;
                    case OPCODE_STORE:
                        assert(wants_write_new_tile);
                        set_world_coordinate_value(world, _new_location, pop());
                        break;
                    case OPCODE_EXCHANGE:
                        a = pop();
                        // TODO: should push(...) itself discard nothings, always?
                        push(new_value);
                        assert(wants_write_new_tile);
                        set_world_coordinate_value(world, _new_location, a);
                        break;
                    default:
                        // To avoid explicit loads everywhere, when we run over
                        // something that
                        // - is not an opcode
                        // - is copyable, aka immaterial, sybmbolic, numeric?
                        // we pick it up.  Good idea?
                        if (new_value.is_int64_t()) {
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
                        
                        // all of these opcodes just manipulate the entity
                        // state
                        
                    case OPCODE_BRANCH_LEFT:
                    case OPCODE_BRANCH_RIGHT:
                    case OPCODE_HEADING_STORE:
                        if (peek().is_int64_t())
                            pop();
                        break;
                        
                    case OPCODE_HEADING_LOAD:
                        push(Value(_new_heading));
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
                        if (a.is_int64_t()) {
                            a = a.as_int64_t() != 0;
                            pop(); push(a);
                        }
                        break;
                        
                    case OPCODE_LOGICAL_NOT:
                        a = peek();
                        if (a.is_int64_t()) {
                            a = !a.as_int64_t();
                            pop(); push(a);
                        }
                        break;
                    case OPCODE_LOGICAL_AND:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() && b.as_int64_t();
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_LOGICAL_OR:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() || b.as_int64_t();
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_LOGICAL_XOR:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = !a.as_int64_t() != !b.as_int64_t();
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_BITWISE_NOT:
                        a = peek();
                        if (a.is_int64_t()) {
                            a = ~a.as_int64_t();
                            pop(); push(a);
                        }
                        break;
                    case OPCODE_BITWISE_AND:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() & b.as_int64_t();
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_BITWISE_OR:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() | b.as_int64_t();
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_BITWISE_XOR:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() ^ b.as_int64_t();
                            pop2push1(a);
                        }
                        break;
                        
                    case OPCODE_BITWISE_SPLIT:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            i64 x = a.as_int64_t() & b.as_int64_t();
                            i64 y = a.as_int64_t() ^ b.as_int64_t();
                            a = x;
                            b = y;
                            pop();
                            pop();
                            push(a);
                            push(b);
                        }
                        break;
                        
                    case OPCODE_SHIFT_RIGHT:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() >> b.as_int64_t();
                            pop2push1(a);
                        }
                        break;
                        
                    case OPCODE_POPCOUNT:
                        a = peek();
                        if (a.is_int64_t()) {
                            a = __builtin_popcountll(a.as_int64_t());
                            pop(); push(a);
                        }
                        break;
                        
                    case OPCODE_NEGATE:
                        a = peek();
                        if (a.is_int64_t()) {
                            a = -a.as_int64_t();
                            pop(); push(a);
                        }
                        break;
                    case OPCODE_ABS:
                        a = peek();
                        if (a.is_int64_t()) {
                            a = abs(a.as_int64_t());
                            pop(); push(a);
                        }
                        break;
                    case OPCODE_SIGN:
                        a = peek();
                        if (a.is_int64_t()) {
                            a = (0 < a.as_int64_t()) - (a.as_int64_t() < 0);
                            pop(); push(a);
                        }
                        break;
                    case OPCODE_EQUAL:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() == b.as_int64_t();
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_NOT_EQUAL:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() != b.as_int64_t();
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_LESS_THAN:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() < b.as_int64_t();
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_GREATER_THAN:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() > b.as_int64_t();
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_LESS_THAN_OR_EQUAL_TO:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() <= b.as_int64_t();
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_GREATER_THAN_OR_EQUAL_TO:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() >= b.as_int64_t();
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_COMPARE:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = (a.as_int64_t() < b.as_int64_t()) - (b.as_int64_t() < (i64) a.as_int64_t());
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_ADD:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() + b.as_int64_t();
                            pop2push1(a);
                        }
                        break;
                    case OPCODE_SUBTRACT:
                        std::tie(a, b) = peek2();
                        if (a.is_int64_t() && b.is_int64_t()) {
                            a = a.as_int64_t() - b.as_int64_t();
                            pop2push1(a);
                        }
                        break;
                        
                        // these are unusal self-modifying opcodes
                    case OPCODE_FLIP_FLOP:
                        assert(wants_write_new_tile);
                        set_world_coordinate_value(world, 
                                                   _new_location,
                                                   gc::value_make_opcode(OPCODE_FLOP_FLIP));
                        break;
                    case OPCODE_FLOP_FLIP:
                        assert(wants_write_new_tile);
                        set_world_coordinate_value(world,
                                                   _new_location,
                                                   gc::value_make_opcode(OPCODE_FLIP_FLOP));
                        break;
                                                
                } // switch (next_action)
                
                _on_arrival = next_action;
                _new_heading = next_heading;
                _new_location = next_location;
                _old_time = world_time(world);
                _new_time = world_time(world) + 64;
                _phase = PHASE_TRAVELLING;
                entity_wait_on_world_time(this, world, _new_time);
                return;
            }
                
            default:
                __builtin_unreachable();
                
        } // switch (_phase)
#endif
    }
 
    
} // namespace wry::sim
