//
//  player.cpp
//  client
//
//  Created by Antony Searle on 18/10/2025.
//

#include <algorithm>
#include <utility>
#include <vector>

#include "player.hpp"
#include "server.hpp"
#include "transaction.hpp"
#include "world.hpp"

#include "epoch.hpp"
#include "spawner.hpp"
#include "test.hpp"

namespace wry {

    int64_t Player::notify(TransactionContext* context) const {

        // This step's actions from the clients the World maps to this
        // Player, in the server's order.  A transaction writes a key at
        // most once (the rebuild asserts a single committed writer per
        // key), so within a step the last write to a cell wins.
        std::vector<std::pair<Coordinate, Term>> writes;
        for (Command const& c : context->_commands) {
            if (c.kind != Command::ACTION)
                continue;
            EntityID who{};
            if (!context->_world->_player_for_client.try_get(c.origin, who) || (who != _entity_id))
                continue;
            switch (c.action.tag) {
                case Action::NONE:
                    break;
                case Action::WRITE_VALUE_FOR_COORDINATE: {
                    const Coordinate at = c.action.coordinate;
                    auto it = std::find_if(writes.begin(), writes.end(),
                                           [at](auto const& w) { return w.first == at; });
                    if (it == writes.end())
                        writes.emplace_back(at, c.action.value._value);
                    else
                        it->second = c.action.value._value;
                    break;
                }
            }
        }

        // Always wait again.  WAIT_ALWAYS, not the WAIT_ON_COMMIT default:
        // the writes below are exclusive, and when one loses a same-cell
        // conflict the whole transaction aborts.  A commit-only wait would
        // abort with it, and nothing else ever re-schedules the player, so
        // one lost write would silence every later one.  See
        // player_survives_write_conflict.
        Transaction* tx = Transaction::make(context, this, 1 + writes.size());
        tx->wait_on_time(context->next_now(),
                         Transaction::Operation::WAIT_ALWAYS);
        for (auto const& [at, value] : writes)
            tx->write_value_for_coordinate(at, value);

        return 0;
    }


    // ----------------------------------------------------------------
    // Tests.

    namespace {

        constexpr ClientID TEST_CLIENT = 1;

        coroutine::Task player_test_step(Root<World*>& world, std::vector<Command> const& commands = {}) {
            // A pin in this frame holds the epoch floor for step's
            // epoch-allocated transaction structures across the await
            // (see machine.cpp's test_step_until).
            epoch::Epoch our_pin = pin_global_epoch();
            Root<World*> next = co_await world._ptr->step(commands);
            unpin_global_epoch(our_pin);
            assert(next._ptr);
            world = std::move(next);
        }

        Command player_test_write(Coordinate xy, Term value, ClientID client = TEST_CLIENT) {
            return make_action_command(client, 0, make_write_action(xy, value));
        }

        // A world with a Player mapped to TEST_CLIENT, ready at time 0
        Player const* install_test_player(World* w) {
            Player* p = new Player;
            p->_entity_id = w->generate_entity_id();
            p->seal();
            w->_entity_for_entity_id.set(p->_entity_id, p);
            w->_waiting_on_time.set({Time{0}, p->_entity_id});
            w->_player_for_client.set(TEST_CLIENT, p->_entity_id);
            return p;
        }

        bool cell_holds(World const* w, Coordinate xy, Term value) {
            Term t{};
            return w->_term_for_coordinate.try_get(xy, t) && (t._data == value._data);
        }

        bool cell_empty(World const* w, Coordinate xy) {
            Term t{};
            return !w->_term_for_coordinate.try_get(xy, t);
        }

    } // anonymous namespace

    // The player must stay scheduled however its writes resolve.
    //
    // Regression for the 2026-09 GUI failure where world edits stopped
    // landing part-way through a session: a user write that lost a
    // same-cell, same-tick conflict aborted the player's transaction, and
    // the next-tick wait in that same transaction -- registered
    // WAIT_ON_COMMIT -- went down with it.  Nothing else ever re-schedules
    // the player, so it was never notified again and its input was never
    // applied: every later click and hex key was silently dropped.
    //
    // A Sink empties its cell whenever the cell holds a value, so writing
    // a value there every tick guarantees a same-key conflict on every
    // tick the cell starts non-empty.  Priority is a per-tick hash of
    // (EntityID, time), so over 64 ticks the player loses some of them.
    define_test("player_survives_write_conflict") {

        World* w = new World;

        Player const* p = install_test_player(w);
        const EntityID player_id = p->_entity_id;

        Sink* sink = new Sink;
        sink->_entity_id = w->generate_entity_id();
        sink->_location = Coordinate{3, 3};
        sink->seal();
        w->_entity_for_entity_id.set(sink->_entity_id, sink);
        { EntityIDSet s; s.set(sink->_entity_id);
          w->_located_for_coordinate.set(sink->_location, s); }
        w->_waiting_on_time.set({Time{0}, sink->_entity_id});

        w->hack_repair_invariant();
        Root<World*> world{w};

        auto player_is_scheduled = [&world, player_id]() -> bool {
            return world._ptr->_ready.find(player_id)
                != world._ptr->_ready.end();
        };
        assert(player_is_scheduled());

        // One action per tick, as the step's input.
        const Coordinate contended = sink->_location;
        int ticks_lost = 0;
        for (int i = 0; i != 64; ++i) {
            co_await player_test_step(world, {player_test_write(contended, term_make_integer_with(7))});
            // A live player writes 7 into an empty cell unopposed, so the
            // cell is empty after a step only when the sink out-prioritized
            // the player's write (or the player is dead, caught below).
            if (cell_empty(world._ptr, contended))
                ++ticks_lost;
            assert(player_is_scheduled());
        }
        // Non-vacuity: the player really did lose contested ticks.
        assert(ticks_lost > 0);

        // An uncontended write still lands on the next step.
        const Coordinate quiet = Coordinate{5, 5};
        co_await player_test_step(world, {player_test_write(quiet, term_make_integer_with(42))});
        assert(cell_holds(world._ptr, quiet, term_make_integer_with(42)));
        assert(player_is_scheduled());

        co_return;
    };

