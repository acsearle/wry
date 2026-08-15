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
#include "spawner.hpp"
#include "test.hpp"

namespace wry {

    // ====================================================================
    // The interpreter, structured around the transaction commit point
    // (the occupancy claim).  Each arrival divides into:
    //
    //   plan_arrival    the DECIDE half: pure reads.  Classify the
    //                   pending memory op, read the cell, resolve
    //                   steering, and choose a disposition -- every way
    //                   an arrival can park, enumerated in one place.
    //   notify          the COMMIT half: write the waits for a parking
    //                   disposition, or claim the next cell and apply
    //                   this arrival's effects.  Nothing has happened
    //                   until the claim succeeds; a parked machine has
    //                   done nothing at all.
    //   apply_pending   the latched memory op's effect at this cell
    //                   (LOAD / STORE / EXCHANGE / auto-pickup).
    //   the action switch  the executed opcode's stack and world
    //                   effects, in notify's commit tail.
    //
    // Steering has a single source of truth, steering_of: the planner
    // uses it for the heading and records the operand consumption in
    // the plan, so no opcode's logic is split across switches (the
    // peek-here-pop-there duplication that produced the HEADING
    // crossed-wires bug is structurally gone).
    //
    // The phase machinery around all this (travel, release-old, clone,
    // retries) is the transactional shell, not language semantics.

    namespace {

        struct Steering {
            enum Kind {
                STEER_STRAIGHT,        // no heading change
                STEER_ABSOLUTE,        // heading := argument
                STEER_RELATIVE,        // heading += argument
                STEER_STACK_RELATIVE,  // heading += argument * top, if
                                       // top is inty; consumes top
                STEER_STACK_ABSOLUTE,  // heading := top, if top is
                                       // inty; consumes top
            };
            Kind kind;
            i64 argument;
        };

        Steering steering_of(i64 action) {
            switch (action) {
                case OPCODE_TURN_NORTH:
                    return {Steering::STEER_ABSOLUTE, HEADING_NORTH};
                case OPCODE_TURN_EAST:
                    return {Steering::STEER_ABSOLUTE, HEADING_EAST};
                case OPCODE_TURN_SOUTH:
                    return {Steering::STEER_ABSOLUTE, HEADING_SOUTH};
                case OPCODE_TURN_WEST:
                    return {Steering::STEER_ABSOLUTE, HEADING_WEST};
                case OPCODE_TURN_RIGHT:
                case OPCODE_FLIP_FLOP:
                    return {Steering::STEER_RELATIVE, +1};
                case OPCODE_TURN_LEFT:
                case OPCODE_FLOP_FLIP:
                    return {Steering::STEER_RELATIVE, -1};
                case OPCODE_TURN_BACK:
                    return {Steering::STEER_RELATIVE, +2};
                case OPCODE_BRANCH_RIGHT:
                    return {Steering::STEER_STACK_RELATIVE, +1};
                case OPCODE_BRANCH_LEFT:
                    return {Steering::STEER_STACK_RELATIVE, -1};
                case OPCODE_HEADING_STORE:
                    return {Steering::STEER_STACK_ABSOLUTE, 0};
                default:
                    return {Steering::STEER_STRAIGHT, 0};
            }
        }

        // The four memory opcodes latch across the hop and treat their
        // operand cell -- the cell the machine lands on next -- as
        // data: not executed, not auto-picked.
        bool pending_treats_cell_as_data(i64 pending) {
            switch (pending) {
                case OPCODE_SKIP:
                case OPCODE_LOAD:
                case OPCODE_STORE:
                case OPCODE_EXCHANGE:
                    return true;
                default:
                    return false;
            }
        }

        // Does a valve at the destination bar entry along this heading?
        // A valve is passable only along its named axis.  The gate
        // applies to EVERY entry, even one whose pending memory op will
        // treat the valve as data (entering is entering); the FLIP,
        // by contrast, happens only when the valve is executed, so a
        // SKIP or memory op ghosts through an open valve unflipped.
        bool valve_blocks(Term destination_value, i64 heading) {
            if (!destination_value.is_opcode())
                return false;
            i64 opcode = destination_value.as_opcode();
            if ((opcode != OPCODE_VALVE_NORTH_SOUTH)
                && (opcode != OPCODE_VALVE_EAST_WEST))
                return false;
            bool valve_is_north_south = (opcode == OPCODE_VALVE_NORTH_SOUTH);
            bool travelling_north_south = ((heading & 1) == 0);
            return valve_is_north_south != travelling_north_south;
        }

        struct ArrivalPlan {
            enum Disposition {
                PROCEED,           // claim next_location, apply effects
                PARK_HALT,         // sleep until the value under us changes
                PARK_STORE_GUARD,  // the pending STORE may not act yet
                PARK_OCCUPIED,     // next_location is occupied
                PARK_VALVE,        // a valve ahead is crossed to our axis
                PARK_JUNCTION,     // a DO_NOT_QUEUE ahead whose exit is
                                   // not currently enterable
            };
            Disposition disposition;
            Term cell_value;       // value plane at the machine's cell
            i64 action;            // opcode executing this arrival, or NOOP
            bool steering_pop;     // the action consumed the top of stack
            i64 next_heading;
            Coordinate next_location;
            bool claim_beyond;     // DO_NOT_QUEUE: also claim `beyond`
            Coordinate beyond;     // the junction's exit cell
        };

