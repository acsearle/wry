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

#include "epoch.hpp"
#include "matter.hpp"
#include "test.hpp"

namespace wry {
    
    void Machine::notify(TransactionContext* context) const {

        Transaction* tx = Transaction::make(context, this, 10);

        Term a = {};
        Term b = {};

        Machine* new_this = make_mutable_clone();
        assert(new_this->_entity_id == _entity_id);
        
        //printf("Machine::notify()\n");

        switch (_phase) {
                
            case PHASE_TRAVELLING: {
                // Machine is travelling from old location to new location
                assert(_old_location != _new_location);
                if ((context->_world->_time - _new_time) < 0) {
                    // This was a spurious wakeup
                    // printf("EntityID %lld experienced spurious wakeup\n", _entity_id.data);
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
                
                Term new_value = {};
                i64 next_action = OPCODE_NOOP;
                switch (_on_arrival) {
                    case OPCODE_SKIP:
                        // we ignore the current value entirely
                        break;
                    case OPCODE_STORE:
                        // we load [_new_location] only to check that we may
                        // overwrite it; see the matter early-out below
                        (void) tx->try_read_value_for_coordinate(_new_location, new_value);
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
                    // printf("EntityID %lld proposes to HALT\n", _entity_id.data);
                    return;
                }

                // a pending STORE must not destroy matter in the
                // destination, and matter itself may only be placed into
                // an empty cell (in particular, never over a program
                // glyph).  Park here and retry when the cell changes.
                if (_on_arrival == OPCODE_STORE) {
                    Term pending = new_this->peek();
                    if (new_value.is_matter()
                        || (pending.is_matter() && !term_is_null(new_value))) {
                        tx->write_entity_for_entity_id(this->_entity_id, new_this);
                        tx->wait_on_value_for_coordinate(_new_location);
                        tx->on_abort_retry();
                        return;
                    }
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
                        a = new_this->peek();
                        if (a.is_inty())
                            next_heading -= a.as_int();
                        break;
                    case OPCODE_BRANCH_RIGHT:
                        a = new_this->peek();
                        if (a.is_inty())
                            next_heading += a.as_int();
                        break;
                    case OPCODE_HEADING_STORE:
                        a = new_this->peek();
                        if (a.is_inty())
                            next_heading = a.as_int();
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
                    // occupied; wait for our destination to clear
                    tx->wait_on_entity_id_for_coordinate(next_location);
                    // or wait for the instruction under us to change
                    tx->wait_on_value_for_coordinate(_new_location);
                    // waiting can't fail
                    // clearing our own previous location shouldn't fail
                    // writing our next state can fail though if somebody is
                    // messing with us
                    // if our own state gets written by somebody else, they are
                    // responsible for scheduling us
                    // printf("EntityID %lld proposes to WAIT on next_location (or new instructions)\n", _entity_id.data);
                    tx->on_abort_retry();
                    //tx->describe();
                    return;
                }
                tx->write_entity_id_for_coordinate(next_location, this->_entity_id);
                
                                
                

                switch (_on_arrival) {
                    case OPCODE_SKIP:
                        break;
                    case OPCODE_LOAD:
                        new_this->push(new_value);
                        // matter is taken, not copied
                        if (new_value.is_matter())
                            tx->write_value_for_coordinate(_new_location, term_make_empty());
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
                        if (new_value.is_inty()) {
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
                        if (new_this->peek().is_inty())
                            new_this->pop();
                        break;
                        
                    case OPCODE_HEADING_LOAD:
                        new_this->push(Term(_new_heading));
                        break;
                        
                    case OPCODE_DROP:
                        // matter is not destroyed; it is put down in the
                        // next cell as if by STORE (waiting until empty)
                        if (new_this->peek().is_matter())
                            next_action = OPCODE_STORE;
                        else
                            new_this->pop();
                        break;
                    case OPCODE_DUPLICATE:
                        // matter is not copyable
                        a = new_this->peek();
                        if (!a.is_matter())
                            new_this->push(a);
                        break;
                    case OPCODE_OVER:
                        // matter is not copyable
                        std::tie(b, a) = new_this->peek2();
                        if (!b.is_matter())
                            new_this->push(b);
                        break;
                    case OPCODE_SWAP:
                        a = new_this->pop();
                        b = new_this->pop();
                        new_this->push(a);
                        new_this->push(b);
                        break;
                    case OPCODE_ROT: {
                        // ( x y z -- y z x ); pure permutation, so matter
                        // may ride along; needs three operands
                        auto* s = new_this->_stack;
                        if (s && s->_next && s->_next->_next) {
                            Term z = new_this->pop();
                            Term y = new_this->pop();
                            Term x = new_this->pop();
                            new_this->push(y);
                            new_this->push(z);
                            new_this->push(x);
                        }
                    } break;
                        
                        // arithmetic / logic
                        
                    case OPCODE_IS_ZERO:
                        a = new_this->peek();
                        if (a.is_inty()) {
                            a = a.as_int() == 0;
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_IS_NOT_ZERO:
                        a = new_this->peek();
                        if (a.is_inty()) {
                            a = a.as_int() != 0;
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_IS_POSITIVE:
                        a = new_this->peek();
                        if (a.is_inty()) {
                            a = a.as_int() > 0;
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_IS_NOT_POSITIVE:
                        a = new_this->peek();
                        if (a.is_inty()) {
                            a = a.as_int() <= 0;
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_IS_NEGATIVE:
                        a = new_this->peek();
                        if (a.is_inty()) {
                            a = a.as_int() < 0;
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_IS_NOT_NEGATIVE:
                        a = new_this->peek();
                        if (a.is_inty()) {
                            a = a.as_int() >= 0;
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_IS_MATTER:
                        // ( x -- x flag ); non-consuming, so testing can
                        // never destroy matter
                        a = new_this->peek();
                        if (!term_is_null(a))
                            new_this->push(Term(a.is_matter()));
                        break;


                    case OPCODE_LOGICAL_NOT:
                        a = new_this->peek();
                        if (a.is_booly()) {
                            a = a.is_falsey();
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_LOGICAL_AND:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_booly() && b.is_booly()) {
                            a = a.is_truthy() && b.is_truthy();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_LOGICAL_OR:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_booly() && b.is_booly()) {
                            a = a.is_truthy() || b.is_truthy();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_LOGICAL_XOR:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_booly() && b.is_booly()) {
                            a = a.is_truthy() != b.is_truthy();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_BITWISE_NOT:
                        a = new_this->peek();
                        if (a.is_inty()) {
                            a = ~a.as_int();
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_BITWISE_AND:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_inty() && b.is_inty()) {
                            a = a.as_int() & b.as_int();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_BITWISE_OR:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_inty() && b.is_inty()) {
                            a = a.as_int() | b.as_int();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_BITWISE_XOR:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_inty() && b.is_inty()) {
                            a = a.as_int() ^ b.as_int();
                            new_this->pop2push1(a);
                        }
                        break;

                    case OPCODE_BITWISE_SPLIT:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_inty() && b.is_inty()) {
                            i64 x = a.as_int() & b.as_int();
                            i64 y = a.as_int() ^ b.as_int();
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
                        if (a.is_inty() && b.is_inty()) {
                            // clamp: small integers are 60-bit, so by 60
                            // every value is all sign bits anyway, and a
                            // negative count would be undefined behavior
                            i64 shift = b.as_int();
                            if (shift < 0) shift = 0;
                            if (shift > 60) shift = 60;
                            a = a.as_int() >> shift;
                            new_this->pop2push1(a);
                        }
                        break;

                    case OPCODE_POPCOUNT:
                        a = new_this->peek();
                        if (a.is_inty()) {
                            a = __builtin_popcountll(a.as_int());
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                        
                    case OPCODE_NEGATE:
                        a = new_this->peek();
                        if (a.is_inty()) {
                            a = -a.as_int();
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_ABS:
                        a = new_this->peek();
                        if (a.is_inty()) {
                            a = abs(a.as_int());
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_SIGN:
                        a = new_this->peek();
                        if (a.is_inty()) {
                            a = (0 < a.as_int()) - (a.as_int() < 0);
                            new_this->pop(); new_this->push(a);
                        }
                        break;
                    case OPCODE_EQUAL:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_inty() && b.is_inty()) {
                            // booleans and integers cross-compare as 0/1
                            a = a.as_int() == b.as_int();
                            new_this->pop2push1(a);
                        } else if (!term_is_null(a) && !term_is_null(b)
                                   && !a.is_matter() && !b.is_matter()) {
                            // any information compares; consuming matter
                            // into a flag would destroy it, so matter
                            // refuses (until ghost matter exists).  Term-
                            // incomparable pairs (cross-type) count as
                            // not equal rather than leaking ERROR.
                            Term r = term_eq(a, b);
                            new_this->pop2push1(term_is_boolean(r)
                                                ? r : Term(false));
                        }
                        break;
                    case OPCODE_NOT_EQUAL:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_inty() && b.is_inty()) {
                            a = a.as_int() != b.as_int();
                            new_this->pop2push1(a);
                        } else if (!term_is_null(a) && !term_is_null(b)
                                   && !a.is_matter() && !b.is_matter()) {
                            // negation of EQUAL, same contract
                            Term r = term_eq(a, b);
                            bool eq = term_is_boolean(r) && term_as_boolean(r);
                            new_this->pop2push1(Term(!eq));
                        }
                        break;
                    case OPCODE_LESS_THAN:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_inty() && b.is_inty()) {
                            a = a.as_int() < b.as_int();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_GREATER_THAN:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_inty() && b.is_inty()) {
                            a = a.as_int() > b.as_int();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_LESS_THAN_OR_EQUAL_TO:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_inty() && b.is_inty()) {
                            a = a.as_int() <= b.as_int();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_GREATER_THAN_OR_EQUAL_TO:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_inty() && b.is_inty()) {
                            a = a.as_int() >= b.as_int();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_COMPARE:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_inty() && b.is_inty()) {
                            // sign(a - b), agreeing with SUBTRACT; SIGN
                            a = (b.as_int() < a.as_int()) - (a.as_int() < b.as_int());
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_ADD:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_inty() && b.is_inty()) {
                            a = a.as_int() + b.as_int();
                            new_this->pop2push1(a);
                        }
                        break;
                    case OPCODE_SUBTRACT:
                        std::tie(a, b) = new_this->peek2();
                        if (a.is_inty() && b.is_inty()) {
                            a = a.as_int() - b.as_int();
                            new_this->pop2push1(a);
                        }
                        break;
                        
                        // these are unusal self-modifying opcodes
                    case OPCODE_FLIP_FLOP:
                        tx->write_value_for_coordinate(
                                                   _new_location,
                                                   term_make_opcode(OPCODE_FLOP_FLIP));
                        break;
                    case OPCODE_FLOP_FLIP:
                        //assert(wants_write_new_tile);
                        tx->write_value_for_coordinate(
                                                   _new_location,
                                                   term_make_opcode(OPCODE_FLIP_FLOP));
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
                // printf("EntityID %lld proposes to WAIT on new time\n", _entity_id.data);
                //tx->describe();
                return;
            }
                
            default:
                __builtin_unreachable();
                
        } // switch (_phase)

    }

    // ====================================================================
    // Machine interpreter semantics tests
    //
    // Build a small world containing glyph tracks and at-rest machines,
    // step it headlessly to a fixed time, then assert on the machines'
    // final state and on the value plane.  Tracks park their machines on
    // HALT so the terminal state is stable regardless of how far past the
    // last arrival we step.
    //
    // Stepping protocol: World::step's transaction structures are epoch-
    // allocated and its work is serviced by many pool threads, so the
    // whole fork-join must sit under an epoch pin.  The GUI gets this
    // implicitly -- WorldState::update's thread stays pinned across its
    // fork+sync_wait.  The test instead holds a PORTABLE pin
    // (pin_global_epoch / unpin_global_epoch): owned by the coroutine
    // frame rather than by any thread, held across a co_await of the
    // step, which may resume on a different pool thread with a
    // different thread-local pin.  This exercises the non-thread-pin
    // path the epoch Service was designed around (see
    // State::pin_explicit's comment in epoch.hpp).  The pin is released
    // between steps so the epoch, and hence the collector, can advance
    // across the loop; the live world is always held by a Root.

    namespace {

        void test_put(World* w, i32 x, i32 y, Term t) {
            w->_term_for_coordinate.set(Coordinate{x, y}, t);
        }

        EntityID test_machine_at_rest(World* w, i32 x, i32 y) {
            Machine* m = new Machine;
            m->_old_location = Coordinate{x, y};
            m->_new_location = Coordinate{x, y};
            w->_entity_for_entity_id.set(m->_entity_id, m);
            w->_entity_id_for_coordinate.set(Coordinate{x, y}, m->_entity_id);
            w->_waiting_on_time.set({Time{0}, m->_entity_id});
            return m->_entity_id;
        }

        Coroutine::Task test_step_until(Root<World*>& world, Time t_end) {
            while (world._ptr->_time < t_end) {
                // Extra pin, living in this coroutine's frame: it holds
                // the epoch floor for step's epoch-allocated structures
                // while the awaiting thread's own pin comes and goes.
                epoch::Epoch our_pin = pin_global_epoch();
                Root<World*> next = co_await world._ptr->step();
                unpin_global_epoch(our_pin);
                assert(next._ptr);
                world = std::move(next);
            }
        }

        const Machine* test_machine_for(const Root<World*>& world, EntityID id) {
            const Entity* e = nullptr;
            (void) world._ptr->_entity_for_entity_id.try_get(id, e);
            assert(e);
            return static_cast<const Machine*>(e);
        }

        // expected reads bottom -> top, the order the machine acquired them
        [[maybe_unused]]
        bool test_stack_is(const Machine* m, std::initializer_list<Term> expected) {
            std::vector<Term> top_first;
            for (auto* s = m->_stack; s; s = s->_next)
                top_first.push_back(s->_payload);
            if (top_first.size() != expected.size())
                return false;
            size_t i = top_first.size();
            for (Term t : expected) {
                --i;
                if (top_first[i]._data != t._data)
                    return false;
            }
            return true;
        }

    } // anonymous namespace

    define_test("machine_step_semantics") {

        World* w = new World;

        // Track A (x=0, northbound): auto-pickup of literals, ADD, the
        // flipped COMPARE (sign(a-b)), DUPLICATE, the clamped SHIFT_RIGHT,
        // IS_ZERO producing a boolean, HALT.
        //
        // 12 COMPARE 3 = +1 (the pre-fix inverted convention gave -1,
        // which would propagate to a final false); 2 >> 65 clamps to
        // 2 >> 60 = 0 (unclamped ARM mod-64 behavior would give
        // 2 >> 1 = 1).
        EntityID ma = test_machine_at_rest(w, 0, 0);
        test_put(w, 0,  1, term_make_integer_with(5));
        test_put(w, 0,  2, term_make_integer_with(7));
        test_put(w, 0,  3, term_make_opcode(OPCODE_ADD));
        test_put(w, 0,  4, term_make_integer_with(3));
        test_put(w, 0,  5, term_make_opcode(OPCODE_COMPARE));
        test_put(w, 0,  6, term_make_opcode(OPCODE_DUPLICATE));
        test_put(w, 0,  7, term_make_opcode(OPCODE_ADD));
        test_put(w, 0,  8, term_make_integer_with(65));
        test_put(w, 0,  9, term_make_opcode(OPCODE_SHIFT_RIGHT));
        test_put(w, 0, 10, term_make_opcode(OPCODE_IS_ZERO));
        test_put(w, 0, 11, term_make_opcode(OPCODE_HALT));

        // Track B (x=10): ROT, integer-steered BRANCH_RIGHT (east), then
        // HEADING_STORE turns south by data, HEADING_LOAD reads the
        // heading back, HALT.
        EntityID mb = test_machine_at_rest(w, 10, 0);
        test_put(w, 10, 1, term_make_integer_with(1));
        test_put(w, 10, 2, term_make_integer_with(2));
        test_put(w, 10, 3, term_make_integer_with(3));
        test_put(w, 10, 4, term_make_opcode(OPCODE_ROT));
        test_put(w, 10, 5, term_make_integer_with(1));
        test_put(w, 10, 6, term_make_opcode(OPCODE_BRANCH_RIGHT));
        test_put(w, 11, 6, term_make_integer_with(2));
        test_put(w, 12, 6, term_make_opcode(OPCODE_HEADING_STORE));
        test_put(w, 12, 5, term_make_opcode(OPCODE_HEADING_LOAD));
        test_put(w, 12, 4, term_make_opcode(OPCODE_HALT));

        // Track C (x=20): EQUAL on equal ints pushes true; BRANCH_RIGHT
        // consumes the boolean as 1 and turns east.
        EntityID mc = test_machine_at_rest(w, 20, 0);
        test_put(w, 20, 1, term_make_integer_with(4));
        test_put(w, 20, 2, term_make_integer_with(4));
        test_put(w, 20, 3, term_make_opcode(OPCODE_EQUAL));
        test_put(w, 20, 4, term_make_opcode(OPCODE_BRANCH_RIGHT));
        test_put(w, 21, 4, term_make_opcode(OPCODE_HALT));

        // Track D (x=30): EQUAL on unequal ints pushes false; BRANCH_RIGHT
        // consumes it as 0 and continues straight.
        EntityID md = test_machine_at_rest(w, 30, 0);
        test_put(w, 30, 1, term_make_integer_with(4));
        test_put(w, 30, 2, term_make_integer_with(5));
        test_put(w, 30, 3, term_make_opcode(OPCODE_EQUAL));
        test_put(w, 30, 4, term_make_opcode(OPCODE_BRANCH_RIGHT));
        test_put(w, 30, 5, term_make_opcode(OPCODE_HALT));

        // Track E (x=40): LOAD takes matter (the cell empties), DUPLICATE
        // refuses it, IS_MATTER tests without consuming, the flag steers
        // BRANCH_RIGHT east, DROP of matter becomes a STORE into the next
        // empty cell, HALT.  Conservation: exactly one container, moved
        // from (40,2) to (42,5).
        EntityID me = test_machine_at_rest(w, 40, 0);
        test_put(w, 40, 1, term_make_opcode(OPCODE_LOAD));
        test_put(w, 40, 2, term_make_matter(MATTER_SHIPPING_CONTAINER));
        test_put(w, 40, 3, term_make_opcode(OPCODE_DUPLICATE));
        test_put(w, 40, 4, term_make_opcode(OPCODE_IS_MATTER));
        test_put(w, 40, 5, term_make_opcode(OPCODE_BRANCH_RIGHT));
        test_put(w, 41, 5, term_make_opcode(OPCODE_DROP));
        // (42,5) deliberately left empty: the DROP target
        test_put(w, 43, 5, term_make_opcode(OPCODE_HALT));

        Root<World*> world{w};

        // Longest track parks after 11 hops = 704 ticks; run past it.
        co_await test_step_until(world, Time{800});

        {
            const Machine* m = test_machine_for(world, ma);
            assert(m->_phase == Machine::PHASE_WAITING_FOR_NEW);
            assert((m->_new_location == Coordinate{0, 11}));
            assert(m->_new_heading == HEADING_NORTH);
            assert(m->_on_arrival == OPCODE_NOOP);
            assert((test_stack_is(m, {Term(true)})));
        }

        {
            const Machine* m = test_machine_for(world, mb);
            assert((m->_new_location == Coordinate{12, 4}));
            assert(m->_new_heading == HEADING_SOUTH);
            assert((test_stack_is(m, {term_make_integer_with(2),
                                      term_make_integer_with(3),
                                      term_make_integer_with(1),
                                      term_make_integer_with(2)})));
        }

        {
            const Machine* m = test_machine_for(world, mc);
            assert((m->_new_location == Coordinate{21, 4}));
            assert(m->_new_heading == HEADING_EAST);
            assert((test_stack_is(m, {})));
        }

        {
            const Machine* m = test_machine_for(world, md);
            assert((m->_new_location == Coordinate{30, 5}));
            assert(m->_new_heading == HEADING_NORTH);
            assert((test_stack_is(m, {})));
        }

        {
            const Machine* m = test_machine_for(world, me);
            assert((m->_new_location == Coordinate{43, 5}));
            assert(m->_new_heading == HEADING_EAST);
            assert((test_stack_is(m, {})));

            // try_get leaves its out-param untouched on a miss, so a
            // default (null) Term covers both "erased" and "stored null"
            Term taken{};
            (void) world._ptr->_term_for_coordinate.try_get(Coordinate{40, 2}, taken);
            assert(term_is_null(taken));

            Term placed{};
            (void) world._ptr->_term_for_coordinate.try_get(Coordinate{42, 5}, placed);
            assert(placed._data == term_make_matter(MATTER_SHIPPING_CONTAINER)._data);
        }

        co_return;
    };

} // namespace wry::sim
