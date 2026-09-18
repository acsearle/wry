//
//  player.cpp
//  client
//
//  Created by Antony Searle on 18/10/2025.
//

#include "player.hpp"
#include "transaction.hpp"
#include "world.hpp"

#include "epoch.hpp"
#include "spawner.hpp"
#include "test.hpp"

namespace wry {
    
    void garbage_collected_scan(const Player::Action& x) {
        switch (x.tag) {
            case Player::Action::NONE:
                break;
            case Player::Action::WRITE_VALUE_FOR_COORDINATE:
                garbage_collected_scan(x.coordinate);
                garbage_collected_scan(x.value);
                break;
            default:
                abort();
        }
    }
    
    void Player::_garbage_collected_scan() const {
        garbage_collected_scan(_queue);
    }

    
    int64_t Player::notify(TransactionContext* context) const {
        
        // Always wait again.  WAIT_ALWAYS, not the WAIT_ON_COMMIT default:
        // the write below is exclusive, and when it loses a same-cell
        // conflict the whole transaction aborts.  A commit-only wait would
        // abort with it, and nothing else ever re-schedules the player, so
        // one lost write would silence every later one (the queue would
        // fill and never drain again).  See player_survives_write_conflict.
        Transaction* tx = Transaction::make(context, this, 2);
        tx->wait_on_time(context->next_now(),
                         Transaction::Operation::WAIT_ALWAYS);
        
        Action action = {};
        if (_queue.try_pop_front(action)) {
            switch (action.tag) {
                case Player::Action::NONE:
                    break;
                case Player::Action::WRITE_VALUE_FOR_COORDINATE:
                    tx->write_value_for_coordinate(action.coordinate, action.value);
                    break;
                default:
                    abort();
            };
        }

        return 0;
    }
    

    // ----------------------------------------------------------------
    // Tests.

    namespace {

        Coroutine::Task player_test_step(Root<World*>& world) {
            // A pin in this frame holds the epoch floor for step's
            // epoch-allocated transaction structures across the await
            // (see machine.cpp's test_step_until).
            epoch::Epoch our_pin = pin_global_epoch();
            Root<World*> next = co_await world._ptr->step();
            unpin_global_epoch(our_pin);
            assert(next._ptr);
            world = std::move(next);
        }

        void player_test_submit(Player const* p, Coordinate xy, Term value) {
            Player::Action a;
            a.tag = Player::Action::WRITE_VALUE_FOR_COORDINATE;
            a.coordinate = xy;
            a.value = value;
            p->_queue.push_back(std::move(a));
        }

    } // anonymous namespace

    // The player must stay scheduled however its writes resolve.
    //
    // Regression for the 2026-09 GUI failure where world edits stopped
    // landing part-way through a session: a user write that lost a
    // same-cell, same-tick conflict aborted the player's transaction, and
    // the next-tick wait in that same transaction -- registered
    // WAIT_ON_COMMIT -- went down with it.  Nothing else ever re-schedules
    // the player, so it was never notified again and its input queue was
    // never drained: every later click and hex key was silently queued
    // forever.
    //
    // A Sink empties its cell whenever the cell holds a value, so writing
    // a value there every tick guarantees a same-key conflict on every
    // tick the cell starts non-empty.  Priority is a per-tick hash of
    // (EntityID, time), so over 64 ticks the player loses some of them.
    define_test("player_survives_write_conflict") {

        World* w = new World;

        Player* p = new Player;
        p->_entity_id = w->generate_entity_id();
        w->_entity_for_entity_id.set(p->_entity_id, p);
        w->_waiting_on_time.set({Time{0}, p->_entity_id});
        const EntityID player_id = p->_entity_id;

        Sink* sink = new Sink;
        sink->_entity_id = w->generate_entity_id();
        sink->_location = Coordinate{3, 3};
        w->_entity_for_entity_id.set(sink->_entity_id, sink);
        { WaitSet s; s.set(sink->_entity_id);
          w->_located_for_coordinate.set(sink->_location, s); }
        w->_waiting_on_time.set({Time{0}, sink->_entity_id});

        w->hack_repair_invariant();
        Root<World*> world{w};

        auto player_is_scheduled = [&world, player_id]() -> bool {
            return world._ptr->_ready.find(player_id)
                != world._ptr->_ready.end();
        };
        assert(player_is_scheduled());

        // One action per tick: the player pops exactly one per notify.
        const Coordinate contended = sink->_location;
        int ticks_lost = 0;
        for (int i = 0; i != 64; ++i) {
            player_test_submit(p, contended, term_make_integer_with(7));
            co_await player_test_step(world);
            // A live player writes 7 into an empty cell unopposed, so the
            // cell is empty after a step only when the sink out-prioritized
            // the player's write (or the player is dead, caught below).
            Term after{};
            (void) world._ptr->_term_for_coordinate.try_get(contended, after);
            if (term_is_null(after))
                ++ticks_lost;
            assert(player_is_scheduled());
        }
        // Non-vacuity: the player really did lose contested ticks.
        assert(ticks_lost > 0);

        // The queue kept draining (one action per tick, one tick per
        // action), and an uncontended write still lands on the next step.
        {
            Player::Action leftover;
            bool had_leftover = p->_queue.try_pop_front(leftover);
            assert(!had_leftover);
        }
        const Coordinate quiet = Coordinate{5, 5};
        player_test_submit(p, quiet, term_make_integer_with(42));
        co_await player_test_step(world);
        {
            Term landed{};
            (void) world._ptr->_term_for_coordinate.try_get(quiet, landed);
            assert(landed._data == term_make_integer_with(42)._data);
        }
        assert(player_is_scheduled());

        co_return;
    };

} // namespace wry
