//
//  world_state.cpp
//  client
//
//  Created by Antony Searle on 8/7/2023.
//

#include "assert.hpp"
#include "world_state.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>

#include <unistd.h>

#include "base36.hpp"
#include "command_types.hpp"
#include "coroutine.hpp"
#include "host.hpp"
#include "kqueue_reactor.hpp"
#include "ctype.hpp"
#include "garbage_collected.hpp"
#include "machine.hpp"
#include "matter.hpp"
#include "save.hpp"
#include "save_format.hpp"
#include "session_log.hpp"
#include "terrain.hpp"
#include "test.hpp"
#include "test_support.hpp"
#include "world_dump.hpp"

namespace wry {

    namespace {

        // The starting scenario.  Extracted verbatim from the original model
        // constructor so NEW, restart, and the round-trip tests can all build
        // the same world.  No Player: a client's Player is spawned by the
        // server's JOIN event in the first step (World::step), as for any
        // client joining any game.
        //
        World* make_starting_world() {
            World* world = new World;

            world->_seed = WORLD_SEED_DEFAULT;
            generate_starting_terrain(world);

            auto insert_localized_entity = [&](LocalizedEntity const* entity_ptr) {
                EntityID entity_id = entity_ptr->_entity_id;
                entity_ptr->seal();
                world->_entity_for_entity_id.set(entity_id, entity_ptr);
                world->_waiting_on_time.set({Time{0}, entity_id});
                // located, but not occupying: statics live only in the
                // location multimap
                EntityIDSet located;
                (void) world->_located_for_coordinate.try_get(entity_ptr->_location, located);
                located.set(entity_id);
                world->_located_for_coordinate.set(entity_ptr->_location, located);
            };

            {
                // new machine spawner at origin
                Spawner* p = new Spawner;
                p->_entity_id = world->generate_entity_id();
                p->_free_entity_id = world->generate_entity_id();
                p->_location = Coordinate{0, 0};
                insert_localized_entity(p);
            }

            /*
            {
                // value source
                Source* q = new Source;
                q->_entity_id = world->generate_entity_id();
                q->_location = Coordinate{2, 2};
                q->_of_this = Term(1);
                insert_localized_entity(q);
            }

            {
                // value sink
                Sink* r = new Sink;
                r->_entity_id = world->generate_entity_id();
                r->_location = Coordinate{4, 2};
                insert_localized_entity(r);
            }
             */

            world->_term_for_coordinate.set(Coordinate{-2, -2}, term_make_integer_with(7));
            world->_term_for_coordinate.set(Coordinate{0, +1}, term_make_integer_with(1));
            world->_term_for_coordinate.set(Coordinate{0, +2}, term_make_integer_with(2));
            world->_term_for_coordinate.set(Coordinate{0, +3}, term_make_integer_with(3));
            world->_term_for_coordinate.set(Coordinate{0, +4}, term_make_opcode(OPCODE_FLIP_FLOP));
            world->_term_for_coordinate.set(Coordinate{0, +5}, term_make_integer_with(5));

            // Materials demo, south of the spawner's tracks (its machines
            // leave north up x = 0 and split along y = 4).
            {
                auto put = [&](i32 x, i32 y, Term t) {
                    world->_term_for_coordinate.set(Coordinate{x, y}, t);
                };
                auto machine_at_rest = [&](i32 x, i32 y, i64 heading) {
                    Machine* m = new Machine;
                    m->_entity_id = world->generate_entity_id();
                    m->_old_location = Coordinate{x, y};
                    m->_new_location = Coordinate{x, y};
                    m->_old_heading = heading;
                    m->_new_heading = heading;
                    m->seal();
                    world->_entity_for_entity_id.set(m->_entity_id, m);
                    world->_entity_id_for_coordinate.set(Coordinate{x, y}, m->_entity_id);
                    EntityIDSet located;
                    located.set(m->_entity_id);
                    world->_located_for_coordinate.set(Coordinate{x, y}, located);
                    world->_waiting_on_time.set({Time{0}, m->_entity_id});
                };

                // A pile of each terrain matter, WATER (north) to
                // LIMESTONE (south), on the grass at x = -5
                for (i32 k = 0; k != 7; ++k)
                    put(-5, -k, term_make_matter((MATTER)(MATTER_WATER + k)));

                // The market: a Ship moored on the south side of open
                // water, prow west, alongside a stone pier.  Not placed by
                // hand but found in the generated terrain: the berth
                // nearest the spawner, not south of it, whose row holds
                // the ship with a ship length of water off either end and
                // water north of it, and whose row to the south (the pier)
                // touches land.  The pier's cells become rock, whatever
                // they were.  No such berth, no ship (pre-alpha: fine).
                {
                    // the search makes terrain as it reads it, so it is
                    // not bounded by the starting block (terrain.hpp)
                    auto water = [&](i32 x, i32 y) {
                        (void) ensure_terrain_chunk(world->_seed, world->_terrain_for_coordinate,
                                                    terrain_chunk_of(x), terrain_chunk_of(y));
                        Terrain t = -1;
                        return world->_terrain_for_coordinate.try_get(Coordinate{x, y}, t)
                            && (t == TERRAIN_WATER);
                    };
                    constexpr i32 L = Ship::SHIP_LENGTH;
                    bool found = false;
                    i32 bx = 0, by = 0;
                    i64 best = 0;
                    for (i32 y = 0; y <= 120; ++y) {
                        for (i32 x0 = -120; x0 <= 120 - 2 * L; ++x0) {
                            bool fits = true;
                            for (i32 x = x0 - L; fits && (x < x0 + 2 * L); ++x)
                                fits = water(x, y);
                            for (i32 x = x0 - 1; fits && (x <= x0 + L); ++x)
                                fits = water(x, y + 1);
                            bool landfall = false;
                            for (i32 x = x0; fits && (x < x0 + L); ++x)
                                landfall = landfall || !water(x, y - 1);
                            if (!fits || !landfall)
                                continue;
                            i64 dx = 2 * (i64)x0 + L - 1, dy = 2 * (i64)y;
                            i64 d = dx * dx + dy * dy;
                            if (!found || (d < best)) {
                                found = true;
                                best = d;
                                bx = x0;
                                by = y;
                            }
                        }
                    }
                    if (found) {
                        for (i32 x = bx; x < bx + L; ++x)
                            world->_terrain_for_coordinate.set(Coordinate{x, by - 1}, TERRAIN_ROCK);
                        Ship* ship = new Ship;
                        ship->_entity_id = world->generate_entity_id();
                        ship->_location = Coordinate{bx, by};   // the prow, west end
                        ship->_heading = HEADING_WEST;
                        ship->seal();
                        world->_entity_for_entity_id.set(ship->_entity_id, ship);
                        world->_waiting_on_time.set({Time{0}, ship->_entity_id});
                        for (int k = 0; k != Ship::SHIP_LENGTH; ++k) {
                            Coordinate c = ship->cell(k);
                            world->_entity_id_for_coordinate.set(c, ship->_entity_id);
                            EntityIDSet located;
                            (void) world->_located_for_coordinate.try_get(c, located);
                            located.set(ship->_entity_id);
                            world->_located_for_coordinate.set(c, located);
                        }

                        // A shuttle off the pier, into the second hold:
                        // southbound, MINE digs what lies under the SKIP;
                        // northbound, that SKIP steps over the MINE, so a
                        // round trip digs once.  DUMP on the pier turns
                        // the machine round and puts its load in the hold
                        // behind it; TURN_BACK is the landward end.  Laid
                        // only if the four cells inland are land.
                        i32 sx = ship->cell(2).x;
                        bool inland = true;
                        for (i32 y = by - 5; y <= by - 2; ++y)
                            inland = inland && !water(sx, y);
                        if (inland) {
                            put(sx, by - 1, term_make_opcode(OPCODE_DUMP));
                            put(sx, by - 2, term_make_opcode(OPCODE_MINE));
                            put(sx, by - 3, term_make_opcode(OPCODE_SKIP));
                            put(sx, by - 5, term_make_opcode(OPCODE_TURN_BACK));
                            machine_at_rest(sx, by - 4, HEADING_NORTH);
                        }
                    }
                }

                // A loader: takes three piles from the ground (LOAD
                // moves matter) and picks up two literals on the way,
                // then halts showing a mixed stack
                put(-8, -5, term_make_opcode(OPCODE_LOAD));
                put(-8, -4, term_make_matter(MATTER_IRON_ORE));
                put(-8, -3, term_make_integer_with(3));
                put(-8, -2, term_make_opcode(OPCODE_LOAD));
                put(-8, -1, term_make_matter(MATTER_LIMESTONE));
                put(-8,  0, term_make_opcode(OPCODE_LOAD));
                put(-8,  1, term_make_matter(MATTER_COAL));
                put(-8,  2, term_make_integer_with(5));
                put(-8,  3, term_make_opcode(OPCODE_HALT));
                machine_at_rest(-8, -6, HEADING_NORTH);
            }

            /*
            // matter: shipping containers, on the ground where the demo
            // machines will drive over (through) them
            world->_term_for_coordinate.set(Coordinate{-2, +2}, term_make_matter(MATTER_SHIPPING_CONTAINER));
            world->_term_for_coordinate.set(Coordinate{-3, +4}, term_make_matter(MATTER_SHIPPING_CONTAINER));
             */

            world->hack_repair_invariant();
            return world;
        }

        // Stress-test scenario, unreferenced while new_game uses
        // make_starting_world; wire it in temporarily for big-world runs.
        // Machines with random headings, randomly part-way through a move
        // between adjacent tiles, scattered over a square region centered
        // on the origin whose tiles each have a 10% chance of holding the
        // flip-flop opcode.  Fixed seed, and modulo rather than
        // uniform_int_distribution (whose mapping is
        // implementation-defined), so every run and every peer builds the
        // identical world.
        [[maybe_unused]] World* make_starting_world_big(i32 region_extent = 3000,
                                                        int machine_count = 100000) {
            World* world = new World;

            {
                Player* p = new Player;
                p->_entity_id = world->generate_entity_id();
                p->seal();
                world->_entity_for_entity_id.set(p->_entity_id, p);
                world->_waiting_on_time.set({Time{0}, p->_entity_id});
            }

            std::mt19937_64 gen{20260709};

            // The defaults: [-1500, 1500) each axis, 100000 machines.
            const i32 half = region_extent / 2;

            for (i32 y = -half; y != half; ++y) {
                for (i32 x = -half; x != half; ++x) {
                    if (gen() % 10 == 0)
                        world->_term_for_coordinate.set(Coordinate{x, y},
                                                        term_make_opcode(OPCODE_FLIP_FLOP));
                }
            }

            // Each machine starts mid-travel, mirroring the state a departing
            // Machine::notify leaves behind (phase TRAVELLING, straight move,
            // _on_arrival NOOP, wake scheduled at _new_time): arrival times
            // are uniform over (0, travel_ticks], so wakeups run at a steady
            // ~machine_count/travel_ticks per tick instead of a lockstep herd
            // at t=0 -- and stay spread, since every subsequent hop is another
            // travel_ticks.  A travelling machine occupies BOTH endpoint
            // cells (Machine::notify asserts it is the registered occupant of
            // each), so claim the pair, and reject-and-redraw a draw whose
            // pair collides (100000 pairs over 9000000 tiles, so few
            // retries).
            constexpr i64 travel_ticks = 64;   // one hop, per Machine::notify
            int placed = 0;
            while (placed != machine_count) {
                Coordinate xy{(i32)(gen() % region_extent) - half,
                              (i32)(gen() % region_extent) - half};
                i64 heading = (i64)(gen() % 4);
                Time arrival = Time{1 + (i64)(gen() % travel_ticks)};
                Coordinate destination = xy;
                switch (heading & 3) {
                    case 0: ++destination.y; break;
                    case 1: ++destination.x; break;
                    case 2: --destination.y; break;
                    case 3: --destination.x; break;
                }
                EntityID occupant = {};
                if (world->_entity_id_for_coordinate.try_get(xy, occupant) ||
                    world->_entity_id_for_coordinate.try_get(destination, occupant))
                    continue;
                Machine* machine = new Machine;
                machine->_entity_id = world->generate_entity_id();
                machine->_free_entity_id.data = 0;
                machine->_phase = Machine::PHASE_TRAVELLING;
                machine->_old_heading = heading;
                machine->_new_heading = heading;
                machine->_old_location = xy;
                machine->_new_location = destination;
                machine->_old_time = arrival - travel_ticks;
                machine->_new_time = arrival;
                machine->seal();
                world->_entity_for_entity_id.set(machine->_entity_id, machine);
                world->_entity_id_for_coordinate.set(xy, machine->_entity_id);
                world->_entity_id_for_coordinate.set(destination, machine->_entity_id);
                // located at both transit endpoints, mirroring occupancy;
                // the reject-and-redraw above guarantees no cell sharing,
                // so singleton sets suffice
                { EntityIDSet s; s.set(machine->_entity_id);
                  world->_located_for_coordinate.set(xy, s); }
                { EntityIDSet s; s.set(machine->_entity_id);
                  world->_located_for_coordinate.set(destination, s); }
                world->_waiting_on_time.set({arrival, machine->_entity_id});
                ++placed;
            }

            world->hack_repair_invariant();
            return world;
        }

    } // anonymous namespace

    // Swap in `world` as the only entry in _worlds, and ask the server for
    // the local client's Player: a JOIN rides the next step, spawning one
    // if the world does not already map this client (a new game), and
    // doing nothing if it does (a load of our own save).  Single-threaded
    // at the call sites (main thread, between pump and render), so the
    // brief drain is unobservable to the renderer.
    static void install_displayed_world(WorldState& m, World* world) {
        Root<World const*> discard;
        while (m._worlds.try_pop_front(discard)) { }
        m._worlds.emplace_back(world);
        m._world_to_render = world;
        m._steps_taken = 0;
        // The backlog is left alone: a joiner installing its donated save
        // has the steps after the boundary waiting in it, and a new or
        // loaded game started a new session, which cleared it
        if (m._server)
            m._server->request_join();
        // The session's log starts from this World (multiplayer.md
        // section 5); a joiner's placeholder, drawn while its save is in
        // flight, is not one the stream continues
        if (m._server && !m._awaiting_save)
            m._open_log(world);
    }

    // A new or loaded game is a new stream: a fresh relay whose steps
    // continue the installed world's time, that world as its genesis
    void WorldState::new_game() {
        World* world = make_starting_world();
        _start_hosting((uint64_t)world->_time);
        install_displayed_world(*this, world);
    }

    void WorldState::load_from_save(int id) {
        World* world = load_game(id);
        _start_hosting((uint64_t)world->_time);
        install_displayed_world(*this, world);
    }

    // ---- The session --------------------------------------------------

    RemoteServer* WorldState::_client() {
        if (_host)
            return &_host->client();
        return _remote.get();
    }

    void WorldState::_start_session(std::optional<int> save) {
        if (!save && !_gui.join_address.empty()) {
            _start_joining(_gui.join_address, _gui.join_port);
            // A placeholder to draw until the host's save arrives
            _awaiting_save = true;
            install_displayed_world(*this, make_starting_world());
            return;
        }
        if (save)
            load_from_save(*save);
        else
            new_game();
        if (_gui.host_port) {
            auto bound = host_listen(*_gui.host_port);
            String message;
            if (bound) {
                message = String("Hosting on port ");
                message.append(StringView(std::to_string(*bound).c_str()));
            } else {
                message = String("Hosting failed: ");
                message.append(bound.error());
            }
            _gui.append_log(message);
        }
    }