        Coordinate step_from(Coordinate c, i64 heading) {
            switch (heading & 3) {
                case 0: ++c.y; break;
                case 1: ++c.x; break;
                case 2: --c.y; break;
                case 3: --c.x; break;
            }
            return c;
        }

        // The decide half.  Reads only; no writes, no waits.
        ArrivalPlan plan_arrival(const Machine* self, Transaction* tx) {
            ArrivalPlan plan = {};

            // The cell's value is executed only if no pending memory op
            // claims it as data
            (void) tx->try_read_value_for_coordinate(self->_new_location,
                                                     plan.cell_value);
            plan.action = OPCODE_NOOP;
            if (!pending_treats_cell_as_data(self->_on_arrival)
                && plan.cell_value.is_opcode())
                plan.action = plan.cell_value.as_opcode();

            // Steering
            plan.next_heading = self->_new_heading;
            plan.steering_pop = false;
            Steering steering = steering_of(plan.action);
            switch (steering.kind) {
                case Steering::STEER_STRAIGHT:
                    break;
                case Steering::STEER_ABSOLUTE:
                    plan.next_heading = steering.argument;
                    break;
                case Steering::STEER_RELATIVE:
                    plan.next_heading += steering.argument;
                    break;
                case Steering::STEER_STACK_RELATIVE: {
                    Term top = self->peek();
                    if (top.is_inty()) {
                        plan.next_heading += steering.argument * top.as_int();
                        plan.steering_pop = true;
                    }
                } break;
                case Steering::STEER_STACK_ABSOLUTE: {
                    Term top = self->peek();
                    if (top.is_inty()) {
                        plan.next_heading = top.as_int();
                        plan.steering_pop = true;
                    }
                } break;
            }

            plan.next_location = step_from(self->_new_location,
                                           plan.next_heading);

            // Disposition: every way an arrival can park, in one place

            if (plan.action == OPCODE_HALT) {
                plan.disposition = ArrivalPlan::PARK_HALT;
                return plan;
            }

            // A pending STORE must not destroy matter in the
            // destination, and matter itself may only be placed into an
            // empty cell (in particular, never over a program glyph)
            if (self->_on_arrival == OPCODE_STORE) {
                Term pending = self->peek();
                if (plan.cell_value.is_matter()
                    || (pending.is_matter()
                        && !term_is_null(plan.cell_value))) {
                    plan.disposition = ArrivalPlan::PARK_STORE_GUARD;
                    return plan;
                }
            }

            Term destination_value = {};
            (void) tx->try_read_value_for_coordinate(plan.next_location,
                                                     destination_value);

            // Valves gate passage by axis.  Checked before occupancy:
            // an occupant's departure does not open a crossed valve, so
            // waiting on the valve's VALUE (it flips when an aligned
            // passage executes it, or an edit replaces it) is the
            // productive wait.
            if (valve_blocks(destination_value, plan.next_heading)) {
                plan.disposition = ArrivalPlan::PARK_VALVE;
                return plan;
            }

            EntityID occupant = {};
            (void) tx->try_read_entity_id_for_coordinate(plan.next_location,
                                                         occupant);
            if ((occupant == self->_entity_id)
                && (plan.next_location != self->_old_location)) {
                // The cell ahead is already ours: a DO_NOT_QUEUE
                // pre-claim made on the junction approach.  Fall through
                // to the junction rule (the pre-claimed cell may itself
                // be a junction) and re-claim idempotently on commit.
            } else if (occupant) {
                // Reading ourselves at the just-released old cell is a
                // U-turn in progress: reads see the old world, and the
                // park's wait is woken by our own committing release --
                // a U-turn costs a one-tick reversing pause.  Any other
                // occupant is traffic to queue behind.
                plan.disposition = ArrivalPlan::PARK_OCCUPIED;
                return plan;
            }

            // DO_NOT_QUEUE: entering a junction cell requires the cell
            // beyond it -- the exit, straight through, since a junction
            // marker never steers -- to be enterable too, and both are
            // claimed together, so a machine can never end up parked ON
            // the junction.  If the exit is not enterable we park HERE,
            // holding nothing, and cross-traffic uses the junction
            // freely.  A machine already holding the junction (the
            // pre-claim case above) extends the same rule one cell
            // onward when the exit is itself a junction; if that scan
            // would need to keep going, it instead parks holding what
            // it has -- the accepted O(1) imperfection of chained
            // junctions (machine_language.md 10.4).
            if (destination_value.is_opcode()
                && (destination_value.as_opcode() == OPCODE_DO_NOT_QUEUE)) {
                plan.beyond = step_from(plan.next_location, plan.next_heading);
                Term beyond_value = {};
                (void) tx->try_read_value_for_coordinate(plan.beyond,
                                                         beyond_value);
                EntityID beyond_occupant = {};
                (void) tx->try_read_entity_id_for_coordinate(plan.beyond,
                                                             beyond_occupant);
                if (beyond_occupant
                    || valve_blocks(beyond_value, plan.next_heading)) {
                    plan.disposition = ArrivalPlan::PARK_JUNCTION;
                    return plan;
                }
                plan.claim_beyond = true;
            }

            plan.disposition = ArrivalPlan::PROCEED;
            return plan;
        }