    // A committed write of the null Term erases the cell's key: absence is
    // the one representation of "no term".  (Until 2026-09-21 the null was
    // stored; nothing could read it back as different from absent, but
    // the minimap plotted it and the ground renderer drew a dot.)
    define_test("player_null_write_erases_key") {

        World* w = new World;
        (void) install_test_player(w);
        w->hack_repair_invariant();
        Root<World*> world{w};

        const Coordinate xy{5, 5};
        co_await player_test_step(world, {player_test_write(xy, term_make_integer_with(7))});
        assert(cell_holds(world._ptr, xy, term_make_integer_with(7)));

        co_await player_test_step(world, {player_test_write(xy, term_make_null())});
        assert(cell_empty(world._ptr, xy));

        // Erasing a cell that was never written inserts nothing.
        const Coordinate never{6, 6};
        co_await player_test_step(world, {player_test_write(never, term_make_null())});
        assert(cell_empty(world._ptr, never));

        co_return;
    };

    // The step's input is explicit (multiplayer.md 9.2): server events
    // spawn and retire Players deterministically, actions route by the
    // World's client map, and nothing outside the World decides a step.
    define_test("player_join_leave") {

        World* w = new World;
        w->hack_repair_invariant();
        const EntityID cursor = w->_entity_id_source;

        // Two lineages from one base, given the same JOIN: the same
        // Player id, the same mapping, the same hash
        Root<World*> a{w};
        Root<World*> b{w};
        co_await player_test_step(a, {make_join_command(5)});
        co_await player_test_step(b, {make_join_command(5)});
        EntityID pa{}, pb{};
        assert(a._ptr->_player_for_client.try_get(5, pa) && (pa == cursor));
        assert(b._ptr->_player_for_client.try_get(5, pb) && (pb == pa));
        assert(a._ptr->content_hash() == b._ptr->content_hash());
        assert(a._ptr->_entity_id_source.data == cursor.data + 1);
        {
            Entity const* e = nullptr;
            assert(a._ptr->_entity_for_entity_id.try_get(pa, e));
            assert(dynamic_cast<Player const*>(e));
        }
        // Scheduled from the step after the JOIN
        assert(a._ptr->_ready.find(pa) != a._ptr->_ready.end());

        // An action in the JOIN's own step is too early and is dropped;
        // from the next step the client's actions land
        const Coordinate xy{1, 1};
        co_await player_test_step(a, {make_join_command(6), player_test_write(xy, term_make_integer_with(9), 6)});
        assert(cell_empty(a._ptr, xy));
        co_await player_test_step(a, {player_test_write(xy, term_make_integer_with(9), 6)});
        assert(cell_holds(a._ptr, xy, term_make_integer_with(9)));

        // A JOIN for a mapped client is a no-op: no new entity, same id
        {
            const EntityID before = a._ptr->_entity_id_source;
            co_await player_test_step(a, {make_join_command(5)});
            EntityID again{};
            assert(a._ptr->_player_for_client.try_get(5, again) && (again == pa));
            assert(a._ptr->_entity_id_source == before);
        }

        // An action from a client the World does not map is dropped
        const Coordinate stray{2, 2};
        co_await player_test_step(a, {player_test_write(stray, term_make_integer_with(1), 9)});
        assert(cell_empty(a._ptr, stray));

        // Several actions in one step from one client all land; the last
        // write to a cell wins
        const Coordinate c3{3, 3}, c4{4, 4};
        co_await player_test_step(a, {
            player_test_write(c3, term_make_integer_with(1), 5),
            player_test_write(c4, term_make_integer_with(2), 5),
            player_test_write(c3, term_make_integer_with(3), 5),
        });
        assert(cell_holds(a._ptr, c3, term_make_integer_with(3)));
        assert(cell_holds(a._ptr, c4, term_make_integer_with(2)));

        // LEAVE unmaps the client; its Player stays, idle, and its
        // actions no longer route anywhere
        co_await player_test_step(a, {make_leave_command(5)});
        assert(!a._ptr->_player_for_client.contains(5));
        {
            Entity const* e = nullptr;
            assert(a._ptr->_entity_for_entity_id.try_get(pa, e) && e);
        }
        co_await player_test_step(a, {player_test_write(stray, term_make_integer_with(1), 5)});
        assert(cell_empty(a._ptr, stray));

        // The clock events are not the World's business
        co_await player_test_step(a, {Command{Command::PAUSED}, Command{Command::RESUMED}});

        co_return;
    };

} // namespace wry