    void WorldState::_start_hosting(uint64_t head) {
        _end_session(std::chrono::milliseconds(500));
        assert(_gui.scope);
        std::string error;
        _host = Host::make(*_gui.scope, Relay::Limits{}, error, head);
        if (!_host) {
            String message("Hosting failed: ");
            message.append(StringView(error.c_str()));
            _gui.append_log(message);
            return;
        }
        _server = &_host->client();
        _logged_state = RemoteServer::State::CONNECTING;
    }

    void WorldState::_start_joining(String const& address, uint16_t port) {
        _end_session(std::chrono::milliseconds(500));
        assert(_gui.scope);
        _remote = RemoteServer::make(*_gui.scope);
        _remote->dial(std::string(address.data(), address.chars.size()), port);
        _server = _remote.get();
        _logged_state = RemoteServer::State::CONNECTING;
        String message("Joining ");
        message.append(address);
        message.append(StringView(":"));
        message.append(StringView(std::to_string(port).c_str()));
        _gui.append_log(message);
    }

    // Blocking: the main thread, between frames.  Nothing to wait for when
    // the loops have already ended (the process scope drained)
    void WorldState::_end_session(std::chrono::milliseconds grace) {
        _server = nullptr;
        if (_host) {
            if (!_host->finished())
                (void) execution::sync_wait(_host->shutdown(grace));
            _host.reset();
        }
        if (_remote) {
            if (!_remote->finished())
                (void) execution::sync_wait(_remote->shutdown(grace));
            _remote.reset();
        }
        _backlog.clear();
        _log.reset();
        _replay.reset();
        // A join that never got its save, a donation never served: the
        // next session starts clean, or it would never apply a step
        _awaiting_save = false;
        _donations.clear();
    }

    coroutine::Task WorldState::end_session(std::chrono::milliseconds grace) {
        _server = nullptr;
        if (_host) {
            co_await _host->shutdown(grace);
            _host.reset();
        }
        if (_remote) {
            co_await _remote->shutdown(grace);
            _remote.reset();
        }
        _backlog.clear();
        _log.reset();
        _replay.reset();
        _awaiting_save = false;
        _donations.clear();
    }

    WorldState::~WorldState() {
        _end_session(std::chrono::milliseconds(500));
        if (_gui.world == this)
            _gui.world = nullptr;
        fprintf(stderr, "%s\n", __PRETTY_FUNCTION__);
    }

    std::expected<uint16_t, String> WorldState::host_listen(uint16_t port) {
        // A replayed World, sessionless, becomes a game of our own first
        if (!_host && !_remote)
            _host_displayed_world();
        if (!_host)
            return std::unexpected(String("not the host"));
        auto bound = _host->relay().listen("0.0.0.0", port);
        if (!bound)
            return std::unexpected(String(StringView(bound.error().c_str())));
        return *bound;
    }

    void WorldState::join(String const& address, uint16_t port) {
        _start_joining(address, port);
        // The host's client donates its World as of the head we arrive at,
        // and the stream from there follows; until the save has landed the
        // steps wait and whatever is displayed stays as a placeholder
        _awaiting_save = true;
        _backlog.clear();
        _steps_taken = 0;
        _server->request_join();
    }

    void WorldState::leave() {
        if (!_remote)
            return;
        _remote->leave();
        // Our world is ours now: a fork of the game, hosted by us from
        // here.  Its client map still names the old game's clients (open
        // item 11.11)
        _host_displayed_world();
    }

    void WorldState::_host_displayed_world() {
        Root<World const*> current;
        (void) _worlds.try_pop_front(current);
        assert(current);
        _start_hosting((uint64_t)current->_time);
        _worlds.emplace_back(current);
        _backlog.clear();
        if (_server) {
            _server->request_join();
            _open_log(current._ptr);
        }
    }

    // ---- The session log and its replay (multiplayer.md section 5) ----

    namespace {

        std::string narrow(String const& s) {
            return std::string(s.data(), s.chars.size());
        }

        String widen(std::string const& s) {
            return String(StringView(s.c_str()));
        }

    } // namespace

    void WorldState::_open_log(World const* world) {
        if (_log_directory.empty())
            _log_directory = _gui.session_log_directory;   // empty: no log
        _log = std::make_unique<SessionLog>(_log_directory, (uint64_t)world->_time,
                                            world->content_hash(), serialize_world(world));
    }

    std::expected<void, String> WorldState::replay(String const& path_text, std::optional<uint64_t> through) {
        const std::filesystem::path path(narrow(path_text));
        std::expected<SessionLogContents, std::string> contents = read_session_log(path);
        if (!contents)
            return std::unexpected(widen(contents.error()));
        const uint64_t head = contents->head;
        const uint64_t last = contents->last();
        const uint64_t target = through.value_or(last);
        if ((target < head) || (target > last)) {
            std::string why = contents->entries.empty()
                ? "the log holds no steps: its checkpoint is at time " + std::to_string(head)
                : "the log holds steps " + std::to_string(head + 1) + " to " + std::to_string(last);
            return std::unexpected(widen(why));
        }
        World* world = load_world_from_bytes(contents->checkpoint);
        if (!world)
            return std::unexpected(String("the log's checkpoint is not a save this build reads"));
        // Sessionless from here: the stream is the file's.  (Ending the
        // session drops any earlier replay and clears the backlog)
        _end_session(std::chrono::milliseconds(500));
        install_displayed_world(*this, world);
        Replay r;
        r.path = path;
        r.first = head + 1;
        r.through = target;
        r.same_build = contents->same_build();
        for (SessionLogContents::Entry const& e : contents->entries)
            r.hashes.push_back(e.hash);
        const uint64_t loaded = world->content_hash();
        if (loaded != contents->head_hash) {
            std::string text = "the checkpoint at time " + std::to_string(head) + " hashes to "
                             + hash_literal(loaded) + "; the log recorded " + hash_literal(contents->head_hash);
            if (!r.same_build)
                text += " (another build wrote the log)";
            r.result = widen(text);
        } else if (target == head) {
            r.result = widen("replayed no steps: the checkpoint at time " + std::to_string(head)
                             + ", " + hash_literal(loaded));
        } else {
            for (size_t i = 0; i != (size_t)(target - head); ++i)
                _backlog.push_back(std::move(contents->entries[i].step));
        }
        _replay = std::move(r);
        return {};
    }

    // After a step is applied: against the record.  The first
    // disagreement ends the replay with the divergent World displayed
    void WorldState::_replay_check(Step const& step) {
        if (!_replay || _replay->result)
            return;
        assert(step.number >= _replay->first);
        const uint64_t recorded = _replay->hashes[(size_t)(step.number - _replay->first)];
        const uint64_t computed = _world_to_render._ptr->content_hash();
        if (recorded != computed) {
            std::string text = "diverged at step " + std::to_string(step.number) + ": this build "
                             + hash_literal(computed) + ", the log recorded " + hash_literal(recorded);
            if (!_replay->same_build)
                text += " (another build wrote the log)";
            _replay->result = widen(text);
            _backlog.clear();
        } else if (step.number == _replay->through) {
            _replay->result = widen("replayed steps " + std::to_string(_replay->first) + " to "
                                    + std::to_string(_replay->through) + ": consistent");
        }
    }

    // A copy of the displayed World with one cell changed, in place of it:
    // nothing rides the stream, so from the next step this client's hash
    // disagrees with everyone's
    void WorldState::corrupt(Coordinate at, Term term) {
        Root<World const*> old;
        if (!_worlds.try_pop_front(old))
            return;
        World const* w = old._ptr;
        WaitableMap<Coordinate, Term> terms = w->_term_for_coordinate;
        if (term_is_null(term)) {
            Term dropped{};
            (void) terms.kv.try_erase(at, dropped);
        } else {
            terms.set(at, term);
        }
        World* next = new World(w->_time, w->_entity_id_source, w->_ready,
                                w->_entity_id_for_coordinate, w->_located_for_coordinate,
                                w->_entity_for_entity_id, terms, w->_terrain_for_coordinate,
                                w->_waiting_on_time, w->_player_for_client,
                                w->_money, w->_seed);
        next->seal();
        _world_to_render = next;
        _worlds.emplace_back(next);
        _gui.append_log("Corrupted the displayed world: a desync, if the game goes on");
    }

    bool WorldState::has_server_input() {
        return (!_awaiting_save && !_backlog.empty()) || (_server && _server->has_input());
    }

    coroutine::MultiProducerSingleConsumerQueue<Transport::Event>* WorldState::server_inbox() {
        RemoteServer* client = _client();
        return client ? &client->inbox() : nullptr;
    }

    // The host's client as donor: a request the relay queued after STEP
    // boundary names the World that step produced, which is the displayed
    // one once it has been applied; the walk runs on the pool over a rooted
    // snapshot (io/save.cpp's serialize_world)
    void WorldState::_serve_donations() {
        RemoteServer* client = _client();
        if (!client || !_world_to_render || _awaiting_save)
            return;
        const uint64_t time = (uint64_t)_world_to_render._ptr->_time;
        std::erase_if(_donations, [&](RemoteServer::Donation const& d) {
            if (d.boundary > time)
                return false;               // not there yet
            if (d.boundary == time) {
                client->donate(d, Root<World const*>{_world_to_render._ptr}, *_gui.scope);
            } else {
                String message("Cannot donate the world at time ");
                message.append(StringView(std::to_string(d.boundary).c_str()));
                message.append(StringView(": already past it"));
                _gui.append_log(message);
            }
            return true;
        });
    }

    coroutine::Task WorldState::_apply_step_async(Step const& step) {
        // A pin in this frame holds the epoch floor for step's
        // epoch-allocated transaction structures across the await
        epoch::Epoch pin = pin_global_epoch();
        Root<World const*> old_world;
        (void) _worlds.try_pop_front(old_world);
        assert(old_world);
        assert(old_world->_time + 1 == (Time)step.number);
        Root<World*> next = co_await old_world->step(step.commands);
        unpin_global_epoch(pin);
        assert(next);
        // The tripwire (multiplayer.md section 5): what we computed, for
        // the relay to compare with the reference client's
        const uint64_t hash = next._ptr->content_hash();
        if (_server)
            _server->report_hash(step.number, hash);
        // The log: the step as finalized and what it produced here
        if (_log) {
            RemoteServer* client = _client();
            (void) _log->append(step, hash,
                                client ? client->client_id() : SERVER_CLIENT_ID,
                                client ? client->quantum_ns() : 0);
        }
        _world_to_render = next._ptr;
        _worlds.emplace_back(std::move(next));
        ++_steps_taken;
    }

    void WorldState::_apply_step(Step const& step) {
        (void) execution::sync_wait(_apply_step_async(step));
    }

    bool WorldState::save_current(move_only_function<void(int)> on_done) {
        // Hand a rooted snapshot of the displayed world to the async saver and
        // return to the frame immediately; the save no longer blocks the main
        // thread.  The world is an immutable persistent structure that
        // World::step() never mutates (it builds a fresh World), so the
        // background walk reads a stable snapshot even as play continues, and
        // the Root keeps it (and everything reachable) alive until the save
        // finishes.
        Root<World const*> w;
        if (!_worlds.try_pop_front(w))
            return false;
        // The callback runs on a worker thread when the save finishes; post the
        // result for the main-thread pump to surface in the log.  It holds
        // the GuiContext, which outlives every WorldState and the scope's
        // drain, not this WorldState, which a return to the main menu may
        // have destroyed by then
        assert(_gui.scope);
        GuiContext* gui = &_gui;
        _gui.scope->soon(save_game(w, [gui, on_done = std::move(on_done)](int id) mutable {
            gui->post_notification(id ? "Saved." : "Save failed.");
            if (on_done)
                on_done(id);
        }));
        _worlds.push_front(std::move(w));
        return true;
    }


    void WorldState::_regenerate_uniforms() {
        
        // camera setup
        
        // The camera sits at _camera_offset from the focus and looks at
        // it.  In render space the focus is at the sub-tile fraction, so
        // the view first brings that to the origin, then rotates the eye
        // onto the Z axis.
        //
        // The zoom dollies the camera along its own line of sight: the
        // offset (and with it the clip planes) scales by 1 / zoom, the
        // field of view never changes.  Map view keeps the default camera
        // and scales the map quad instead (map_model_transform).
        const float dolly = 1.0f / (show_map() ? ZOOM_DEFAULT : _zoom);
        float3 p = _camera_offset * dolly;
        float2 f = _focus_fraction;
        _uniforms.camera_position_world = make<float4>(p.x + f.x, p.y + f.y, p.z, 1.0f);
        quatf q(simd_normalize(p), simd_make_float3(0, 0, 1));
        float4x4 V = simd_matrix_translate(0, 0, -simd_length(p)) *
                              float4x4(q) *
                              simd_matrix_translate(-f.x, -f.y, 0.0f);
        
        simd_float4x4 F = simd_mul(matrix_perspective_right_hand(M_PI_2, 1,
                                                                 5.0f * dolly,
                                                                 50.0f * dolly),
                                   simd_matrix_scale(1, 1, -1, 1));
        
        float aspect_ratio = _gui.viewport_size.x / _gui.viewport_size.y;
        simd_float4x4 P = simd_mul(simd_matrix_scale(2, 2 * aspect_ratio, 1),
                                   F);
        
        _uniforms.view_transform = V;
        _uniforms.viewprojection_transform = simd_mul(P, V);
        
        _uniforms.inverse_view_transform = simd_inverse(_uniforms.view_transform);
        _uniforms.inverse_viewprojection_transform = simd_inverse(_uniforms.viewprojection_transform);

        // The camera ray basis (ShaderTypes.h): the ray through an NDC
        // point, divided by the linear depth (clip w) of its near-plane
        // point, so that camera + ray * depth lands at depth
        {
            const float4x4 VP = _uniforms.viewprojection_transform;
            const float4x4 IVP = _uniforms.inverse_viewprojection_transform;
            const float3 C = _uniforms.camera_position_world.xyz;
            auto ray_at = [&](float x, float y) {
                float4 n = simd_mul(IVP, make<float4>(x, y, 0.0f, 1.0f));
                float3 N = n.xyz / n.w;
                float w = simd_mul(VP, make<float4>(N.x, N.y, N.z, 1.0f)).w;
                float3 r = (N - C) / w;
                return make<float4>(r.x, r.y, r.z, 0.0f);
            };
            float4 centre = ray_at(0.0f, 0.0f);
            _uniforms.camera_ray_centre = centre;
            _uniforms.camera_ray_dx = ray_at(1.0f, 0.0f) - centre;
            _uniforms.camera_ray_dy = ray_at(0.0f, 1.0f) - centre;
        }

        // sun setup
        
        p = simd_normalize(simd_make_float3(-2, -1, 3));
        //p = simd_normalize(simd_make_float3(-1, 0, 1)); // Factorio lol
        _uniforms.light_direction = p;
        _uniforms.radiance = simd_make_float3(2.0f, 0.5f, 0.5f);

        
        // we have a lot of freedom in our choice of the shadow map projection;
        // it must map light_direction to clip_space Z, and it must not be
        // degenerate; everything else affects how shadow map pixels relate
        // to screen pixels
        
        // our typical view is of the ground from above, with most shadows cast
        // on the plane Z=0, and relatively weak perspective effects at work
        //
        // we can cast pixel-perfect shadows on this plane, and good quality
        // shadows near it, by choosing the shadow mapping which maps the
        // x,y,0,1 plane to pixels u/w,v/w
        
        // first, we shear the world to move the light source to 0,0,-1,0
        // - the ground plane is unaltered
        // - all shadows are now cast down the z axis
        // - the output x,y,w is the projection of a point on to the ground
        // - the output z is the distance above the plane of the point
        
        simd_float4x4 A = simd_matrix(make<float4>(1.0f, 0.0f, 0.0, 0.0f),
                                      make<float4>(0.0f, 1.0f, 0.0, 0.0f),
                                      make<float4>(-p.x / p.z,
                                                       -p.y / p.z,
                                                       -1.0f, 0.0f),
                                      make<float4>(0.0f, 0.0f, 0.0f, 1.0f));
        // :todo: for a light source not at infinity, we'll have to also
        // put in w = 1.0f - z / light_position.z or something; for a light
        // source inside / near the camera frustum, we need to go to a cube
        // map and investigate skewing the cube map in some alternative fashion
        
        // second, we apply the the camera's viewrojection_transform to x,y,w
        // but pass through z unchanged.
        // note that the clip space z will still be divided through by w, but
        // that will not affect its relative ordering
        simd_float4x4 B = simd_matrix(_uniforms.viewprojection_transform.columns[0],
                                      _uniforms.viewprojection_transform.columns[1],
                                      make<float4>(0.0f, 0.0f, 1.0f, 0.0f),
                                      _uniforms.viewprojection_transform.columns[3]);
        // :todo: ensure that result fits in the clip space z range though
        
        // finally, we rescale the projection to fit in the larger shadow map
        // texture: screen NDC [-1, 1] maps to the central viewport-sized
        // texel rect of the map, preserving the 1:1 screen-pixel-to-texel
        // correspondence, and the remainder is margin for out-of-plane
        // geometry (see WryWorldScene -resizeShadowMapForDrawableSize:,
        // which sizes the texture and mirrors it into _shadow_map_size
        // before rerunning us)

        // when the texture and viewport parities differ, the centering
        // offset (map - viewport) / 2 is a half-integer number of texels,
        // which would break the exact texel-to-pixel correspondence; snap
        // it to a whole texel with a sub-texel translation

        float t0x = (_shadow_map_size.x - _gui.viewport_size.x) * 0.5f;
        float t0y = (_shadow_map_size.y - _gui.viewport_size.y) * 0.5f;

        simd_float4x4 C = simd_mul(
            simd_matrix_translate(simd_make_float3(2.0f * (roundf(t0x) - t0x) / _shadow_map_size.x,
                                                   2.0f * (roundf(t0y) - t0y) / _shadow_map_size.y,
                                                   0.0f)),
            simd_matrix_scale(_gui.viewport_size.x / _shadow_map_size.x,
                              _gui.viewport_size.y / _shadow_map_size.y,
                              1.0f));

        _uniforms.light_viewprojection_transform = simd_mul(C , simd_mul(B, A));
        _uniforms.light_viewprojectiontexture_transform = simd_mul(matrix_ndc_to_tc_float4x4,
                                                                   _uniforms.light_viewprojection_transform);

        // Though all shadow lookups on ground plane will be inside
        // the camera viewport-sized middle of the texture, geometry above the
        // ground plane can sample outside this region.  If we can comprehend
        // the camera frustum's projection onto the shadow texture, we may
        // be able to define a shadow viewport and reduce rendering.  For
        // example, in our standard view, the camera view is an irregular
        // pyramid, and the shadow map is an irregular column; both have the
        // same base, and we only need to extend the shadow map along the sides
        // of the column that the pyramid leans out of; when the light source
        // is approxmiately behind the camera, we don't need to extend the
        // shadow map at all
        
        
        
    }