        // The latched memory op's effect at the cell we stand on.
        void apply_pending(const Machine* self, Machine* new_this,
                           Transaction* tx, const Term& cell_value) {
            switch (self->_on_arrival) {
                case OPCODE_SKIP:
                    break;
                case OPCODE_LOAD:
                    new_this->push(cell_value);
                    // matter is taken, not copied
                    if (cell_value.is_matter())
                        tx->write_value_for_coordinate(self->_new_location,
                                                       term_make_empty());
                    break;
                case OPCODE_STORE:
                    // the guard in plan_arrival already vetted this write
                    tx->write_value_for_coordinate(self->_new_location,
                                                   new_this->pop());
                    break;
                case OPCODE_EXCHANGE: {
                    // TODO: should push(...) itself discard nothings, always?
                    Term displaced = new_this->pop();
                    new_this->push(cell_value);
                    tx->write_value_for_coordinate(self->_new_location,
                                                   displaced);
                } break;
                default:
                    // To avoid explicit loads everywhere, when we run over
                    // something that
                    // - is not an opcode
                    // - is copyable, aka immaterial, symbolic, numeric?
                    // we pick it up.  Good idea?
                    if (cell_value.is_inty()) {
                        new_this->push(cell_value);
                    }
                    break;
            }
        }

    } // anonymous namespace

    int64_t Machine::notify(TransactionContext* context) const {

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
                if ((context->now() - _new_time) < 0) {
                    // This was a spurious wakeup
                    // printf("EntityID %lld experienced spurious wakeup\n", _entity_id.data);
                    return 0;
                }
                new_this->_phase = PHASE_WAITING_FOR_OLD;
            } [[fallthrough]];
                
            case PHASE_WAITING_FOR_OLD: {
                assert(_old_location != _new_location);
                EntityID occupant = {};
                (void) tx->try_read_entity_id_for_coordinate(_old_location, occupant);
                assert(occupant == this->_entity_id);
                tx->write_entity_id_for_coordinate(_old_location, EntityID{0});
                {
                    // location mirrors occupancy: rebuild the released
                    // cell's set without us (PersistentSet has no erase;
                    // sets here are tiny), preserving any non-occupying
                    // residents
                    WaitSet located;
                    (void) tx->try_read_located_for_coordinate(_old_location, located);
                    WaitSet without;
                    EntityID self = this->_entity_id;
                    located.for_each([&without, self](EntityID id) {
                        if (id != self)
                            without.set(id);
                    });
                    tx->write_located_for_coordinate(_old_location, without);
                }
                new_this->_old_time = _new_time;
                new_this->_old_location = _new_location;
                new_this->_old_heading = _new_heading;
                new_this->_phase = PHASE_WAITING_FOR_NEW;
            } [[fallthrough]];
                