    // ---- Per-frame logic (moved out of WryWorldScene, which is now a thin
    //      Metal renderer over this state) -----------------------------------

    void WorldState::update(double dt) {
        (void) dt;   // the world advances one step per frame, not by wall-clock

        // Service the garbage collector once per frame.
        mutator_repin();

        // The connection's news, once
        if (RemoteServer* client = _client()) {
            RemoteServer::State state = client->state();
            if (state != _logged_state) {
                _logged_state = state;
                String message;
                switch (state) {
                    case RemoteServer::State::CONNECTING: break;
                    case RemoteServer::State::HANDSHAKE: break;
                    case RemoteServer::State::SUBSCRIBED:
                        message = String("Connected as client ");
                        message.append(StringView(std::to_string(client->client_id()).c_str()));
                        break;
                    case RemoteServer::State::JOINED:
                        message = String("Joined the game");
                        break;
                    case RemoteServer::State::DISCONNECTED:
                        message = String("Disconnected: ");
                        message.append(StringView(client->reason().c_str()));
                        break;
                }
                if (!message.chars.empty())
                    _gui.append_log(message);
            }
        }

        // The finalized steps that have arrived are the step's input: the
        // server events and every client's actions, in the server's order.
        // A frame applies its share and keeps the rest; when current, the
        // share is everything.  (Single player: our own actions,
        // round-tripped through the relay in this process.)  A joiner
        // holds them until the donated save has given it the World they
        // continue; a host's client serves the donation requests the
        // relay sent it
        if (_server) {
            for (Step& step : _server->take_steps())
                _backlog.push_back(std::move(step));
            if (RemoteServer* client = _client()) {
                for (RemoteServer::Donation const& d : client->take_donations())
                    _donations.push_back(d);
                if (_awaiting_save) {
                    if (std::optional<RemoteServer::DonatedSave> save = client->take_save()) {
                        World* world = load_world_from_bytes(save->bytes);
                        if (!world) {
                            client->disconnect("the donated save is unreadable");
                        } else {
                            _awaiting_save = false;
                            install_displayed_world(*this, world);
                            // Proof of receipt: our hash of what we loaded
                            client->report_hash(save->boundary, world->content_hash());
                            String message("Loaded the game at time ");
                            message.append(StringView(std::to_string(save->boundary).c_str()));
                            _gui.append_log(message);
                        }
                    }
                }
            }
            _serve_donations();
        }
        // The backlog is the step's input whether it came from the session
        // or from a log under replay (sessionless); a frame applies its
        // share
        for (size_t applied = 0; !_awaiting_save && !_backlog.empty() && (applied != max_steps_per_frame); ++applied) {
            Step step = std::move(_backlog.front());
            _backlog.pop_front();
            // Steps are numbered by the World time they produce; a
            // stream that does not continue our World (a join into a
            // game that started from a save) is no use to us
            const uint64_t expected = (uint64_t)_world_to_render._ptr->_time + 1;
            if (step.number != expected) {
                _backlog.clear();
                std::string why = "the stream does not continue this world: step "
                                  + std::to_string(step.number) + " for time "
                                  + std::to_string(expected - 1);
                if (RemoteServer* client = _client())
                    client->disconnect(why);
                if (_replay && !_replay->result)
                    _replay->result = widen(why);
                break;
            }
            _apply_step(step);
            _replay_check(step);
            _serve_donations();
        }

        // ~1 Hz: hand a rooted snapshot of the freshly stepped world to the
        // background map builder.  The load_acquire pairs with the builder's
        // completion store_release, so the previous build's publication is
        // visible before we permit the next one; the map may lag the world
        // by a second or two, by design.
        {
            auto now = std::chrono::steady_clock::now();
            if (!_map_handoff->in_flight.load_acquire()
                && (now - _map_last_start >= std::chrono::seconds(1))) {
                _map_last_start = now;
                _map_handoff->in_flight.store_relaxed(true);
                assert(_gui.scope);
                _gui.scope->soon(world_map_build(Root<const World*>{_world_to_render._ptr},
                                                 _map_handoff));
            }
        }

        // Project the cursor for the renderer.  (Input became commands
        // as it was pumped; see pump_legacy_event.)
        project_cursor();
    }

    // Coordinates are i32 and wrap: the arithmetic is modular, which
    // C++20 defines for the conversion back to signed
    static i32 wrap_add(i32 a, i32 b) {
        return (i32)((u32)a + (u32)b);
    }
    static i32 wrap_sub(i32 a, i32 b) {
        return (i32)((u32)a - (u32)b);
    }

    float2 WorldState::render_position(Coordinate xy) const {
        return make<float2>((float)wrap_sub(xy.x, _focus_tile.x),
                            (float)wrap_sub(xy.y, _focus_tile.y));
    }

    std::optional<Coordinate> WorldState::world_tile(float2 render) const {
        const float x = std::round(render.x);
        const float y = std::round(render.y);
        // The comparisons are false for NaN.  2^31 is exactly
        // representable; anything at or beyond it would overflow i32.
        constexpr float limit = 2147483648.0f;
        if (!(x >= -limit && x < limit && y >= -limit && y < limit))
            return std::nullopt;
        return Coordinate{wrap_add(_focus_tile.x, (i32)x),
                          wrap_add(_focus_tile.y, (i32)y)};
    }

    // The machine occupying a tile of the displayed World, or none
    EntityID WorldState::machine_at(Coordinate xy) {
        Root<World const*> w;
        if (!_worlds.try_pop_front(w))
            return {};
        EntityID id = {};
        const Entity* e = nullptr;
        const bool machine
            = w._ptr->_entity_id_for_coordinate.try_get(xy, id) && id
              && w._ptr->_entity_for_entity_id.try_get(id, e)
              && dynamic_cast<const Machine*>(e);
        _worlds.push_front(std::move(w));
        return machine ? id : EntityID{};
    }

    std::string WorldState::hand_hint() const {
        if (!is_holding())
            return {};
        auto keys = [this](gui::Action a) {
            std::string out;
            for (gui::KeyCombo c : _gui.settings.keymap.combos_from_action(a)) {
                String s = gui::string_from_combo(c);
                out += out.empty() ? "[" : " / [";
                out.append(s.data(), s.chars.size());
                out += "]";
            }
            return out;
        };
        std::string hint;
        auto add = [&hint](std::string const& k, const char* what) {
            if (k.empty())
                return;   // unbound
            if (!hint.empty())
                hint += "    ";
            hint += k + " " + what;
        };
        const Term t = _holding_value._value;
        if (t.is_opcode()) {
            const OPCODE o = (OPCODE)t.as_opcode();
            const bool turns = opcode_rotated_clockwise(o) != o;
            const bool two_cycle = turns && (opcode_rotated_clockwise(opcode_rotated_clockwise(o)) == o);
            if (turns) {
                add(keys(gui::Action::rotate), "rotate");
                if (!two_cycle)   // a 2-cycle turns back the same either way
                    add(keys(gui::Action::reverse_rotate), "rotate back");
            }
            const bool h = opcode_mirrored_horizontal(o) != o;
            const bool v = opcode_mirrored_vertical(o) != o;
            if (h && v && (opcode_mirrored_horizontal(o) == opcode_mirrored_vertical(o)))
                add(keys(gui::Action::flip_horizontal), "mirror");   // either mirror swaps it
            else {
                if (h) add(keys(gui::Action::flip_horizontal), "mirror east-west");
                if (v) add(keys(gui::Action::flip_vertical), "mirror north-south");
            }
        }
        add("[click]", "place");
        add("[Esc]", "drop");
        return hint;
    }

    // Centre on where the renderer last drew the followed machine (render
    // space is relative to the focus tile, so the step is that position
    // less the focus fraction): a frame behind the drawing, and smooth.
    // A machine not drawn last frame (off screen: the follow began from
    // afar, or the renderer is absent) is found in the displayed World
    // and centred on its tile; one no longer there ends the follow.
    void WorldState::follow_camera() {
        if (!_follow)
            return;
        if (_follow_drawn_at) {
            float2 at = *_follow_drawn_at;
            _follow_drawn_at.reset();
            pan_focus(at - _focus_fraction);
            return;
        }
        const Entity* e = nullptr;
        const Machine* m = nullptr;
        Root<World const*> w;
        if (_worlds.try_pop_front(w)) {
            if (w._ptr->_entity_for_entity_id.try_get(_follow, e))
                m = dynamic_cast<const Machine*>(e);
            Coordinate at = m ? m->_new_location : Coordinate{};
            _worlds.push_front(std::move(w));
            if (m) {
                pan_focus(render_position(at) - _focus_fraction);
                return;
            }
        }
        _follow = {};
    }

    void WorldState::pan_focus(float2 delta) {
        if (!(std::isfinite(delta.x) && std::isfinite(delta.y)))
            return;
        // Whole tiles carry into the tile; a carry beyond the i32 range
        // wraps with it (the focus is then anywhere, but defined)
        constexpr float limit = 2147483648.0f;
        auto carry = [](float fraction, i32& tile) {
            float whole = std::clamp(std::round(fraction), -limit, limit - 1.0f);
            tile = wrap_add(tile, (i32)whole);
            return fraction - whole;
        };
        _focus_fraction += delta;
        _focus_fraction.x = carry(_focus_fraction.x, _focus_tile.x);
        _focus_fraction.y = carry(_focus_fraction.y, _focus_tile.y);
        _regenerate_uniforms();
    }

    simd_float4x4 WorldState::map_model_transform(float zoom) const {
        // Scale render space about the focus, so the camera's view
        // direction and lighting stay the world's own
        float2 f = _focus_fraction;
        return simd_mul(simd_matrix_translate(f.x, f.y, 0.0f),
                        simd_mul(simd_matrix_scale(zoom),
                                 simd_matrix_translate(-f.x, -f.y, 0.0f)));
    }

    void WorldState::set_zoom(float zoom) {
        if (!std::isfinite(zoom))
            return;
        _zoom = std::clamp(zoom, ZOOM_MIN, ZOOM_MAX);
        _regenerate_uniforms();
    }

    // The cursor's ray meets the ground plane here, in render space
    simd_float4 WorldState::cursor_on_ground() const {
        using namespace ::simd;
        simd_float4 b = make<float4>(_mouse, 0.0f, 1.0f);
        return make<float4>(project_screen_ray(_uniforms.viewprojection_transform, b),
                            0.0f, 1.0f);
    }

    void WorldState::project_cursor() {
        _mouse4 = cursor_on_ground();
    }

    std::optional<Coordinate> WorldState::cursor_tile() const {
        return world_tile(cursor_on_ground().xy);
    }

    // Empty the hand: the palette forgets its selection and the renderer's
    // cursor refresh puts the platform arrow back.
    // The spelling of an opcode for the log, without its "OPCODE_" prefix.
    // A code this build does not name (a save from elsewhere) is not a
    // null pointer to print from
    static const char* opcode_label(OPCODE code) {
        const char* name = name_from_OPCODE(code);
        return name ? name + 7 : "an opcode";
    }

    void WorldState::drop_hand() {
        if (!is_holding())
            return;
        char buffer[100];
        const Term held = _holding_value._value;
        std::snprintf(buffer, sizeof(buffer), "Dropped %s",
                      held.is_opcode() ? opcode_label((OPCODE) held.as_opcode()) : "value");
        _holding_value = term_make_null();
        _palette_overlay.clear_selection();
        _palette_overlay.request_cursor_refresh();
        _gui.append_log(buffer);
    }

    // Put a Term in the hand; the palette follows (its selection marks the
    // held opcode's orbit) and the cursor glyph refreshes.  Null empties it.
    void WorldState::hold(Term t) {
        if (term_is_null(t)) {
            drop_hand();
            return;
        }
        _holding_value = t;
        if (t.is_opcode())
            _palette_overlay.select_orbit_of(t);
        else
            _palette_overlay.clear_selection();
        _palette_overlay.request_cursor_refresh();
    }

    // Pipette (Q with an empty hand): copy whatever is on the tile into
    // the hand.  Matter is excepted -- placing the copy would conjure
    // matter; ghosts are the eventual answer.  Reads the displayed world:
    // the hand is client state, so no command is involved.
    bool WorldState::pipette_at(Coordinate xy) {
        Root<World const*> w;
        if (!_worlds.try_pop_front(w))
            return false;
        Term t{};
        const bool has = w._ptr->_term_for_coordinate.try_get(xy, t);
        _worlds.push_front(std::move(w));
        if (!has || term_is_null(t) || t.is_matter())
            return false;
        hold(t);
        char buffer[100];
        if (t.is_opcode())
            std::snprintf(buffer, sizeof(buffer), "Picked up %s", opcode_label((OPCODE) t.as_opcode()));
        else if (t.is_inty())
            std::snprintf(buffer, sizeof(buffer), "Picked up %lld",
                          (long long) t.as_int());
        else
            std::snprintf(buffer, sizeof(buffer), "Picked up a value");
        _gui.append_log(buffer);
        return true;
    }

    // ----------------------------------------------------------------
    // Commands (core/docs/commands.md).  The keymap's actions are commands
    // named by their stable ids; gestures in pump_legacy_event issue the
    // world edits.

    namespace {

        using command::Category;
        using command::Param;
        using command::Reply;
        using command::make_command;
        using Ctx = CommandContext;

        // Wrap a handler taking WorldState& so it refuses when no world
        // scene is showing.
        template<typename F>
        auto with_world(F f) {
            return [f](Ctx& c, auto const&... args) -> Reply {
                if (!c.world)
                    return Reply::error(String("no world is loaded"));
                return f(*c.world, args...);
            };
        }

        // The tile under the cursor: the default for gesture-shaped
        // commands, printed resolved in history.
        Param<Ctx, Coordinate> cursor_param(const char* name, const char* summary) {
            return Param<Ctx, Coordinate>{
                name, summary,
                [](Ctx& c) -> std::expected<Coordinate, String> {
                    if (!c.world)
                        return std::unexpected(String("no world is loaded"));
                    if (auto tile = c.world->cursor_tile())
                        return *tile;
                    return std::unexpected(String("the cursor is not over the ground"));
                },
                "the tile under the cursor"};
        }

        // Reorient the held opcode, Factorio-style: rotate is clockwise,
        // reverse-rotate counterclockwise, the flips mirror east<->west /
        // north<->south.  The palette shows one glyph per orbit; these
        // reach the rest.
        Reply reorient_hand(WorldState& ws, OPCODE (*f)(OPCODE)) {
            if (!ws._holding_value._value.is_opcode())
                return Reply::ok();
            OPCODE held = (OPCODE)ws._holding_value._value.as_opcode();
            OPCODE reoriented = f(held);
            if (reoriented != held) {
                Term next = term_make_opcode((int)reoriented);
                // The palette follows the hand: its cell for this orbit
                // now shows the orientation you are holding.
                ws._palette_overlay.replace_glyph(ws._holding_value._value, next);
                ws._holding_value = next;
                ws._palette_overlay.request_cursor_refresh();
            }
            char buffer[100];
            std::snprintf(buffer, sizeof(buffer), "Holding %s", opcode_label(reoriented));
            ws._gui.append_log(buffer);
            return Reply::ok();
        }

        Reply toggle(WorldState& ws, bool& flag, const char* what) {
            flag = !flag;
            char buffer[100];
            std::snprintf(buffer, sizeof(buffer), "%s %s",
                          flag ? "Show" : "Hide", what);
            ws._gui.append_log(buffer);
            return Reply::ok();
        }

        Reply submit_write(WorldState& ws, Coordinate at, Term term) {
            if (!ws._server)
                return Reply::error(String("no session"));
            if (!ws._server->submit(make_write_action(at, term)))
                return Reply::error(String("the action cannot travel: no literal form, or over the wire's cap"));
            return Reply::ok();
        }

        // `step n`: done once the world has taken n more steps.  Holds the
        // WorldState by address only to notice that it has gone (the scene
        // changed); it is never dereferenced unless it is still the live
        // one.
        struct StepPending final : command::Pending {
            GuiContext& gui;
            WorldState* ws;
            uint64_t target;
            StepPending(GuiContext& g, WorldState* w, uint64_t t)
            : gui(g), ws(w), target(t) {}

            std::optional<Reply> poll() override {
                if (gui.world != ws)
                    return Reply::error(String("the world was unloaded"));
                if (ws->_steps_taken < target)
                    return std::nullopt;
                return Reply::ok();
            }

            // The relay has already finalized the steps; cancelling only
            // stops the wait for them
            void cancel() override {}
        };

        // `replay`: done once the log's steps have been applied, or the
        // first of them disagreed with the record
        struct ReplayPending final : command::Pending {
            GuiContext& gui;
            WorldState* ws;
            ReplayPending(GuiContext& g, WorldState* w) : gui(g), ws(w) {}

            std::optional<Reply> poll() override {
                if (gui.world != ws)
                    return Reply::error(String("the world was unloaded"));
                if (!ws->_replay)
                    return Reply::error(String("the replay was abandoned"));
                if (!ws->_replay->result)
                    return std::nullopt;
                return Reply::ok(*ws->_replay->result);
            }

            // The World stays where the replay got to
            void cancel() override {
                if ((gui.world == ws) && ws->_replay && !ws->_replay->result) {
                    ws->_backlog.clear();
                    ws->_replay.reset();
                }
            }
        };

        // `save`: done once the background save has published (or failed).
        // The result is shared with the save's completion callback, which
        // stores it on a worker thread; it holds no WorldState, so the save
        // may outlive the world it was taken from
        struct SaveResult {
            // -1 until the save finishes; then the id published, or 0.
            // The callback's store_release pairs with poll's load_acquire
            Atomic<int> id{-1};
        };

        struct SavePending final : command::Pending {
            std::shared_ptr<SaveResult> result;
            explicit SavePending(std::shared_ptr<SaveResult> r) : result(std::move(r)) {}

            std::optional<Reply> poll() override {
                const int id = result->id.load_acquire();
                if (id < 0)
                    return std::nullopt;
                if (id == 0)
                    return Reply::error(String("the save failed"));
                return Reply::ok(widen(save_name(id)));
            }

            // The save carries on; only the wait for it stops
            void cancel() override {}
        };

        // The id of the save `name` names, as list-saves prints it, if
        // there is one
        std::optional<int> save_named(String const& name) {
            const std::string wanted = narrow(name);
            for (auto const& [file, id] : enumerate_games())
                if (file == wanted)
                    return id;
            return std::nullopt;
        }

        // new-game and load at the main menu: a WorldState for the host to
        // show, as the main menu's buttons once built themselves.  Its
        // constructor starts the session from the starting scenario (or
        // joins, on --join) or the save, as the in-game path's new_game
        // and load_from_save do
        Reply show_new_world(Ctx& c, std::optional<int> save) {
            if (!c.gui.on_world)
                return Reply::error(String("this host cannot show a world"));
            c.gui.on_world(std::make_shared<WorldState>(c.gui, save));
            return Reply::ok();
        }

    } // namespace