            case PHASE_WAITING_FOR_NEW: {

                assert(new_this->_old_location == _new_location);

                // TODO: at the moment we have _on_arrival and next action to
                // coordinate stuff.  Can we instead use the top of the stack
                // itself as the instruction slot and current machine state?
                // (The thought latch, machine_language.md 10.5.)

                EntityID occupant = {};
                (void) tx->try_read_entity_id_for_coordinate(_new_location, occupant);
                assert(occupant == this->_entity_id);

                // Decide, then commit: the plan gathers every read and
                // chooses a disposition; writes happen only once we know
                // which path we are on.  See the interpreter banner at
                // the top of this file.
                ArrivalPlan plan = plan_arrival(this, tx);

                switch (plan.disposition) {

                    case ArrivalPlan::PARK_HALT:
                        // park; wake -- executing whatever is here by
                        // then -- when the value under us changes
                        new_this->_on_arrival = OPCODE_NOOP;
                        tx->write_entity_for_entity_id(this->_entity_id, new_this);
                        tx->wait_on_value_for_coordinate(_new_location);
                        return 0;

                    case ArrivalPlan::PARK_STORE_GUARD:
                        // the pending STORE may not act yet (see the
                        // guard in plan_arrival); retry when the cell
                        // changes
                        tx->write_entity_for_entity_id(this->_entity_id, new_this);
                        tx->wait_on_value_for_coordinate(_new_location);
                        tx->on_abort_retry();
                        return 0;

                    case ArrivalPlan::PARK_OCCUPIED:
                        tx->write_entity_for_entity_id(this->_entity_id, new_this);
                        // wait for our destination to clear
                        tx->wait_on_entity_id_for_coordinate(plan.next_location);
                        // or for the instruction under us to change
                        tx->wait_on_value_for_coordinate(_new_location);
                        // waiting can't fail; writing our next state can
                        // fail if somebody is messing with us, and then
                        // whoever wrote our state is responsible for
                        // scheduling us
                        tx->on_abort_retry();
                        return 0;

                    case ArrivalPlan::PARK_VALVE:
                        tx->write_entity_for_entity_id(this->_entity_id, new_this);
                        // wait for the valve ahead to flip open
                        tx->wait_on_value_for_coordinate(plan.next_location);
                        // or for the instruction under us to change
                        tx->wait_on_value_for_coordinate(_new_location);
                        tx->on_abort_retry();
                        return 0;

                    case ArrivalPlan::PARK_JUNCTION:
                        tx->write_entity_for_entity_id(this->_entity_id, new_this);
                        // wait for the junction's exit to open (occupant
                        // leaving, or a crossed valve there flipping)
                        tx->wait_on_entity_id_for_coordinate(plan.beyond);
                        tx->wait_on_value_for_coordinate(plan.beyond);
                        // or for the instruction under us to change
                        tx->wait_on_value_for_coordinate(_new_location);
                        tx->on_abort_retry();
                        return 0;

                    case ArrivalPlan::PROCEED:
                        break;
                }

                // Commit: claim the destination (occupancy, mirrored in
                // the location multimap), then apply this arrival's
                // effects
                tx->write_entity_for_entity_id(this->_entity_id, new_this);
                tx->write_entity_id_for_coordinate(plan.next_location, this->_entity_id);
                {
                    // location mirrors occupancy: join the claimed cell's
                    // set alongside any non-occupying residents
                    WaitSet located;
                    (void) tx->try_read_located_for_coordinate(plan.next_location, located);
                    located.set(this->_entity_id);
                    tx->write_located_for_coordinate(plan.next_location, located);
                }
                if (plan.claim_beyond) {
                    // DO_NOT_QUEUE: reserve the junction's exit in the
                    // same transaction, so we can never park ON the
                    // junction; the reservation is released by the
                    // ordinary old-cell release as we pass through
                    tx->write_entity_id_for_coordinate(plan.beyond, this->_entity_id);
                    WaitSet located;
                    (void) tx->try_read_located_for_coordinate(plan.beyond, located);
                    located.set(this->_entity_id);
                    tx->write_located_for_coordinate(plan.beyond, located);
                }

                apply_pending(this, new_this, tx, plan.cell_value);

                // Apply the executed action.  latched is what enters
                // _on_arrival: usually the action itself, but DROP of
                // matter latches STORE.  The steering operand was
                // identified by the planner; consume it here.
                i64 latched = plan.action;
                if (plan.steering_pop)
                    new_this->pop();

                switch (plan.action) {
                        
                    default:
                        // default: no action
                        break;
                        
                    case OPCODE_HALT:
                        // the planner parks HALT; it never proceeds here
                        abort();

                        // all of these opcodes just manipulate the entity
                        // state

                        // (BRANCH_LEFT / BRANCH_RIGHT / HEADING_STORE have
                        // no case here: their operand is the steering_pop
                        // consumed above)

                    case OPCODE_HEADING_LOAD:
                        new_this->push(Term(_new_heading));
                        break;
                        
                    case OPCODE_DROP:
                        // matter is not destroyed; it is put down in the
                        // next cell as if by STORE (waiting until empty)
                        if (new_this->peek().is_matter())
                            latched = OPCODE_STORE;
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

                        // valves flip their axis on every executed
                        // passage; the gate itself lives in plan_arrival
                    case OPCODE_VALVE_NORTH_SOUTH:
                        tx->write_value_for_coordinate(
                                                   _new_location,
                                                   term_make_opcode(OPCODE_VALVE_EAST_WEST));
                        break;
                    case OPCODE_VALVE_EAST_WEST:
                        tx->write_value_for_coordinate(
                                                   _new_location,
                                                   term_make_opcode(OPCODE_VALVE_NORTH_SOUTH));
                        break;
                                                
                } // switch (plan.action)

                // depart
                new_this->_on_arrival = latched;
                new_this->_new_heading = plan.next_heading;
                new_this->_new_location = plan.next_location;
                new_this->_old_time = context->now();
                new_this->_new_time = context->now() + 64;
                new_this->_phase = PHASE_TRAVELLING;
                tx->wait_on_time(new_this->_new_time);
                tx->on_abort_retry();
                return 0;
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

        EntityID test_machine_at_rest(World* w, i32 x, i32 y,
                                      i64 heading = HEADING_NORTH) {
            Machine* m = new Machine;
            m->_entity_id = w->generate_entity_id();
            m->_old_location = Coordinate{x, y};
            m->_new_location = Coordinate{x, y};
            m->_old_heading = heading;
            m->_new_heading = heading;
            w->_entity_for_entity_id.set(m->_entity_id, m);
            w->_entity_id_for_coordinate.set(Coordinate{x, y}, m->_entity_id);
            { WaitSet s; s.set(m->_entity_id);
              w->_located_for_coordinate.set(Coordinate{x, y}, s); }
            w->_waiting_on_time.set({Time{0}, m->_entity_id});
            return m->_entity_id;
        }

        // the machine (and nothing else here) is located at exactly (x, y)
        bool test_located_only_at(const Root<World*>& world, EntityID id,
                                  i32 x, i32 y,
                                  std::initializer_list<Coordinate> not_at) {
            WaitSet s{};
            (void) world._ptr->_located_for_coordinate.try_get(Coordinate{x, y}, s);
            if (!s.contains(id))
                return false;
            for (Coordinate c : not_at) {
                WaitSet t{};
                (void) world._ptr->_located_for_coordinate.try_get(c, t);
                if (t.contains(id))
                    return false;
            }
            return true;
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

        // Track F (x=50): TURN_BACK aims the machine at the cell it is
        // still releasing -- the U-turn.  It stalls one tick (woken by
        // its own committing release), re-enters, and retraces the track
        // southbound, re-picking the literal on the way out.
        EntityID mf = test_machine_at_rest(w, 50, 0);
        test_put(w, 50,  1, term_make_integer_with(7));
        test_put(w, 50,  2, term_make_opcode(OPCODE_TURN_BACK));
        test_put(w, 50, -1, term_make_opcode(OPCODE_HALT));

        // Track G (x=60): SKIP suppresses exactly one cell, both ways --
        // the skipped 9 is not picked up, and the skipped TURN_EAST is
        // not executed.
        EntityID mg = test_machine_at_rest(w, 60, 0);
        test_put(w, 60, 1, term_make_integer_with(5));
        test_put(w, 60, 2, term_make_opcode(OPCODE_SKIP));
        test_put(w, 60, 3, term_make_integer_with(9));
        test_put(w, 60, 4, term_make_opcode(OPCODE_SKIP));
        test_put(w, 60, 5, term_make_opcode(OPCODE_TURN_EAST));
        test_put(w, 60, 6, term_make_opcode(OPCODE_HALT));

        // Track H (x=70): LOAD as quote (copies an ADD glyph to the
        // stack without executing it; the source cell keeps its glyph),
        // STORE writing that glyph as code into an empty cell (without
        // executing what it stands on), then EXCHANGE swapping 8 for 42.
        EntityID mh = test_machine_at_rest(w, 70, 0);
        test_put(w, 70, 1, term_make_opcode(OPCODE_LOAD));
        test_put(w, 70, 2, term_make_opcode(OPCODE_ADD));
        test_put(w, 70, 3, term_make_opcode(OPCODE_STORE));
        // (70,4) empty: receives the quoted ADD
        test_put(w, 70, 5, term_make_integer_with(7));
        test_put(w, 70, 6, term_make_integer_with(8));
        test_put(w, 70, 7, term_make_opcode(OPCODE_EXCHANGE));
        test_put(w, 70, 8, term_make_integer_with(42));
        test_put(w, 70, 9, term_make_opcode(OPCODE_HALT));

        // Track I (x=80): EXCHANGE with an empty stack MOVES the cell
        // value (the cell empties), then the logical family chews
        // booleans and integers interchangeably.
        EntityID mi = test_machine_at_rest(w, 80, 0);
        test_put(w, 80, 1, term_make_opcode(OPCODE_EXCHANGE));
        test_put(w, 80, 2, term_make_integer_with(6));
        test_put(w, 80, 3, term_make_opcode(OPCODE_IS_POSITIVE));
        test_put(w, 80, 4, term_make_opcode(OPCODE_LOGICAL_NOT));
        test_put(w, 80, 5, term_make_integer_with(4));
        test_put(w, 80, 6, term_make_opcode(OPCODE_LOGICAL_OR));
        test_put(w, 80, 7, term_make_opcode(OPCODE_HALT));

        // Track J (x=90): two machines queue through one FLIP_FLOP.
        // The leader takes it as TURN_RIGHT (glyph becomes FLOP_FLIP);
        // the follower -- which first waits twice for the leader's cells
        // to clear -- takes it as TURN_LEFT (glyph restored).  The
        // follower's heading winds to -1, reduced mod 4 at the assert.
        EntityID mj1 = test_machine_at_rest(w, 90,  0);
        EntityID mj2 = test_machine_at_rest(w, 90, -1);
        test_put(w, 90, 1, term_make_opcode(OPCODE_FLIP_FLOP));
        test_put(w, 91, 1, term_make_opcode(OPCODE_HALT));
        test_put(w, 89, 1, term_make_opcode(OPCODE_HALT));

        // Track K (x=100): the STORE guard parks.  The machine takes the
        // container, then its STORE finds the target cell holding a 9 --
        // matter needs emptiness, so it parks on the cell.  A Sink AT
        // that cell (non-occupying: located, sharing the cell with the
        // parked machine -- the occupancy/location split at work) is
        // scheduled to wake at t=300, eats the 9, and the value change
        // wakes the machine, which places the container and moves on
        // (the Sink then lawfully consumes it).  Arrival time at the
        // HALT proves the park happened: unblocked it would be 320.
        EntityID mk = test_machine_at_rest(w, 100, 0);
        test_put(w, 100, 1, term_make_opcode(OPCODE_LOAD));
        test_put(w, 100, 2, term_make_matter(MATTER_SHIPPING_CONTAINER));
        test_put(w, 100, 3, term_make_opcode(OPCODE_STORE));
        test_put(w, 100, 4, term_make_integer_with(9));
        test_put(w, 100, 5, term_make_opcode(OPCODE_HALT));
        Sink* sink = new Sink;
        sink->_location = Coordinate{100, 4};
        w->_entity_for_entity_id.set(sink->_entity_id, sink);
        { WaitSet s; s.set(sink->_entity_id);
          w->_located_for_coordinate.set(sink->_location, s); }
        w->_waiting_on_time.set({Time{300}, sink->_entity_id});
        EntityID sink_id = sink->_entity_id;

        // Track L (x=103..111): a boolean ground signal between two
        // machines.  The producer computes 4 == 4 and STOREs true at
        // (110,5); the consumer, arriving from the west well after the
        // producer has passed (it briefly queues on the producer's
        // release of the shared cell), auto-picks the boolean COPY and
        // branches south on it.
        EntityID ml_producer = test_machine_at_rest(w, 110, 0);
        test_put(w, 110, 1, term_make_integer_with(4));
        test_put(w, 110, 2, term_make_integer_with(4));
        test_put(w, 110, 3, term_make_opcode(OPCODE_EQUAL));
        test_put(w, 110, 4, term_make_opcode(OPCODE_STORE));
        // (110,5) empty: receives true
        test_put(w, 110, 6, term_make_opcode(OPCODE_HALT));
        EntityID ml_consumer = test_machine_at_rest(w, 103, 5, HEADING_EAST);
        test_put(w, 111, 5, term_make_opcode(OPCODE_BRANCH_RIGHT));
        test_put(w, 111, 4, term_make_opcode(OPCODE_HALT));

        // Track M (x=115): an aligned machine passes through a valve,
        // flipping its axis.
        EntityID mm = test_machine_at_rest(w, 115, 0);
        test_put(w, 115, 1, term_make_opcode(OPCODE_VALVE_NORTH_SOUTH));
        test_put(w, 115, 2, term_make_opcode(OPCODE_HALT));

        // Track N (x=117..121): the turnstile.  A, eastbound, parks
        // against the crossed valve at (120,5).  B, northbound, passes
        // through it (flipping it east-west), which wakes A; A queues
        // briefly on B's occupancy, then passes through the now-open
        // valve, flipping it BACK to north-south on the way out.
        // Unblocked, A would reach its HALT at t=256; the valve wait
        // pushes it past 400.
        EntityID mn_a = test_machine_at_rest(w, 117, 5, HEADING_EAST);
        EntityID mn_b = test_machine_at_rest(w, 120, 1);
        test_put(w, 120, 5, term_make_opcode(OPCODE_VALVE_NORTH_SOUTH));
        test_put(w, 120, 6, term_make_opcode(OPCODE_HALT));
        test_put(w, 121, 5, term_make_opcode(OPCODE_HALT));

        // Track O (x=130): ghost passage.  A SKIP-latched machine
        // enters an ALIGNED valve as data: the gate admits it (axis
        // matches) but nothing executes, so the valve does not flip.
        EntityID mo = test_machine_at_rest(w, 130, 0);
        test_put(w, 130, 1, term_make_opcode(OPCODE_SKIP));
        test_put(w, 130, 2, term_make_opcode(OPCODE_VALVE_NORTH_SOUTH));
        test_put(w, 130, 3, term_make_opcode(OPCODE_HALT));

        // Track P (x=140): a clear DO_NOT_QUEUE junction costs nothing:
        // the machine claims junction and exit together, rolls through
        // at full speed, and releases both behind it.
        EntityID mp = test_machine_at_rest(w, 140, 0);
        test_put(w, 140, 2, term_make_opcode(OPCODE_DO_NOT_QUEUE));
        test_put(w, 140, 4, term_make_opcode(OPCODE_HALT));

        // Track Q (x=142..147): the box-junction rule.  A, eastbound,
        // wants through the junction at (145,5), but the exit (146,5)
        // is held by a parked blocker.  A parks BEFORE the junction,
        // claiming nothing.  B, northbound, crosses the same junction
        // freely while A waits.  Under plain queueing A would advance
        // onto the junction and B would never get through -- B's final
        // position is the non-vacuous assert.
        EntityID mq_a = test_machine_at_rest(w, 142, 5, HEADING_EAST);
        EntityID mq_b = test_machine_at_rest(w, 145, 2);
        EntityID mq_blocker = test_machine_at_rest(w, 146, 5);
        test_put(w, 146, 5, term_make_opcode(OPCODE_HALT));   // parks the blocker in place
        test_put(w, 145, 5, term_make_opcode(OPCODE_DO_NOT_QUEUE));
        test_put(w, 145, 7, term_make_opcode(OPCODE_HALT));

        w->hack_repair_invariant();
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

        // The location multimap tracked every move: each parked machine is
        // located at exactly its HALT cell, and the sets along its track
        // (start cell, a mid-track cell, the pre-turn corner) emptied
        // behind it.
        assert((test_located_only_at(world, ma, 0, 11,
                                     {Coordinate{0, 0}, Coordinate{0, 5},
                                      Coordinate{0, 10}})));
        assert((test_located_only_at(world, mb, 12, 4,
                                     {Coordinate{10, 0}, Coordinate{10, 6},
                                      Coordinate{12, 6}})));
        assert((test_located_only_at(world, mc, 21, 4,
                                     {Coordinate{20, 0}, Coordinate{20, 4}})));
        assert((test_located_only_at(world, md, 30, 5,
                                     {Coordinate{30, 0}, Coordinate{30, 4}})));
        assert((test_located_only_at(world, me, 43, 5,
                                     {Coordinate{40, 0}, Coordinate{40, 2},
                                      Coordinate{41, 5}, Coordinate{42, 5}})));

        {
            const Machine* m = test_machine_for(world, mf);
            assert((m->_new_location == Coordinate{50, -1}));
            assert(m->_new_heading == HEADING_SOUTH);
            assert((test_stack_is(m, {term_make_integer_with(7),
                                      term_make_integer_with(7)})));
        }
        assert((test_located_only_at(world, mf, 50, -1,
                                     {Coordinate{50, 0}, Coordinate{50, 1},
                                      Coordinate{50, 2}})));

        {
            // Track G: the 9 was not picked up, the TURN_EAST was not
            // executed
            const Machine* m = test_machine_for(world, mg);
            assert((m->_new_location == Coordinate{60, 6}));
            assert(m->_new_heading == HEADING_NORTH);
            assert((test_stack_is(m, {term_make_integer_with(5)})));
        }

        {
            // Track H: quote copied (source glyph intact), code written,
            // exchange swapped
            const Machine* m = test_machine_for(world, mh);
            assert((m->_new_location == Coordinate{70, 9}));
            assert((test_stack_is(m, {term_make_integer_with(7),
                                      term_make_integer_with(42)})));
            Term quoted{};
            (void) world._ptr->_term_for_coordinate.try_get(Coordinate{70, 2}, quoted);
            assert(quoted._data == term_make_opcode(OPCODE_ADD)._data);
            Term written{};
            (void) world._ptr->_term_for_coordinate.try_get(Coordinate{70, 4}, written);
            assert(written._data == term_make_opcode(OPCODE_ADD)._data);
            Term swapped{};
            (void) world._ptr->_term_for_coordinate.try_get(Coordinate{70, 8}, swapped);
            assert(swapped._data == term_make_integer_with(8)._data);
        }

        {
            // Track I: exchange-with-empty-stack moved the 6 out of the
            // cell; the logical family accepted the bool/int mix
            const Machine* m = test_machine_for(world, mi);
            assert((m->_new_location == Coordinate{80, 7}));
            assert((test_stack_is(m, {Term(true)})));
            Term moved{};
            (void) world._ptr->_term_for_coordinate.try_get(Coordinate{80, 2}, moved);
            assert(term_is_null(moved));
        }

        {
            // Track J: leader turned right, follower (after queueing on
            // the leader twice) turned left, and the glyph flipped twice
            // back to FLIP_FLOP.  The follower's heading wound to -1;
            // reduce mod 4.
            const Machine* m1 = test_machine_for(world, mj1);
            assert((m1->_new_location == Coordinate{91, 1}));
            assert(m1->_new_heading == HEADING_EAST);
            const Machine* m2 = test_machine_for(world, mj2);
            assert((m2->_new_location == Coordinate{89, 1}));
            assert((m2->_new_heading & 3) == HEADING_WEST);
            Term glyph{};
            (void) world._ptr->_term_for_coordinate.try_get(Coordinate{90, 1}, glyph);
            assert(glyph._data == term_make_opcode(OPCODE_FLIP_FLOP)._data);
            assert((test_located_only_at(world, mj1, 91, 1,
                                         {Coordinate{90, 0}, Coordinate{90, 1}})));
            assert((test_located_only_at(world, mj2, 89, 1,
                                         {Coordinate{90, -1}, Coordinate{90, 0},
                                          Coordinate{90, 1}})));
        }

        {
            // Track K: the STORE guard parked on the 9 until the Sink ate
            // it (arrival at the HALT would be t=320 unblocked; the park
            // pushes it past 360), the container was placed and then
            // lawfully sunk, and the parked machine shared the Sink's
            // cell in the location map while never displacing it
            const Machine* m = test_machine_for(world, mk);
            assert((m->_new_location == Coordinate{100, 5}));
            assert((test_stack_is(m, {})));
            assert(m->_new_time >= Time{360});
            Term taken{};
            (void) world._ptr->_term_for_coordinate.try_get(Coordinate{100, 2}, taken);
            assert(term_is_null(taken));
            Term sunk{};
            (void) world._ptr->_term_for_coordinate.try_get(Coordinate{100, 4}, sunk);
            assert(term_is_null(sunk));
            WaitSet at_sink{};
            (void) world._ptr->_located_for_coordinate.try_get(Coordinate{100, 4}, at_sink);
            assert(at_sink.contains(sink_id));
            assert(!at_sink.contains(mk));
            assert((test_located_only_at(world, mk, 100, 5,
                                         {Coordinate{100, 0}, Coordinate{100, 4}})));
        }

        {
            // Track L: the producer stored true on the ground; the
            // consumer picked up a COPY (the signal survives) and
            // branched south on it
            const Machine* p = test_machine_for(world, ml_producer);
            assert((p->_new_location == Coordinate{110, 6}));
            assert((test_stack_is(p, {})));
            const Machine* c = test_machine_for(world, ml_consumer);
            assert((c->_new_location == Coordinate{111, 4}));
            assert(c->_new_heading == HEADING_SOUTH);
            assert((test_stack_is(c, {})));
            Term signal{};
            (void) world._ptr->_term_for_coordinate.try_get(Coordinate{110, 5}, signal);
            assert(signal._data == Term(true)._data);
        }

        {
            // Track M: passage flipped the valve's axis
            const Machine* m = test_machine_for(world, mm);
            assert((m->_new_location == Coordinate{115, 2}));
            Term valve{};
            (void) world._ptr->_term_for_coordinate.try_get(Coordinate{115, 1}, valve);
            assert(valve._data == term_make_opcode(OPCODE_VALVE_EAST_WEST)._data);
        }

        {
            // Track N: the turnstile round-trip.  A was genuinely held
            // (256 unblocked, past 400 held), both machines are through,
            // and the valve flipped twice back to north-south.
            const Machine* a2 = test_machine_for(world, mn_a);
            assert((a2->_new_location == Coordinate{121, 5}));
            assert(a2->_new_heading == HEADING_EAST);
            assert(a2->_new_time >= Time{400});
            const Machine* b2 = test_machine_for(world, mn_b);
            assert((b2->_new_location == Coordinate{120, 6}));
            assert(b2->_new_heading == HEADING_NORTH);
            Term valve{};
            (void) world._ptr->_term_for_coordinate.try_get(Coordinate{120, 5}, valve);
            assert(valve._data == term_make_opcode(OPCODE_VALVE_NORTH_SOUTH)._data);
        }

        {
            // Track O: the ghost passage left the valve unflipped
            const Machine* m = test_machine_for(world, mo);
            assert((m->_new_location == Coordinate{130, 3}));
            assert((test_stack_is(m, {})));
            Term valve{};
            (void) world._ptr->_term_for_coordinate.try_get(Coordinate{130, 2}, valve);
            assert(valve._data == term_make_opcode(OPCODE_VALVE_NORTH_SOUTH)._data);
        }

        {
            // Track P: the clear junction cost nothing (parked at the
            // usual time for a 4-hop track) and both claimed cells were
            // released behind the machine
            const Machine* m = test_machine_for(world, mp);
            assert((m->_new_location == Coordinate{140, 4}));
            assert(m->_new_time == Time{256});
            assert((test_located_only_at(world, mp, 140, 4,
                                         {Coordinate{140, 2}, Coordinate{140, 3}})));
            EntityID junction_occupant = {};
            (void) world._ptr->_entity_id_for_coordinate.try_get(Coordinate{140, 2},
                                                                 junction_occupant);
            assert(!junction_occupant);
        }

        {
            // Track Q: A parked BEFORE the junction, holding nothing (it
            // never re-departed after t=128, and the junction's
            // occupancy is clear), while B crossed and parked beyond
            const Machine* a2 = test_machine_for(world, mq_a);
            assert((a2->_new_location == Coordinate{144, 5}));
            assert(a2->_new_heading == HEADING_EAST);
            assert(a2->_new_time <= Time{200});
            const Machine* b2 = test_machine_for(world, mq_b);
            assert((b2->_new_location == Coordinate{145, 7}));
            assert(b2->_new_heading == HEADING_NORTH);
            const Machine* blocker2 = test_machine_for(world, mq_blocker);
            assert((blocker2->_new_location == Coordinate{146, 5}));
            EntityID junction_occupant = {};
            (void) world._ptr->_entity_id_for_coordinate.try_get(Coordinate{145, 5},
                                                                 junction_occupant);
            assert(!junction_occupant);
            WaitSet at_junction{};
            (void) world._ptr->_located_for_coordinate.try_get(Coordinate{145, 5},
                                                               at_junction);
            assert(!at_junction.contains(mq_a));
            assert(!at_junction.contains(mq_b));
        }

        co_return;
    };

} // namespace wry::sim