    void register_world_commands(CommandRegistry& registry) {

        // The clock (commands.md section 8, multiplayer.md section 4):
        // the relay's, which only the host commands.  A joined client's
        // `step n` waits for n steps of the server's clock
        registry.add(make_command<Ctx>(
            "pause", "Stop the step clock (frames continue)", Category::clock,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                if (!ws._host)
                    return Reply::error(String("not the host"));
                ws._host->relay().pause();
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "resume", "Run the step clock again", Category::clock,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                if (!ws._host)
                    return Reply::error(String("not the host"));
                ws._host->relay().resume();
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "step", "Advance the world n steps (at once if paused), then reply", Category::clock,
            std::tuple{
                Param<Ctx, int64_t>{"n", "how many steps", int64_t{1}},
            },
            [](Ctx& c, int64_t n) {
                if (!c.world)
                    return Reply::error(String("no world is loaded"));
                if (n < 0)
                    return Reply::error(String("n must be non-negative"));
                WorldState& ws = *c.world;
                // The relay decides, in order behind any `pause` or
                // `resume` just issued; clock() here would still read the
                // state before them
                if (ws._host)
                    ws._host->relay().step_if_paused((uint64_t)n);
                return Reply::later(std::make_shared<StepPending>(
                    c.gui, &ws, ws._steps_taken + (uint64_t)n));
            }));

        // The session (multiplayer.md sections 2 and 6)
        registry.add(make_command<Ctx>(
            "host", "Listen for players on a TCP port (0 picks one); replies with the port", Category::session,
            std::tuple{
                Param<Ctx, int64_t>{"port", "the TCP port", int64_t{0}},
            },
            with_world([](WorldState& ws, int64_t port) {
                if (port < 0 || port > 65535)
                    return Reply::error(String("port must be 0 to 65535"));
                auto bound = ws.host_listen((uint16_t)port);
                if (!bound)
                    return Reply::error(bound.error());
                return Reply::ok(String(StringView(std::to_string(*bound).c_str())));
            })));

        registry.add(make_command<Ctx>(
            "join", "Join the game at address:port, starting from the host's save", Category::session,
            std::tuple{
                Param<Ctx, String>{"where", "the host's address:port"},
            },
            with_world([](WorldState& ws, String const& where) {
                std::string text(where.data(), where.chars.size());
                size_t colon = text.rfind(':');
                if (colon == std::string::npos || colon == 0 || colon + 1 == text.size())
                    return Reply::error(String("expected address:port"));
                int port = std::atoi(text.c_str() + colon + 1);
                if (port <= 0 || port > 65535)
                    return Reply::error(String("port must be 1 to 65535"));
                ws.join(String(StringView(text.substr(0, colon).c_str())), (uint16_t)port);
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "session", "This process's place in the game: hosting or joined, client id, steps applied", Category::local,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                std::string text;
                RemoteServer* client = ws._client();
                if (!client) {
                    if (ws._replay) {
                        text = "replay of " + ws._replay->path.string() + ": ";
                        if (ws._replay->result)
                            text += narrow(*ws._replay->result);
                        else
                            text += "step " + std::to_string((uint64_t)ws._world_to_render._ptr->_time)
                                  + " of " + std::to_string(ws._replay->through);
                        return Reply::ok(widen(text));
                    }
                    return Reply::ok(String("no session"));
                }
                text = ws._host ? "host" : "joined";
                switch (client->state()) {
                    case RemoteServer::State::CONNECTING: text += " connecting"; break;
                    case RemoteServer::State::HANDSHAKE: text += " handshake"; break;
                    case RemoteServer::State::SUBSCRIBED: text += " subscribed"; break;
                    case RemoteServer::State::JOINED: text += " joined"; break;
                    case RemoteServer::State::DISCONNECTED:
                        text += " disconnected: " + client->reason();
                        return Reply::ok(String(StringView(text.c_str())));
                }
                text += " client " + std::to_string(client->client_id());
                text += " head " + std::to_string(client->head());
                text += " applied " + std::to_string(ws._steps_taken);
                if (ws._awaiting_save)
                    text += " awaiting save";
                if (ws._host) {
                    Relay::Clock clock = ws._host->relay().clock();
                    text += clock.paused ? " paused" : " running";
                }
                if (ws._log && !ws._log->path().empty())
                    text += " log " + ws._log->path().string();
                return Reply::ok(widen(text));
            })));

        registry.add(make_command<Ctx>(
            "leave", "Leave the joined game and go on alone from here", Category::session,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                if (!ws._remote)
                    return Reply::error(String("not in a joined game"));
                ws.leave();
                return Reply::ok();
            })));

        // The games (commands.md section 7).  At the main menu, where no
        // world is showing, new-game and load build one for the host to
        // show (GuiContext::on_world); in a game they replace the world in
        // place, as the in-game menu always has.  Either way the session
        // ends and a fresh one is hosted from the new world (new_game,
        // load_from_save): a joined client leaves its game
        registry.add(make_command<Ctx>(
            "new-game", "Start a game from the starting scenario: from the main menu, or in place of the game showing (which ends its session)", Category::session,
            std::tuple<>{},
            [](Ctx& c) {
                if (!c.world)
                    return show_new_world(c, std::nullopt);
                c.world->new_game();
                return Reply::ok();
            }));

        registry.add(make_command<Ctx>(
            "save", "Save the displayed world as a new save; replies with its name once it is written", Category::session,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                auto result = std::make_shared<SaveResult>();
                if (!ws.save_current([result](int id) { result->id.store_release(id); }))
                    return Reply::error(String("no displayed world"));
                return Reply::later(std::make_shared<SavePending>(std::move(result)));
            })));

        registry.add(make_command<Ctx>(
            "load", "Load a save: from the main menu, or in place of the game showing (which ends its session)", Category::session,
            std::tuple{
                Param<Ctx, String>{"name", "the save, as list-saves names it"},
            },
            [](Ctx& c, String const& name) {
                const std::optional<int> id = save_named(name);
                if (!id)
                    return Reply::error(widen("no save named " + narrow(name)));
                if (!c.world)
                    return show_new_world(c, id);
                c.world->load_from_save(*id);
                return Reply::ok();
            }));

        registry.add(make_command<Ctx>(
            "list-saves", "The saves, one name per line, newest first", Category::session,
            std::tuple<>{},
            [](Ctx&) {
                std::string text;
                for (auto const& [file, id] : enumerate_games()) {
                    if (!text.empty())
                        text += "\n";
                    text += file;
                }
                return Reply::ok(widen(text));
            }));

        registry.add(make_command<Ctx>(
            "main-menu", "Quit to the main menu: end the game showing (and its session) and show the menu", Category::session,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                if (!ws._gui.on_main_menu)
                    return Reply::error(String("this host has no main menu"));
                ws._gui.on_main_menu();
                return Reply::ok();
            })));

        // The diagnosis harness (multiplayer.md section 5): the session
        // log replayed, two logs compared, the displayed World as text,
        // and a deliberate desync to exercise it all
        registry.add(make_command<Ctx>(
            "replay", "Replay a session log from its checkpoint, checking every recorded hash; the world at the last step replayed is displayed, with no session", Category::session,
            std::tuple{
                Param<Ctx, String>{"log", "the log file"},
                Param<Ctx, int64_t>{"through", "the last step to apply; the log's last if omitted", int64_t{-1}},
            },
            [](Ctx& c, String const& log, int64_t through) {
                if (!c.world)
                    return Reply::error(String("no world is loaded"));
                if (through < -1)
                    return Reply::error(String("through must be a step number"));
                WorldState& ws = *c.world;
                auto started = ws.replay(log, (through < 0) ? std::nullopt : std::optional<uint64_t>((uint64_t)through));
                if (!started)
                    return Reply::error(started.error());
                return Reply::later(std::make_shared<ReplayPending>(c.gui, &ws));
            }));

        registry.add(make_command<Ctx>(
            "compare", "Compare two session logs of one game: the steps they share, and the first where the streams and where the hashes differ", Category::local,
            std::tuple{
                Param<Ctx, String>{"a", "a log file"},
                Param<Ctx, String>{"b", "another log file"},
            },
            [](Ctx&, String const& a, String const& b) {
                auto ca = read_session_log(std::filesystem::path(narrow(a)));
                if (!ca)
                    return Reply::error(widen(ca.error()));
                auto cb = read_session_log(std::filesystem::path(narrow(b)));
                if (!cb)
                    return Reply::error(widen(cb.error()));
                return Reply::ok(widen(compare_session_logs(*ca, *cb)));
            }));

        registry.add(make_command<Ctx>(
            "dump", "The displayed world's state as text, for diffing against another's; written to a file if one is named, else the reply", Category::local,
            std::tuple{
                Param<Ctx, String>{"path", "the file to write", String("")},
            },
            with_world([](WorldState& ws, String const& path) {
                Root<World const*> w;
                if (!ws._worlds.try_pop_front(w))
                    return Reply::error(String("no displayed world"));
                const std::string text = dump_world(w._ptr);
                ws._worlds.push_front(std::move(w));
                if (path.chars.empty())
                    return Reply::ok(widen(text));
                std::ofstream out(narrow(path), std::ios::binary);
                out.write(text.data(), (std::streamsize)text.size());
                if (!out)
                    return Reply::error(widen("cannot write " + narrow(path)));
                const size_t lines = (size_t)std::count(text.begin(), text.end(), '\n');
                return Reply::ok(widen("wrote " + std::to_string(lines) + " lines to " + narrow(path)));
            })));

        registry.add(make_command<Ctx>(
            "corrupt", "Write a Term on a tile of the displayed world only, bypassing the server: a deliberate desync, for exercising the tripwire and the diagnosis harness", Category::local,
            std::tuple{
                cursor_param("at", "the tile to write"),
                Param<Ctx, Term>{"term", "what to write (null erases)"},
            },
            with_world([](WorldState& ws, Coordinate const& at, Term const& term) {
                ws.corrupt(at, term);
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "rotate", "Rotate the held opcode clockwise", Category::local,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                return reorient_hand(ws, opcode_rotated_clockwise);
            })));

        registry.add(make_command<Ctx>(
            "reverse-rotate", "Rotate the held opcode counterclockwise", Category::local,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                return reorient_hand(ws, opcode_rotated_counterclockwise);
            })));

        registry.add(make_command<Ctx>(
            "flip-horizontal", "Mirror the held opcode east-west", Category::local,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                return reorient_hand(ws, opcode_mirrored_horizontal);
            })));

        registry.add(make_command<Ctx>(
            "flip-vertical", "Mirror the held opcode north-south", Category::local,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                return reorient_hand(ws, opcode_mirrored_vertical);
            })));

        registry.add(make_command<Ctx>(
            "toggle-map", "Switch between the world and the map", Category::local,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                // Classify the current zoom and jump to the other view's
                // default: from any world zoom to the canonical map, from
                // any map zoom back to the default world view.
                ws.set_zoom(ws.show_map() ? WorldState::ZOOM_DEFAULT
                                          : WorldState::ZOOM_MAP_DEFAULT);
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "toggle-symbol-decals", "Show or hide the symbols' ambient-occlusion decals (debug)", Category::local,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                return toggle(ws, ws._show_symbol_decals, "symbol decals");
            })));

        registry.add(make_command<Ctx>(
            "ssao-repeat", "Encode the screen-space occlusion this many times per frame (measuring aid: with the GPU saturated by it, (frame - frame at 1) / (n - 1) is one pass's cost at full clock)", Category::local,
            std::tuple{
                Param<Ctx, int64_t>{"n", "1 to 64", 1},
            },
            with_world([](WorldState& ws, int64_t n) {
                ws._ssao_repeat = (int)std::clamp<int64_t>(n, 1, 64);
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "ssao", "Screen-space ambient occlusion: off, gtao or luft, with -show for the factor itself; then a radius in texels (128 per world unit), a strength in percent, and for GTAO the slices and steps per side and whether the taps read the depth pyramid; then the blur kernel and its taps per side", Category::local,
            std::tuple{
                Param<Ctx, String>{"mode", "off, gtao, luft, gtao-show or luft-show", {}, nullptr,
                                   {"off", "gtao", "luft", "gtao-show", "luft-show"}},
                Param<Ctx, int64_t>{"radius", "texels, 128 per world unit", 64},
                Param<Ctx, int64_t>{"strength", "percent; 100 = as estimated", 100},
                Param<Ctx, int64_t>{"slices", "GTAO slice directions per pixel, 1 to 16", 3},
                Param<Ctx, int64_t>{"steps", "GTAO taps per side per slice, 1 to 32", 4},
                Param<Ctx, int64_t>{"pyramid", "GTAO taps read the depth pyramid (1) or the Depth G-buffer (0)", 1},
                Param<Ctx, int64_t>{"blur", "blur kernel: 0 direct window, 1 separable, 2 tiled window", WRY_SSAO_BLUR_SEPARABLE},
                Param<Ctx, int64_t>{"blur-radius", "blur taps per side, 1 to 4 (2: a 5 x 5 window)", 2},
            },
            with_world([](WorldState& ws, String const& mode, int64_t radius_texels, int64_t strength_percent, int64_t slices, int64_t steps, int64_t pyramid, int64_t blur, int64_t blur_radius) {
                const double radius = radius_texels / 128.0;
                const double strength = strength_percent / 100.0;
                std::string_view m((const char*)mode.chars.data(), mode.chars.size());
                int which;
                bool show = false;
                if (m == "off") which = WRY_SSAO_OFF;
                else if (m == "gtao") which = WRY_SSAO_GTAO;
                else if (m == "luft") which = WRY_SSAO_LUFT;
                else if (m == "gtao-show") { which = WRY_SSAO_GTAO; show = true; }
                else if (m == "luft-show") { which = WRY_SSAO_LUFT; show = true; }
                else return Reply::error(String("mode must be off, gtao, luft, gtao-show or luft-show"));
                ws._ssao_mode = which;
                ws._ssao_show = show;
                ws._ssao_radius = (float)std::clamp(radius, 0.01, 8.0);
                ws._ssao_strength = (float)std::clamp(strength, 0.0, 4.0);
                ws._ssao_slices = (int)std::clamp<int64_t>(slices, 1, 16);
                ws._ssao_steps = (int)std::clamp<int64_t>(steps, 1, 32);
                ws._ssao_pyramid = pyramid != 0;
                ws._ssao_blur = (int)std::clamp<int64_t>(blur, WRY_SSAO_BLUR_DIRECT, WRY_SSAO_BLUR_TILED);
                ws._ssao_blur_radius = (int)std::clamp<int64_t>(blur_radius, 1, WRY_SSAO_BLUR_MAX_RADIUS);
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "board", "The terrain's look as a printed board: the lattice line's width in mm and its minimum width in pixels (thinner than that it fades instead), the line and mark opacities in percent, the tint's misregistration in mm and the width of its soft edge, the tint's strength in percent (0 is a white board), and a bias in mm on the marks' edges (negative grows them)", Category::local,
            std::tuple{
                Param<Ctx, int64_t>{"line-mm", "lattice line width, world mm", 30},
                Param<Ctx, int64_t>{"min-px", "minimum lattice line width on screen, pixels", 1},
                Param<Ctx, int64_t>{"line-alpha", "lattice opacity, percent", 80},
                Param<Ctx, int64_t>{"mark-alpha", "mark opacity, percent", 80},
                Param<Ctx, int64_t>{"shift-x-mm", "tint misregistration east, mm", 50},
                Param<Ctx, int64_t>{"shift-y-mm", "tint misregistration north, mm", -30},
                Param<Ctx, int64_t>{"blur-mm", "width of the tint's soft edge, mm", 60},
                Param<Ctx, int64_t>{"tint", "tint strength, percent; 0 is a white board", 100},
                Param<Ctx, int64_t>{"bias-mm", "mark edge bias, mm; negative grows the marks", 0},
            },
            with_world([](WorldState& ws, int64_t line_mm, int64_t min_px, int64_t line_alpha, int64_t mark_alpha, int64_t shift_x, int64_t shift_y, int64_t blur_mm, int64_t tint, int64_t bias_mm) {
                ws._board_line_width = (float)std::clamp<int64_t>(line_mm, 0, 500) / 1000.0f;
                ws._board_line_min_pixels = (float)std::clamp<int64_t>(min_px, 0, 16);
                ws._board_line_alpha = (float)std::clamp<int64_t>(line_alpha, 0, 100) / 100.0f;
                ws._board_mark_alpha = (float)std::clamp<int64_t>(mark_alpha, 0, 100) / 100.0f;
                ws._board_misregister_x = (float)std::clamp<int64_t>(shift_x, -500, 500) / 1000.0f;
                ws._board_misregister_y = (float)std::clamp<int64_t>(shift_y, -500, 500) / 1000.0f;
                ws._board_tint_blur = (float)std::clamp<int64_t>(blur_mm, 1, 1000) / 1000.0f;
                ws._board_tint = (float)std::clamp<int64_t>(tint, 0, 200) / 100.0f;
                ws._board_mark_bias = (float)std::clamp<int64_t>(bias_mm, -200, 200) / 1000.0f;
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "exposure", "Scene exposure in percent: scales the sun and the sky alike (100 lights a white board to about mid grey)", Category::local,
            std::tuple{
                Param<Ctx, int64_t>{"percent", "1 to 1000", 100},
            },
            with_world([](WorldState& ws, int64_t percent) {
                ws._exposure = (float)std::clamp<int64_t>(percent, 1, 1000) / 100.0f;
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "zoom", "The view's zoom in percent (100 is the default world view, 200 the closest; below 50 is map view, 1/64 of the world view at TAB's default): set it, or with 0 print it", Category::local,
            std::tuple{
                Param<Ctx, int64_t>{"percent", "0 to print; else 0.39 (1/256) to 200", 0},
            },
            with_world([](WorldState& ws, int64_t percent) {
                if (percent > 0)
                    ws.set_zoom((float)percent / 100.0f);
                char buffer[64];
                snprintf(buffer, sizeof buffer, "zoom %.4g%% (%s view)",
                         ws._zoom * 100.0f, ws.show_map() ? "map" : "world");
                return Reply::ok(String(StringView(buffer)));
            })));

        registry.add(make_command<Ctx>(
            "atlas", "Show the symbols from another atlas: assets/NAME.png (and the field beside it, built on first use): assets is the cutout glyphs, assets_tile the glyphs printed on tiles", Category::local,
            std::tuple{
                Param<Ctx, String>{"name", "the atlas's file name without .png", String("assets_tile")},
            },
            with_world([](WorldState& ws, String name) {
                ws._symbol_atlas = std::move(name);
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "token", "The playing pieces' proportions in world mm: the pentagon's width (the glyph's is 875), how tall it stands, and its edge roundover", Category::local,
            std::tuple{
                Param<Ctx, int64_t>{"width-mm", "the token's width; 875 matches its glyph", 875},
                Param<Ctx, int64_t>{"height-mm", "how tall it stands", 875},
                Param<Ctx, int64_t>{"fillet-mm", "edge roundover", 50},
            },
            with_world([](WorldState& ws, int64_t width, int64_t height, int64_t fillet) {
                ws._token_width = (float)std::clamp<int64_t>(width, 100, 1000) / 1000.0f;
                ws._token_height = (float)std::clamp<int64_t>(height, 10, 2000) / 1000.0f;
                ws._token_fillet = (float)std::clamp<int64_t>(fillet, 0, 500) / 1000.0f;
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "toggle-decals", "Show or hide the symbols' ambient-occlusion decals on the ground (off by default; GTAO covers it)", Category::local,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                return toggle(ws, ws._symbol_decals, "Symbol decals");
            })));

        registry.add(make_command<Ctx>(
            "toggle-jacobians", "Show or hide Jacobians (debug)", Category::local,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                return toggle(ws, ws._show_jacobian, "Jacobians");
            })));

        registry.add(make_command<Ctx>(
            "toggle-points", "Show or hide points (debug)", Category::local,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                return toggle(ws, ws._show_points, "points");
            })));

        registry.add(make_command<Ctx>(
            "toggle-wireframe", "Show or hide wireframe (debug)", Category::local,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                return toggle(ws, ws._show_wireframe, "wireframe");
            })));

        // Factorio's Q: a full hand is emptied; an empty hand picks up the
        // tile.  (So `pipette Coordinate(3, 4)` replays as a drop if the
        // hand was full -- the hand is part of the state it replays in.)
        registry.add(make_command<Ctx>(
            "pipette", "Pick up the Term on a tile, or empty a full hand", Category::local,
            std::tuple{
                cursor_param("at", "the tile to pick up from"),
            },
            with_world([](WorldState& ws, Coordinate const& at) {
                if (ws.is_holding())
                    ws.drop_hand();
                else
                    (void) ws.pipette_at(at);
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "hold", "Put a Term in the hand (null empties it)", Category::local,
            std::tuple{
                Param<Ctx, Term>{"term", "what to hold"},
            },
            with_world([](WorldState& ws, Term const& term) {
                ws.hold(term);
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "drop", "Empty the hand", Category::local,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                ws.drop_hand();
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "get", "Show the Term on a tile of the displayed world", Category::local,
            std::tuple{
                cursor_param("at", "the tile to read"),
            },
            with_world([](WorldState& ws, Coordinate const& at) {
                Root<World const*> w;
                if (!ws._worlds.try_pop_front(w))
                    return Reply::error(String("no displayed world"));
                Term t{};
                const bool has = w._ptr->_term_for_coordinate.try_get(at, t);
                ws._worlds.push_front(std::move(w));
                if (!has)
                    t = term_make_null();
                if (!command::printable(t))
                    return Reply::ok(String("(a Term with no literal form)"));
                return Reply::ok(command::to_string(t));
            })));

        // The content hash of the displayed world (multiplayer.md 9.1): what
        // lockstep peers compare per step, stable across save and load.
        registry.add(make_command<Ctx>(
            "state-hash", "Show the content hash of the displayed world", Category::local,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                Root<World const*> w;
                if (!ws._worlds.try_pop_front(w))
                    return Reply::error(String("no displayed world"));
                const uint64_t h = w._ptr->content_hash();
                ws._worlds.push_front(std::move(w));
                return Reply::ok(command::to_string(Hash{h}));
            })));

        // The camera follows a truck: the machine occupying the tile, or
        // (no machine there) nobody.  Left click with an empty hand.
        registry.add(make_command<Ctx>(
            "follow", "Follow the truck on a tile with the camera (none there: stop)", Category::local,
            std::tuple{
                cursor_param("at", "the truck's tile"),
            },
            with_world([](WorldState& ws, Coordinate const& at) {
                const EntityID id = ws.machine_at(at);
                if (!id) {
                    const bool was = (bool)ws._follow;
                    ws._follow = {};
                    return Reply::ok(String(was ? "stopped following" : "no truck there"));
                }
                ws._follow = id;
                return Reply::ok(String(("following truck " + std::to_string(id.data)).c_str()));
            })));

        // World edits: built into a Player::Action and submitted; the
        // Server orders them and they apply when they come back in a step.
        registry.add(make_command<Ctx>(
            "write", "Write a Term on a tile", Category::world,
            std::tuple{
                cursor_param("at", "the tile to write"),
                Param<Ctx, Term>{"term", "what to write"},
            },
            with_world([](WorldState& ws, Coordinate const& at, Term const& term) {
                return submit_write(ws, at, term);
            })));

        // A null write erases the cell's key (the value-plane combine in
        // World::step); matter included -- this is the editing tool, not a
        // machine's guarded STORE.
        registry.add(make_command<Ctx>(
            "erase", "Erase a tile", Category::world,
            std::tuple{
                cursor_param("at", "the tile to erase"),
            },
            with_world([](WorldState& ws, Coordinate const& at) {
                return submit_write(ws, at, term_make_null());
            })));
    }

    // Whatever the overlay stack didn't claim: the world's ground-plane click,
    // scroll-pan, ESC-opens-the-in-game-menu, hex-key writes, and debug toggles.
    void WorldState::pump_legacy_event(gui::Event const& e) {
        using namespace ::wry::gui;

        switch (e.kind) {

            case WryEventKindMouseUp:
                // No world edits in map mode.  Left places what the hand
                // holds -- an empty hand places nothing, so erasing is
                // never an accident -- and right erases.
                if (show_map())
                    break;
                // A click off the ground edits nothing.
                if (auto tile = cursor_tile()) {
                    if (e.button == MouseButton::Left) {
                        // an empty hand on a truck follows it (a command,
                        // so it is in the history); on anything else it
                        // issues nothing and just stops following
                        if (is_holding())
                            (void) _gui.issue_with(command::Origin::gui, "write",
                                                   *tile, _holding_value._value);
                        else if (machine_at(*tile))
                            (void) _gui.issue_with(command::Origin::gui, "follow", *tile);
                        else {
                            _follow = {};
                            _follow_drawn_at.reset();
                        }
                    } else if (e.button == MouseButton::Right) {
                        (void) _gui.issue_with(command::Origin::gui, "erase", *tile);
                    }
                }
                break;

            case WryEventKindScroll: {
                // Scroll deltas are drawable pixels, 1024 of them to the
                // tile at the default zoom; the content follows the
                // fingers, so the focus moves against the delta (image y
                // is down, world y is up).  The view shows 1 / zoom times
                // the default field, so a swipe traverses 1 / zoom times
                // the world distance for the same on-screen travel, in
                // world and map view alike.  The focus is shared: leaving
                // map view keeps you looking at the spot you panned to.
                float shuttle = 1.0f / (_zoom * 1024.0f);
                // panning takes the camera back from a followed truck
                _follow = {};
                _follow_drawn_at.reset();
                pan_focus(make<float2>(-e.scroll_delta.x * shuttle,
                                       +e.scroll_delta.y * shuttle));
                break;
            }

            case WryEventKindMagnify: {
                // Trackpad pinch zooms, in world and map view alike, and
                // across the boundary between them.  Anchored at the
                // cursor: the ground point under the fingers stays put,
                // like every maps app.
                //
                // Either view is render space scaled by Z about the
                // focus f (literally so for the map quad; a dolly about
                // the focus comes to the same thing), so a ground point q
                // lands at (q - f) * Z.  Holding the cursor's point q
                // fixed on screen across Z -> Z' needs
                // (q - f) * Z == (q - f') * Z', so
                //     f' = q - (q - f) * (Z / Z').
                float factor = 1.0f + e.magnification;
                if (factor < 0.5f) factor = 0.5f;   // one event is a nudge
                if (factor > 2.0f) factor = 2.0f;
                float old_zoom = _zoom;

                // q: the cursor's ground-plane point under the CURRENT
                // view: the camera's own projection (which carries the
                // world-view dolly) with the map's model matrix in map
                // view.
                simd_float4x4 A = _uniforms.viewprojection_transform;
                if (show_map())
                    A = simd_mul(A, map_model_transform(old_zoom));
                simd_float4 b = simd_make_float4(_mouse.x, _mouse.y, 0.0f, 1.0f);
                float2 q = project_screen_ray(A, b);

                set_zoom(old_zoom * factor);
                if (_zoom == old_zoom)
                    break;
                float s = old_zoom / _zoom;
                pan_focus((q - _focus_fraction) * (1.0f - s));
                break;
            }

            case WryEventKindKeyDown: {
                // Escape is reserved (not bindable).  Holding a glyph is
                // a modal state and Escape leaves the innermost one first:
                // with something in hand it drops the hand; with an empty
                // hand it opens the in-game menu.
                if (e.key == key::Escape) {
                    if (is_holding())
                        drop_hand();
                    else
                        _stack.push(&_main_menu_overlay);
                    break;
                }

                // User-bound actions (settings.json via the live keymap).
                // Bound combos win over the hex-write fallback below, so
                // binding e.g. F predictably shadows the hex digit.
                std::optional<Action> action =
                    _gui.settings.keymap.action_from_combo(combo_from_event(e));
                if (action) {
                    // Toggles don't re-fire while the key is held; cycles
                    // (rotate) do.  Either way the keystroke is consumed.
                    // The binding names the command; its parameters take
                    // their defaults (the tile under the cursor).
                    if (!e.is_repeat || action_fires_on_repeat(*action))
                        (void) _gui.issue(command::Origin::binding,
                                          id_from_action(*action));
                    break;
                }

                // Unbound keys: hex-key writes are world edits, and not in
                // map mode.
                if (!show_map() &&
                    ((e.key >= '0' && e.key <= '9') ||
                     (e.key >= 'a' && e.key <= 'f'))) {
                    int64_t k = wry::base36::from_base36_table[e.key];
                    if (auto tile = cursor_tile())
                        (void) _gui.issue_with(command::Origin::gui, "write",
                                               *tile, term_make_integer_with(k));
                }
                break;
            }

            default:
                break;
        }
    }

    // World input pump, moved here from the free gui::pump.  Drains the shared
    // event queue, promotes point locations to drawable pixels, dispatches
    // through the overlay stack, and routes the rest to pump_legacy_event.
    void WorldState::handle_events(float2 view_size_pt) {
        using namespace ::wry::gui;

        // before this frame's input, so a pan or a new `follow` in it wins
        follow_camera();

        const float w_pt = (view_size_pt.x > 0.0f) ? view_size_pt.x : 1.0f;
        const float h_pt = (view_size_pt.y > 0.0f) ? view_size_pt.y : 1.0f;
        const float scale_x = _gui.viewport_size.x / w_pt;
        const float scale_y = _gui.viewport_size.y / h_pt;

        while (!_gui.events.empty()) {
            Event e = _gui.events.pop_front();

            const bool is_positional =
                (e.kind == WryEventKindMouseMove  ||
                 e.kind == WryEventKindMouseDown  ||
                 e.kind == WryEventKindMouseUp    ||
                 e.kind == WryEventKindMouseEnter ||
                 e.kind == WryEventKindMouseExit  ||
                 e.kind == WryEventKindScroll     ||
                 e.kind == WryEventKindMagnify);

            if (is_positional) {
                e.location.x *= scale_x;
                e.location.y *= scale_y;
                // Keep _mouse (NDC, y-up) in lockstep with the dispatched event.
                _mouse.x = 2.0f * e.location.x / _gui.viewport_size.x - 1.0f;
                _mouse.y = 1.0f - 2.0f * e.location.y / _gui.viewport_size.y;
            }

            // App-tier overlays (console / log) get first crack -- the console
            // swallows keystrokes when open -- then the world's palette stack
            // (and any in-game menu pushed onto it), then the legacy fallback.
            // In map mode the palette is hidden and must be inert too, so the
            // scene stack is skipped while it holds only the palette; a menu
            // or save list pushed above it restores normal dispatch.
            bool stack_active = !show_map() || (_stack.size() > 1);
            if (!_gui.overlays.dispatch(e))
                if (!(stack_active && _stack.dispatch(e)))
                    pump_legacy_event(e);
        }
    }

    // ----------------------------------------------------------------
    // Tests.

    // The camera focus: panning carries whole tiles into the focus tile
    // and keeps the fraction sub-tile; render-space positions are offsets
    // from that tile, wrapping with the i32 coordinates; and the cursor at
    // the centre of the viewport lands on the focus tile however far it
    // has been panned, through the view transform the fraction rides in.
    define_test("world_state_focus") {
        coroutine::Nursery scope;
        GuiContext gc;
        gc.scope = &scope;
        gc.viewport_size = float2{1280.0f, 800.0f};
        WorldState ws(gc);

        auto near = [](float a, float b) { return std::abs(a - b) < 1e-4f; };

        assert((ws._focus_tile == Coordinate{0, 0}));
        assert(ws.cursor_tile() && (*ws.cursor_tile() == Coordinate{0, 0}));

        ws.pan_focus(float2{3.25f, -1.75f});
        assert((ws._focus_tile == Coordinate{3, -2}));
        assert(near(ws._focus_fraction.x, 0.25f) && near(ws._focus_fraction.y, 0.25f));
        assert(near(ws.render_position(Coordinate{5, 1}).x, 2.0f));
        assert(near(ws.render_position(Coordinate{5, 1}).y, 3.0f));
        assert((ws.world_tile(float2{2.0f, 3.0f}) == Coordinate{5, 1}));
        assert(!ws.world_tile(float2{NAN, 0.0f}));
        assert(!ws.world_tile(float2{0.0f, 3e9f}));
        // The screen centre looks at the focus: with the fraction at
        // (0.25, 0.25), that rounds to the focus tile
        assert(ws.cursor_tile() && (*ws.cursor_tile() == Coordinate{3, -2}));
        ws.pan_focus(float2{0.5f, 0.5f});
        assert(ws.cursor_tile() && (*ws.cursor_tile() == Coordinate{4, -1}));

        // Far out: positions near the focus are exact, and the coordinates
        // wrap around the i32 range
        ws._focus_tile = Coordinate{INT32_MAX, INT32_MIN};
        ws._focus_fraction = float2{0.0f, 0.0f};
        ws._regenerate_uniforms();
        assert(near(ws.render_position(Coordinate{INT32_MIN, INT32_MAX}).x, 1.0f));
        assert(near(ws.render_position(Coordinate{INT32_MIN, INT32_MAX}).y, -1.0f));
        assert((ws.world_tile(float2{1.0f, -1.0f}) == Coordinate{INT32_MIN, INT32_MAX}));
        assert(ws.cursor_tile() && (*ws.cursor_tile() == Coordinate{INT32_MAX, INT32_MIN}));
        ws.pan_focus(float2{1.0f, -1.0f});
        assert((ws._focus_tile == Coordinate{INT32_MIN, INT32_MAX}));

        // A scroll event pans against its delta, 1024 pixels to the tile
        ws._focus_tile = Coordinate{0, 0};
        ws._focus_fraction = float2{0.0f, 0.0f};
        gui::Event e{};
        e.kind = WryEventKindScroll;
        e.scroll_delta = float2{1024.0f, -2048.0f};
        ws.pump_legacy_event(e);
        assert((ws._focus_tile == Coordinate{-1, -2}));
        assert(near(ws._focus_fraction.x, 0.0f) && near(ws._focus_fraction.y, 0.0f));

        // Zoomed, the same swipe traverses 1 / zoom as far: a tile per
        // 2048 pixels at the closest world view, 64 tiles per 1024 in the
        // canonical map view
        ws._focus_tile = Coordinate{0, 0};
        ws._focus_fraction = float2{0.0f, 0.0f};
        ws.set_zoom(WorldState::ZOOM_MAX);
        assert(!ws.show_map());
        e.scroll_delta = float2{2048.0f, -2048.0f};
        ws.pump_legacy_event(e);
        assert((ws._focus_tile == Coordinate{-1, -1}));
        assert(near(ws._focus_fraction.x, 0.0f) && near(ws._focus_fraction.y, 0.0f));
        ws._focus_tile = Coordinate{0, 0};
        ws.set_zoom(WorldState::ZOOM_MAP_DEFAULT);
        assert(ws.show_map());
        e.scroll_delta = float2{1024.0f, 0.0f};
        ws.pump_legacy_event(e);
        assert((ws._focus_tile == Coordinate{-64, 0}));
        assert(near(ws._focus_fraction.x, 0.0f) && near(ws._focus_fraction.y, 0.0f));
        ws.set_zoom(WorldState::ZOOM_DEFAULT);

        co_await ws.end_session(std::chrono::milliseconds(200));
        (void) co_await scope.join();
        co_return;
    };

    // The zoom: a dolly in world view (the camera's distance from the
    // focus scales by 1 / zoom, its direction and field of view do not),
    // map view below ZOOM_MAP under the default camera, TAB jumping
    // between the two views' defaults, and the pinch holding the ground
    // point under the cursor still in either view
    define_test("world_state_zoom") {
        coroutine::Nursery scope;
        GuiContext gc;
        gc.scope = &scope;
        gc.viewport_size = float2{1280.0f, 800.0f};
        WorldState ws(gc);
        auto near = [](float a, float b) { return std::abs(a - b) < 1e-3f; };
        auto camera = [&] {
            float4 c = ws._uniforms.camera_position_world;
            return make<float3>(c.x - ws._focus_fraction.x, c.y - ws._focus_fraction.y, c.z);
        };
        const float3 c1 = camera();
        assert(ws._zoom == WorldState::ZOOM_DEFAULT && !ws.show_map());

        // Dolly: half the distance at 2x, along the same line
        ws.set_zoom(2.0f);
        assert(near(simd_length(camera()), 0.5f * simd_length(c1)));
        assert(near(simd_length(simd_normalize(camera()) - simd_normalize(c1)), 0.0f));
        // Clamped at the ends
        ws.set_zoom(100.0f);
        assert(ws._zoom == WorldState::ZOOM_MAX);
        ws.set_zoom(0.0f);
        assert(ws._zoom == WorldState::ZOOM_MIN && ws.show_map());
        // The threshold, and the default camera on the map side of it
        ws.set_zoom(WorldState::ZOOM_MAP);
        assert(!ws.show_map());
        assert(near(simd_length(camera()), 2.0f * simd_length(c1)));
        ws.set_zoom(std::nextafter(WorldState::ZOOM_MAP, 0.0f));
        assert(ws.show_map());
        assert(near(simd_length(camera() - c1), 0.0f));

        // TAB classifies and jumps to the other view's default
        ws.set_zoom(1.7f);
        (void) gc.issue(command::Origin::binding, StringView("toggle-map"));
        assert(ws._zoom == WorldState::ZOOM_MAP_DEFAULT && ws.show_map());
        ws.set_zoom(WorldState::ZOOM_MIN);
        (void) gc.issue(command::Origin::binding, StringView("toggle-map"));
        assert(ws._zoom == WorldState::ZOOM_DEFAULT && !ws.show_map());

        // The pinch anchors the ground under the cursor: the world point
        // there (focus tile plus the render-space point, as the handler
        // finds it under the current view) is the same before and after
        ws._mouse = float2{0.3f, -0.45f};
        auto under_cursor = [&] {
            simd_float4x4 A = ws._uniforms.viewprojection_transform;
            if (ws.show_map())
                A = simd_mul(A, ws.map_model_transform(ws._zoom));
            float2 q = project_screen_ray(A, make<float4>(ws._mouse, 0.0f, 1.0f));
            return simd_make_double2((double)ws._focus_tile.x + q.x,
                                     (double)ws._focus_tile.y + q.y);
        };
        auto pinch = [&](float magnification) {
            gui::Event e{};
            e.kind = WryEventKindMagnify;
            e.magnification = magnification;
            ws.pump_legacy_event(e);
        };
        for (float zoom : {1.0f, 1.5f, WorldState::ZOOM_MAP_DEFAULT, 0.51f}) {
            for (float m : {0.4f, -0.3f, -0.05f}) {
                ws._focus_tile = Coordinate{5, -3};
                ws._focus_fraction = float2{0.0f, 0.0f};
                ws.set_zoom(zoom);
                simd_double2 before = under_cursor();
                pinch(m);
                assert(ws._zoom != zoom);
                simd_double2 after = under_cursor();
                assert(simd_length(after - before) < 1e-3);
            }
        }
        // ... and across the boundary: from the world side into map view
        ws._focus_tile = Coordinate{5, -3};
        ws._focus_fraction = float2{0.0f, 0.0f};
        ws.set_zoom(0.55f);
        assert(!ws.show_map());
        simd_double2 before = under_cursor();
        pinch(-0.2f);
        assert(ws.show_map());
        assert(simd_length(under_cursor() - before) < 1e-3);

        co_await ws.end_session(std::chrono::milliseconds(200));
        (void) co_await scope.join();
        co_return;
    };

    // The camera ray basis reproduces the projection: camera + ray(ndc) *
    // depth projects back to ndc at clip w = depth, across the screen and
    // the depth range, so the shaders' matrix-free position recovery
    // matches the rasterizer's
    define_test("world_state_camera_rays") {
        coroutine::Nursery scope;
        GuiContext gc;
        gc.scope = &scope;
        gc.viewport_size = float2{1280.0f, 800.0f};
        WorldState ws(gc);
        ws.pan_focus(float2{3.25f, -1.75f});
        const auto& u = ws._uniforms;
        for (float x : {-1.0f, -0.3f, 0.0f, 0.7f, 1.0f}) {
            for (float y : {-1.0f, 0.1f, 1.0f}) {
                for (float depth : {6.0f, 17.5f, 40.0f}) {
                    float4 ray = u.camera_ray_centre + x * u.camera_ray_dx + y * u.camera_ray_dy;
                    float4 p = u.camera_position_world + ray * depth;
                    p.w = 1.0f;
                    float4 c = simd_mul(u.viewprojection_transform, p);
                    assert(std::abs(c.w - depth) < 1e-3f * depth);
                    assert(std::abs(c.x / c.w - x) < 1e-4f);
                    assert(std::abs(c.y / c.w - y) < 1e-4f);
                }
            }
        }
        // The camera itself is at zero depth
        assert(std::abs(simd_mul(u.viewprojection_transform, u.camera_position_world).w) < 1e-4f);
        co_await ws.end_session(std::chrono::milliseconds(200));
        (void) co_await scope.join();
        co_return;
    };

    // The pump's contract for the hand and the erase gesture: an
    // empty-hand left click places nothing, a right click issues an erase
    // (a null write, which erases the cell's key), a full-hand left click
    // issues a write of what is held, map mode edits nothing, and Escape
    // drops a full hand before it opens the menu.  Gestures are commands,
    // so the history records them.  The server round trip is covered
    // elsewhere (player_null_write_erases_key).
    define_test("world_state_hand_and_erase_input") {
        coroutine::Nursery scope;
        GuiContext gc;
        gc.scope = &scope;

        // With no viewport the cursor projection is degenerate: a click is
        // off the ground and edits nothing (and must not trap).
        {
            WorldState ws(gc);
            assert(!ws.cursor_tile());
            ws._holding_value = term_make_opcode(OPCODE_HALT);
            const size_t issued = gc.history.size();
            gui::Event e{};
            e.kind = WryEventKindMouseUp;
            ws.pump_legacy_event(e);
            assert(gc.history.size() == issued);
            co_await ws.end_session(std::chrono::milliseconds(200));
        }

        // A real viewport, cursor at its center: over the ground.
        gc.viewport_size = float2{1280.0f, 800.0f};
        WorldState ws(gc);
        assert(ws.cursor_tile());

        auto mouse_up = [](gui::MouseButton b) {
            gui::Event e{};
            e.kind = WryEventKindMouseUp;
            e.button = b;
            return e;
        };
        auto key_down = [](uint32_t k) {
            gui::Event e{};
            e.kind = WryEventKindKeyDown;
            e.key = k;
            return e;
        };

        // The line most recently issued, and whether it starts with `prefix`.
        auto last_issued_starts_with = [&](const char* prefix) {
            if (gc.history.empty())
                return false;
            String const& line = gc.history.back().line;
            const size_t n = std::strlen(prefix);
            return line.chars.size() >= n
                && std::memcmp(line.chars.begin(), prefix, n) == 0;
        };
        const String tile = command::to_string(*ws.cursor_tile());

        // Empty hand: left places nothing, right issues an erase.
        assert(!ws.is_holding());
        size_t issued = gc.history.size();
        ws.pump_legacy_event(mouse_up(gui::MouseButton::Left));
        assert(gc.history.size() == issued);
        ws.pump_legacy_event(mouse_up(gui::MouseButton::Right));
        assert(gc.history.size() == issued + 1);
        assert(gc.history.back().origin == command::Origin::gui);
        {
            String expected("erase ");
            expected.append(tile);
            assert(gc.history.back().line == expected);
        }

        // Full hand: left writes what is held, at the cursor tile.
        ws._holding_value = term_make_opcode(OPCODE_HALT);
        assert(ws.is_holding());
        ws.pump_legacy_event(mouse_up(gui::MouseButton::Left));
        {
            String expected("write ");
            expected.append(tile);
            expected.append(StringView(" OPCODE::HALT"));
            assert(gc.history.back().line == expected);
        }
        assert(last_issued_starts_with("write "));

        // Map view: neither button edits.
        ws.set_zoom(WorldState::ZOOM_MAP_DEFAULT);
        assert(ws.show_map());
        issued = gc.history.size();
        ws.pump_legacy_event(mouse_up(gui::MouseButton::Left));
        ws.pump_legacy_event(mouse_up(gui::MouseButton::Right));
        assert(gc.history.size() == issued);
        ws.set_zoom(WorldState::ZOOM_DEFAULT);
        assert(!ws.show_map());

        // Escape: the first drops the hand without opening the menu, the
        // second opens the menu.
        const std::size_t depth = ws._stack.size();
        ws.pump_legacy_event(key_down(gui::key::Escape));
        assert(!ws.is_holding());
        assert(ws._stack.size() == depth);
        assert(!ws._stack.contains(&ws._main_menu_overlay));
        assert(ws._palette_overlay.selected_i() == -1);
        assert(ws._palette_overlay.cursor_needs_refresh());
        ws.pump_legacy_event(key_down(gui::key::Escape));
        assert(ws._stack.size() == depth + 1);
        assert(ws._stack.contains(&ws._main_menu_overlay));

        co_await ws.end_session(std::chrono::milliseconds(200));
        (void) co_await scope.join();
        co_return;
    };

    // Pipette: with an empty hand Q copies the tile under the cursor into
    // the hand, matter excepted; with a full hand Q drops it.  Sampling
    // reads the displayed world -- here the starting world laid out by
    // make_starting_world, before any step.
    // The hint over the palette names only the keys that change what is
    // in hand (opcode.hpp's symmetry classes), as bound by default
    define_test("world_state_hand_hint") {
        coroutine::Nursery scope;
        GuiContext gc;
        gc.scope = &scope;
        {
            WorldState ws(gc);
            auto has = [](std::string const& h, const char* s) { return h.find(s) != std::string::npos; };
            assert(ws.hand_hint().empty());
            ws.hold(term_make_opcode(OPCODE_TURN_NORTH));   // direction-like
            std::string h = ws.hand_hint();
            assert(has(h, "[R] rotate") && has(h, "[Shift+R] rotate back"));
            assert(has(h, "[V] mirror north-south") && !has(h, "east-west"));
            ws.hold(term_make_opcode(OPCODE_TURN_LEFT));    // handedness-like
            h = ws.hand_hint();
            assert(!has(h, "rotate") && has(h, "[H] mirror") && !has(h, "north-south"));
            ws.hold(term_make_opcode(OPCODE_VALVE_NORTH_SOUTH));   // axis-like
            h = ws.hand_hint();
            assert(has(h, "[R] rotate") && !has(h, "rotate back") && !has(h, "mirror"));
            ws.hold(term_make_opcode(OPCODE_ADD));          // invariant
            h = ws.hand_hint();
            assert(!has(h, "rotate") && !has(h, "mirror") && has(h, "[click] place"));
            co_await ws.end_session(std::chrono::milliseconds(200));
        }
        (void) co_await scope.join();
        co_return;
    };

    define_test("world_state_pipette") {
        coroutine::Nursery scope;
        GuiContext gc;
        gc.scope = &scope;
        gc.viewport_size = float2{1280.0f, 800.0f};   // cursor over the ground
        WorldState ws(gc);

        // An empty tile: nothing happens.
        assert(!ws.pipette_at(Coordinate{50, 50}));
        assert(!ws.is_holding());

        // A number, then an opcode, are copied into the hand.
        assert(ws.pipette_at(Coordinate{-2, -2}));
        assert(ws._holding_value._value._data == term_make_integer_with(7)._data);
        assert(ws.pipette_at(Coordinate{0, 4}));
        assert(ws._holding_value._value._data == term_make_opcode(OPCODE_FLIP_FLOP)._data);
        assert(ws._palette_overlay.cursor_needs_refresh());

        // Matter is not: the hand keeps what it had.
        assert(!ws.pipette_at(Coordinate{-2, 2}));
        assert(ws._holding_value._value._data == term_make_opcode(OPCODE_FLIP_FLOP)._data);

        // The command: a full hand drops, an empty hand picks up at the
        // given tile (or, by default, the tile under the cursor).
        gc.issue(command::Origin::console, "pipette");
        assert(!ws.is_holding());
        gc.issue(command::Origin::console, "pipette Coordinate(-2, -2)");
        assert(ws._holding_value._value._data == term_make_integer_with(7)._data);
        assert(gc.history.back().line == StringView("pipette Coordinate(-2, -2)"));

        // hold / drop / get.
        gc.issue(command::Origin::console, "hold OPCODE::SWAP");
        assert(ws._holding_value._value._data == term_make_opcode(OPCODE_SWAP)._data);
        gc.issue(command::Origin::console, "drop");
        assert(!ws.is_holding());
        {
            auto o = gc.issue(command::Origin::console, "get Coordinate(0, 4)");
            assert(o.reply.text == StringView("OPCODE::FLIP_FLOP"));
            auto empty = gc.issue(command::Origin::console, "get Coordinate(50, 50)");
            assert(empty.reply.text == StringView("null"));
        }

        // A binding issues its command with defaults resolved.
        gc.issue(command::Origin::binding, "rotate");
        assert(gc.history.back().origin == command::Origin::binding);

        co_await ws.end_session(std::chrono::milliseconds(200));
        (void) co_await scope.join();
        co_return;
    };

    namespace {

        using testing::eventually;

    } // namespace

    // The clock commands address the relay: pause stops it (a PAUSED step
    // closes the stream for now); step n has it finalize n steps at once,
    // and the command replies once this client has applied them, the
    // console's next line waiting behind it; cancelling stops the wait;
    // resume starts the clock again.  update() itself is not driven here
    // (it repins and forks the step); what it does with the steps is.
    define_test("world_state_clock_commands") {
        coroutine::Nursery scope;
        GuiContext gc;
        gc.scope = &scope;
        {
            WorldState ws(gc);
            using command::Reply;
            assert(ws._host);
            Relay& relay = ws._host->relay();

            // Apply every step that has arrived, as update() would
            auto apply_available = [&]() -> coroutine::Task {
                for (Step& s : ws._server->take_steps())
                    ws._backlog.push_back(std::move(s));
                while (!ws._backlog.empty()) {
                    Step step = std::move(ws._backlog.front());
                    ws._backlog.pop_front();
                    co_await ws._apply_step_async(step);
                }
            };
            // ... until this client has applied `target` steps and is level
            // with the relay's head (a posted step is processed on the
            // pool, so level alone could be early)
            auto catch_up = [&](uint64_t target) -> coroutine::Task {
                for (int i = 0; i != 2000; ++i) {
                    co_await apply_available();
                    if ((ws._steps_taken >= target) && (ws._steps_taken == relay.clock().head))
                        co_return;
                    co_await cancelable_sleep(0.005);
                }
                assert(!"did not catch up");
            };

            gc.issue(command::Origin::console, "pause");
            bool paused = co_await eventually([&] { return relay.clock().paused; });
            assert(paused);
            (void) paused;
            co_await catch_up(relay.clock().head);
            const uint64_t before = ws._steps_taken;
            assert(before >= 1);                            // the PAUSED step at least

            gc.submit(gc.console_source, "step 2");
            gc.submit(gc.console_source, "help step");
            const uint64_t step_seq = gc.history.back().seq;
            assert(gc.history.back().status == Reply::PENDING);
            assert(gc.console_source.queued.size() == 1);   // help waits

            // The relay finalized two steps at once; applying them completes
            // the command, and nothing else arrives while paused
            co_await catch_up(before + 2);
            assert(ws._steps_taken == before + 2);
            ++gc.frame;
            gc.service_commands();
            assert(!gc.console_source.pending);
            assert(gc.history.back().line == StringView("help \"step\""));
            for (auto const& e : gc.history)
                if (e.seq == step_seq)
                    assert(e.status == Reply::OK && e.completed_frame == gc.frame);

            // Cancel: the wait ends; the steps still come
            gc.submit(gc.console_source, "step 5");
            assert(gc.history.back().status == Reply::PENDING);
            gc.cancel(gc.console_source);
            assert(gc.history.back().status == Reply::CANCELLED);
            co_await catch_up(before + 7);
            assert(ws._steps_taken == before + 7);

            // Resume: the clock runs again
            gc.issue(command::Origin::console, "resume");
            bool running = co_await eventually([&] { return !relay.clock().paused && relay.clock().head > before + 7; });
            assert(running);
            (void) running;

            co_await ws.end_session(std::chrono::milliseconds(200));
        }
        (void) co_await scope.join();
        co_return;
    };


    // Per-step cost on the test worlds, as a number to compare across
    // builds (the content hash must be cheap enough to run every step),
    // and the stepped world's incrementally maintained hash checked
    // against the from-scratch oracle.  The starting scenario always; a
    // scaled-down stress scenario (900 machines over a 300 x 300 region)
    // only when WRY_BENCH is set, since it takes long enough to be
    // unwelcome in a routine suite run.  Prints microseconds per step and
    // per World-hash recompute (the combine over the cached parts, as
    // step() does it).
    define_test("world_step_cost") {
        auto bench = [](World* w, int steps, const char* label) -> coroutine::Task {
            Root<World*> r{w};
            const bool trace = std::getenv("WRY_BENCH_TRACE") != nullptr;
            auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i != steps; ++i) {
                epoch::Epoch pin = pin_global_epoch();
                Root<World*> next = co_await r._ptr->step();
                unpin_global_epoch(pin);
                r = std::move(next);
                if (trace)
                    printf("world_step_cost: %s: step %d hash %016llx\n",
                           label, i + 1, (unsigned long long)r._ptr->content_hash());
            }
            auto t1 = std::chrono::steady_clock::now();
            double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / steps;
            const uint64_t live = r._ptr->content_hash();
            const int reps = 1000;
            uint64_t recomputed = 0;
            auto t2 = std::chrono::steady_clock::now();
            for (int i = 0; i != reps; ++i)
                recomputed += r._ptr->_compute_content_hash();
            auto t3 = std::chrono::steady_clock::now();
            double hash_us = std::chrono::duration<double, std::micro>(t3 - t2).count() / reps;
            // The oracle, printed as well as asserted: a Release build has
            // no asserts, and a mismatch there is what we want to see.
            const uint64_t oracle = world_content_hash_oracle(r._ptr);
            printf("world_step_cost: %s: %d steps, %.1f us/step; World hash combine %.2f us; "
                   "final time %lld, %zu entities, hash %016llx, oracle %016llx%s\n",
                   label, steps, us, hash_us, (long long)r._ptr->_time,
                   r._ptr->_entity_for_entity_id.kv.size(), (unsigned long long)live,
                   (unsigned long long)oracle, oracle == live ? "" : " MISMATCH");
            assert(recomputed == live * (uint64_t)reps);
            assert(oracle == live);
        };
        co_await bench(make_starting_world(), 200, "starting world");
        if (std::getenv("WRY_BENCH"))
            co_await bench(make_starting_world_big(300, 900), 64, "stress world (300 x 300, 900 machines)");
        co_return;
    };

    // `state-hash` replies with the displayed world's content hash, in the
    // Hash literal form, stable across calls and changed by a step.
    define_test("world_state_state_hash") {
        coroutine::Nursery scope;
        GuiContext gc;
        gc.scope = &scope;
        WorldState ws(gc);
        auto reply_hash = [&gc]() -> Hash {
            auto o = gc.issue(command::Origin::console, "state-hash");
            assert(o.reply.status == command::Reply::OK);
            auto node = command::parse_node(o.reply.text);
            assert(node);
            auto h = command::Type<Hash>::read(*node);
            assert(h);
            return *h;
        };
        uint64_t h0 = 0;
        {
            Root<World const*> w;
            // Side effects stay outside assert: the suite also runs in a
            // Release build (the GUI's background test run).
            const bool popped = ws._worlds.try_pop_front(w);
            assert(popped);
            if (!popped)
                co_return;
            h0 = w._ptr->content_hash();
            ws._worlds.push_front(std::move(w));
        }
        assert(reply_hash() == (Hash{h0}));
        assert(reply_hash() == (Hash{h0}));
        // Step the displayed world as update() does.
        {
            Root<World const*> old_world;
            const bool popped = ws._worlds.try_pop_front(old_world);
            assert(popped);
            if (!popped)
                co_return;
            epoch::Epoch pin = pin_global_epoch();
            Root<World*> next = co_await old_world._ptr->step();
            unpin_global_epoch(pin);
            ws._worlds.emplace_back(next);
        }
        assert(reply_hash() != (Hash{h0}));
        co_await ws.end_session(std::chrono::milliseconds(200));
        (void) co_await scope.join();
        co_return;
    };

    // The session log and its replay (multiplayer.md section 5): a hosted
    // game writes its log from the installed World; `replay` re-runs the
    // log from its checkpoint, checking every recorded hash, and leaves
    // the World at the last step replayed, sessionless; a tampered hash
    // is a divergence at that step; `compare` names it; `dump` is the
    // World as text; `host` makes a replayed World a game again; and
    // `corrupt` changes the displayed World off the stream.  update() is
    // not driven here (it repins and forks the step); the backlog is
    // applied as it would.
    define_test("world_state_replay") {
        coroutine::Nursery scope;
        GuiContext gc;
        gc.scope = &scope;
        const std::filesystem::path dir = std::filesystem::temp_directory_path()
            / ("wry-replay-" + std::to_string(getpid()));
        std::filesystem::remove_all(dir);
        {
            WorldState ws(gc);
            using command::Reply;
            assert(ws._host && ws._log && ws._log->path().empty());
            // The session's log in `dir`: the one opened at construction
            // has no file yet
            ws._log_directory = dir;
            ws._open_log(ws._world_to_render._ptr);
            Relay& relay = ws._host->relay();

            auto apply_available = [&]() -> coroutine::Task {
                if (ws._server)
                    for (Step& s : ws._server->take_steps())
                        ws._backlog.push_back(std::move(s));
                while (!ws._backlog.empty()) {
                    Step step = std::move(ws._backlog.front());
                    ws._backlog.pop_front();
                    co_await ws._apply_step_async(step);
                    ws._replay_check(step);
                }
            };
            auto catch_up = [&](uint64_t target) -> coroutine::Task {
                for (int i = 0; i != 2000; ++i) {
                    co_await apply_available();
                    if ((ws._steps_taken >= target) && (ws._steps_taken == relay.clock().head))
                        co_return;
                    co_await cancelable_sleep(0.005);
                }
                assert(!"did not catch up");
            };
            auto displayed = [&]() -> World const* { return ws._world_to_render._ptr; };
            auto tile = [&](Coordinate at) -> Term {
                Term t = term_make_null();
                (void) displayed()->_term_for_coordinate.try_get(at, t);
                return t;
            };
            auto issue = [&](std::string const& line) {
                return gc.issue(command::Origin::console, StringView(line.c_str()));
            };
            auto contains = [](std::string const& s, std::string const& what) {
                return s.find(what) != std::string::npos;
            };
            // A pending command driven to completion, the backlog applied
            // between polls as frames would
            auto run_to_completion = [&](std::string const& line) -> coroutine::Task {
                gc.submit(gc.console_source, StringView(line.c_str()));
                for (int i = 0; (i != 1000) && gc.console_source.pending; ++i) {
                    co_await apply_available();
                    ++gc.frame;
                    gc.service_commands();
                }
                assert(!gc.console_source.pending);
            };
            auto last_reply = [&]() -> std::string {
                return narrow(gc.history.back().reply);
            };

            issue("pause");
            bool paused = co_await eventually([&] { return relay.clock().paused; });
            assert(paused);
            (void) paused;
            co_await catch_up(relay.clock().head);

            // A write rides the stream once our JOIN has; step until it
            // has landed, then a few more
            issue("write Coordinate(4, 4) OPCODE::SWAP");
            for (int i = 0; (i != 50) && !tile(Coordinate{4, 4}).is_opcode(); ++i) {
                relay.step(1);
                co_await catch_up(ws._steps_taken + 1);
            }
            assert(tile(Coordinate{4, 4}).is_opcode());
            relay.step(3);
            co_await catch_up(ws._steps_taken + 3);
            const uint64_t end_time = (uint64_t)displayed()->_time;
            const uint64_t end_hash = displayed()->content_hash();
            assert(end_time >= 4);
            const std::filesystem::path log = ws._log->path();
            assert(!log.empty() && std::filesystem::exists(log));
            {
                auto o = issue("session");
                assert(contains(narrow(o.reply.text), " log " + log.string()));
            }
            co_await ws.end_session(std::chrono::milliseconds(200));
            assert(!ws._log && !ws._server);
            CHECK(issue("write Coordinate(4, 4) OPCODE::SWAP").reply.status == Reply::ERROR);

            // The whole log replays to the same World
            co_await run_to_completion("replay \"" + log.string() + "\"");
            assert(gc.history.back().status == Reply::OK);
            assert(contains(last_reply(), "replayed steps 1 to " + std::to_string(end_time) + ": consistent"));
            assert((uint64_t)displayed()->_time == end_time);
            assert(displayed()->content_hash() == end_hash);
            assert(tile(Coordinate{4, 4}).is_opcode());
            assert(!ws._server && ws._replay && ws._replay->result);
            CHECK(contains(narrow(issue("session").reply.text), "replay of " + log.string() + ": replayed"));

            // ... or to an earlier step
            co_await run_to_completion("replay \"" + log.string() + "\" " + std::to_string(end_time - 2));
            assert(contains(last_reply(), "consistent"));
            assert((uint64_t)displayed()->_time == end_time - 2);

            // A log whose record of one step's hash was tampered with:
            // the replay diverges there and stops, that World displayed
            auto contents = read_session_log(log);
            assert(contents && contents->head == 0 && contents->entries.size() == end_time);
            const uint64_t tampered = end_time - 1;
            contents->entries[(size_t)(tampered - 1)].hash ^= 1;
            const std::filesystem::path forged = dir / "forged.wrylog";
            {
                const std::string bytes = encode_session_log(*contents);
                std::ofstream out(forged, std::ios::binary);
                out.write(bytes.data(), (std::streamsize)bytes.size());
            }
            co_await run_to_completion("replay \"" + forged.string() + "\"");
            assert(gc.history.back().status == Reply::OK);
            assert(contains(last_reply(), "diverged at step " + std::to_string(tampered) + ":"));
            assert((uint64_t)displayed()->_time == tampered);

            // compare: the original against the forgery names the step
            {
                auto o = issue("compare \"" + log.string() + "\" \"" + forged.string() + "\"");
                assert(o.reply.status == Reply::OK);
                assert(contains(narrow(o.reply.text), "streams identical"));
                assert(contains(narrow(o.reply.text), "hashes differ at step " + std::to_string(tampered)));
            }
            // Not a log: an error at once; a step the log does not hold too
            CHECK(issue("replay \"" + (dir / "absent.wrylog").string() + "\"").reply.status == Reply::ERROR);
            CHECK(issue("replay \"" + log.string() + "\" " + std::to_string(end_time + 5)).reply.status == Reply::ERROR);

            // dump: the displayed World as text, the same twice
            {
                auto a = issue("dump");
                auto b = issue("dump");
                assert(a.reply.status == Reply::OK);
                assert(narrow(a.reply.text) == narrow(b.reply.text));
                assert(contains(narrow(a.reply.text), "Coordinate(4, 4): OPCODE::SWAP"));
                const std::filesystem::path file = dir / "dump.txt";
                auto c = issue("dump \"" + file.string() + "\"");
                assert(c.reply.status == Reply::OK && std::filesystem::exists(file));
            }

            // A replayed World becomes a game: host starts a session from
            // it, with a new log
            {
                auto o = issue("host 0");
                assert(o.reply.status == Reply::OK);
                assert(ws._host && ws._log && !ws._replay);
                assert((uint64_t)displayed()->_time == tampered);
            }
            // corrupt: the displayed World changes, off the stream
            {
                const uint64_t before = displayed()->content_hash();
                CHECK(issue("corrupt Coordinate(9, 9) 1").reply.status == Reply::OK);
                assert(displayed()->content_hash() != before);
                CHECK(narrow(issue("get Coordinate(9, 9)").reply.text) == "1");
            }
            co_await ws.end_session(std::chrono::milliseconds(200));
        }
        (void) co_await scope.join();
        std::filesystem::remove_all(dir);
        co_return;
    };

    // A join that never gets its save (a dead address) must not leave the
    // next session unable to apply a step: ending the session clears the
    // awaiting-save hold and any donation outstanding.
    // The session commands (commands.md section 7): at the main menu (no
    // world) new-game and load build a WorldState and hand it to the host
    // (GuiContext::on_world, stood in for here); in a game they replace
    // the world in place; save replies with the new save's name once it
    // is written, list-saves names it, and load restores it; main-menu
    // asks the host to drop the world.  In-place replacement ends the
    // session, blocking, so the test ends it first by awaiting (a test
    // never blocks a worker).  The save it makes is deleted at the end
    define_test("world_state_session_commands") {
        using command::Origin;
        using command::Reply;
        coroutine::Nursery scope;
        GuiContext gc;
        gc.scope = &scope;
        std::shared_ptr<WorldState> shown;
        bool to_menu = false;
        gc.on_world = [&](std::shared_ptr<WorldState> w) { shown = std::move(w); };
        gc.on_main_menu = [&] { to_menu = true; };
        auto issue = [&](const char* line) { return gc.issue(Origin::console, StringView(line)); };
        // Issued outside assert: the suite also runs in a Release build
        auto expect = [&](const char* line, Reply::Status want) {
            [[maybe_unused]] const Reply::Status got = issue(line).reply.status;
            assert(got == want);
        };
        auto expect_load = [&](String const& name) {
            [[maybe_unused]] const Reply::Status got = gc.issue_with(Origin::console, "load", name).reply.status;
            assert(got == Reply::OK);
        };
        auto displayed_hash = [&] { return narrow(issue("state-hash").reply.text); };

        // The main menu: nothing to save or leave; a save must exist
        expect("save", Reply::ERROR);
        expect("main-menu", Reply::ERROR);
        expect("load \"no-such-save.wry\"", Reply::ERROR);
        expect("list-saves", Reply::OK);
        assert(!shown && !gc.world);

        // new-game from the menu: the host is handed the world to show
        expect("new-game", Reply::OK);
        assert(shown && gc.world == shown.get());
        WorldState* ws = shown.get();
        [[maybe_unused]] const std::string start = displayed_hash();

        // An edit off the stream, then saved: the reply is the save's name,
        // once it is written
        expect("corrupt Coordinate(5, 5) OPCODE::SWAP", Reply::OK);
        [[maybe_unused]] const std::string edited = displayed_hash();
        assert(edited != start);
        expect("save", Reply::PENDING);
        const uint64_t save_seq = gc.history.back().seq;
        auto save_entry = [&]() -> command::HistoryEntry const* {
            for (auto const& e : gc.history)
                if (e.seq == save_seq)
                    return &e;
            return nullptr;
        };
        for (const auto deadline = testing::soon(); save_entry()->status == Reply::PENDING;) {
            assert(std::chrono::steady_clock::now() < deadline);
            ++gc.frame;
            gc.service_commands();
            co_await testing::pause(std::chrono::milliseconds(1));
        }
        assert(save_entry()->status == Reply::OK);
        const String name = save_entry()->reply;
        const std::optional<int> id = save_named(name);
        assert(id);
        if (!id)
            co_return;
        {
            [[maybe_unused]] const std::string listed = "\n" + narrow(issue("list-saves").reply.text) + "\n";
            assert(listed.find("\n" + narrow(name) + "\n") != std::string::npos);
        }

        // new-game and load in place: the same WorldState, its world
        // replaced, the session ended and hosted afresh
        co_await ws->end_session(std::chrono::milliseconds(200));
        expect("new-game", Reply::OK);
        assert(gc.world == ws && ws->_host);
        [[maybe_unused]] const std::string renewed = displayed_hash();
        assert(renewed == start);
        co_await ws->end_session(std::chrono::milliseconds(200));
        expect_load(name);
        assert(gc.world == ws && ws->_host);
        [[maybe_unused]] const std::string reloaded = displayed_hash();
        assert(reloaded == edited);

        // main-menu: the host is asked to drop the world; doing so leaves
        // no world for the commands, and load from the menu builds one
        // from the save
        expect("main-menu", Reply::OK);
        assert(to_menu);
        co_await ws->end_session(std::chrono::milliseconds(200));
        shown.reset();
        assert(!gc.world);
        expect_load(name);
        assert(shown && gc.world == shown.get() && shown->_host);
        [[maybe_unused]] const std::string menu_loaded = displayed_hash();
        assert(menu_loaded == edited);

        co_await shown->end_session(std::chrono::milliseconds(200));
        shown.reset();
        delete_game(*id);
        assert(!save_named(name));
        (void) co_await scope.join();
        co_return;
    };

    define_test("world_state_abandoned_join") {
        coroutine::Nursery scope;
        GuiContext gc;
        gc.scope = &scope;
        {
            WorldState ws(gc);
            assert(ws._host);
            ws.join(String("127.0.0.1"), 1);            // nothing listens on port 1
            assert(ws._remote && ws._awaiting_save);
            bool refused = co_await eventually([&] {
                (void) ws._server->take_steps();
                return ws._remote->state() == RemoteServer::State::DISCONNECTED;
            });
            assert(refused);
            (void) refused;
            assert(ws._awaiting_save);                  // the save never came
            ws._donations.push_back({7, 7});            // and suppose one was pending
            co_await ws.end_session(std::chrono::milliseconds(200));
            assert(!ws._awaiting_save && ws._donations.empty());

            // A new game after it runs: its steps are applied
            ws.new_game();
            assert(ws._host && !ws._awaiting_save);
            Relay& relay = ws._host->relay();
            relay.pause();
            bool paused = co_await eventually([&] { return relay.clock().paused; });
            assert(paused);
            (void) paused;
            relay.step(2);
            bool applied = co_await eventually([&] {
                for (Step& s : ws._server->take_steps())
                    ws._backlog.push_back(std::move(s));
                return ws._backlog.size() + ws._steps_taken >= 3 && relay.clock().head >= 3;
            });
            assert(applied);
            (void) applied;
            while (!ws._backlog.empty()) {
                Step step = std::move(ws._backlog.front());
                ws._backlog.pop_front();
                co_await ws._apply_step_async(step);
            }
            assert(ws._steps_taken >= 3);
            co_await ws.end_session(std::chrono::milliseconds(200));
        }
        (void) co_await scope.join();
        co_return;
    };

} // namespace wry


