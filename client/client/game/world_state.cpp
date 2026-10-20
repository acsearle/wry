//
//  world_state.cpp
//  client
//
//  Created by Antony Searle on 8/7/2023.
//

#include "world_state.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

#include "base36.hpp"
#include "command_types.hpp"
#include "coroutine.hpp"
#include "ctype.hpp"
#include "garbage_collected.hpp"
#include "matter.hpp"
#include "save.hpp"
#include "save_format.hpp"
#include "terrain.hpp"
#include "test.hpp"

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

            world->_term_for_coordinate.set(Coordinate{-2, -2}, term_make_integer_with(7));
            world->_term_for_coordinate.set(Coordinate{0, +1}, term_make_integer_with(1));
            world->_term_for_coordinate.set(Coordinate{0, +2}, term_make_integer_with(2));
            world->_term_for_coordinate.set(Coordinate{0, +3}, term_make_integer_with(3));
            world->_term_for_coordinate.set(Coordinate{0, +4}, term_make_opcode(OPCODE_FLIP_FLOP));
            world->_term_for_coordinate.set(Coordinate{0, +5}, term_make_integer_with(5));

            // matter: shipping containers, on the ground where the demo
            // machines will drive over (through) them
            world->_term_for_coordinate.set(Coordinate{-2, +2}, term_make_matter(MATTER_SHIPPING_CONTAINER));
            world->_term_for_coordinate.set(Coordinate{-3, +4}, term_make_matter(MATTER_SHIPPING_CONTAINER));

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
        m._server->request_join();
    }

    void WorldState::new_game() {
        install_displayed_world(*this, make_starting_world());
    }

    void WorldState::load_from_save(int id) {
        install_displayed_world(*this, load_game(id));
    }

    void WorldState::save_current() {
        // Hand a rooted snapshot of the displayed world to the async saver and
        // return to the frame immediately; the save no longer blocks the main
        // thread.  The world is an immutable persistent structure that
        // World::step() never mutates (it builds a fresh World), so the
        // background walk reads a stable snapshot even as play continues, and
        // the Root keeps it (and everything reachable) alive until the save
        // finishes.
        Root<World const*> w;
        if (!_worlds.try_pop_front(w))
            return;
        // The callback runs on a worker thread when the save finishes; post the
        // result for the main-thread pump to surface in the log.
        assert(_gui.scope);
        _gui.scope->soon(save_game(w, [this](bool ok) {
            _gui.post_notification(ok ? "Saved." : "Save failed.");
        }));
        _worlds.push_front(std::move(w));
    }


    void WorldState::_regenerate_uniforms() {
        
        // camera setup
        
        // rotate eye location to Z axis
        assert(_uniforms.camera_position_world.w == 1);
        float3 p = _uniforms.camera_position_world.xyz;
        quatf q(simd_normalize(p), simd_make_float3(0, 0, 1));
        float4x4 V = simd_matrix_translate(0, 0, -simd_length(p)) *
                              float4x4(q);
        
        simd_float4x4 F = simd_mul(matrix_perspective_right_hand(M_PI_2, 1, 5, 50),
                                   simd_matrix_scale(1, 1, -1, 1));
        
        float aspect_ratio = _gui.viewport_size.x / _gui.viewport_size.y;
        simd_float4x4 P = simd_mul(simd_matrix_scale(2, 2 * aspect_ratio, 1),
                                   F);
        
        _uniforms.view_transform = V;
        _uniforms.viewprojection_transform = simd_mul(P, V);
        
        _uniforms.inverse_view_transform = simd_inverse(_uniforms.view_transform);
        _uniforms.inverse_viewprojection_transform = simd_inverse(_uniforms.viewprojection_transform);

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

        // Paused (the `pause` command), the world steps only as `step`
        // budgets allow -- except that a world never yet stepped steps
        // once, so the renderer always has a world to draw.  Frames go on
        // regardless.
        if (!_world_to_render || _take_step_permission()) {
            // This step's authoritative commands (from the Server) are the
            // step's input: the server events and every client's actions,
            // in the server's order.  Single player: the local client's own
            // actions, round-tripped through LocalServer (the one-frame
            // submit->apply latency, as before).  They live here until the
            // step has consumed them.
            std::vector<Command> commands = _server->poll();

            // Advance the displayed world one simulation step: pop it, step it,
            // push the result back, and stash it in _world_to_render for the
            // renderer to draw this same frame.
            Root<World const*> old_world;
            (void) _worlds.try_pop_front(old_world);
            assert(old_world);
            coroutine::Nursery nursery;
            nursery.soon(_world_to_render, old_world->step(commands));
            sync_wait(nursery.join());
            _worlds.emplace_back(_world_to_render);
            assert(_world_to_render);

            ++_steps_taken;
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

    bool WorldState::_take_step_permission() {
        if (!_paused)
            return true;
        if (_step_budget == 0)
            return false;
        --_step_budget;
        return true;
    }

    simd_float4 WorldState::cursor_on_ground() const {
        using namespace ::simd;
        auto lookat = matrix_identity_float4x4;
        lookat.columns[3].x += _looking_at.x / 1024.0f;
        lookat.columns[3].y -= _looking_at.y / 1024.0f;
        simd_float4x4 A = simd_mul(_uniforms.viewprojection_transform, lookat);
        simd_float4 b = make<float4>(_mouse, 0.0f, 1.0f);
        return make<float4>(project_screen_ray(A, b), 0.0f, 1.0f);
    }

    void WorldState::project_cursor() {
        _mouse4 = cursor_on_ground();
    }

    std::optional<Coordinate> WorldState::cursor_tile() const {
        simd_float4 g = cursor_on_ground();
        const float x = std::round(g.x);
        const float y = std::round(g.y);
        // The comparisons are false for NaN.  2^31 is exactly
        // representable; anything at or beyond it would overflow i32.
        constexpr float limit = 2147483648.0f;
        if (!(x >= -limit && x < limit && y >= -limit && y < limit))
            return std::nullopt;
        return Coordinate{(i32)x, (i32)y};
    }

    // Empty the hand: the palette forgets its selection and the renderer's
    // cursor refresh puts the platform arrow back.
    void WorldState::drop_hand() {
        if (!is_holding())
            return;
        char buffer[100];
        const Term held = _holding_value._value;
        std::snprintf(buffer, sizeof(buffer), "Dropped %s",
                      held.is_opcode()
                          ? name_from_OPCODE((OPCODE) held.as_opcode()) + 7
                          : "value");
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
            std::snprintf(buffer, sizeof(buffer), "Picked up %s",
                          name_from_OPCODE((OPCODE) t.as_opcode()) + 7);
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
            std::snprintf(buffer, sizeof(buffer), "Holding %s",
                          name_from_OPCODE(reoriented) + 7);
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

        void submit_write(WorldState& ws, Coordinate at, Term term) {
            ws._server->submit(make_write_action(at, term));
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

            // Take back the budget this step has not yet spent, so a
            // cancelled `step 1000` leaves the world paused, not running.
            void cancel() override {
                if (gui.world != ws || ws->_steps_taken >= target)
                    return;
                const uint64_t unspent = target - ws->_steps_taken;
                ws->_step_budget -= std::min(ws->_step_budget, unspent);
            }
        };

    } // namespace

    void register_world_commands(CommandRegistry& registry) {

        // The clock (commands.md section 8).  Today the world steps once
        // per frame; these decouple the step clock from the frame clock as
        // far as tests and scripts need.  Under a networked server they
        // are the same verbs addressed to the local server instance.
        registry.add(make_command<Ctx>(
            "pause", "Stop stepping the world (frames continue)", Category::clock,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                ws._paused = true;
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "resume", "Step the world every frame again", Category::clock,
            std::tuple<>{},
            with_world([](WorldState& ws) {
                ws._paused = false;
                ws._step_budget = 0;
                return Reply::ok();
            })));

        registry.add(make_command<Ctx>(
            "step", "Advance the world n steps (one per frame), then reply", Category::clock,
            std::tuple{
                Param<Ctx, int64_t>{"n", "how many steps", int64_t{1}},
            },
            [](Ctx& c, int64_t n) {
                if (!c.world)
                    return Reply::error(String("no world is loaded"));
                if (n < 0)
                    return Reply::error(String("n must be non-negative"));
                WorldState& ws = *c.world;
                if (ws._paused)
                    ws._step_budget += (uint64_t)n;
                return Reply::later(std::make_shared<StepPending>(
                    c.gui, &ws, ws._steps_taken + (uint64_t)n));
            }));

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
                ws._show_map = !ws._show_map;
                return Reply::ok();
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

        // World edits: built into a Player::Action and submitted; the
        // Server orders them and they apply when they come back in a step.
        registry.add(make_command<Ctx>(
            "write", "Write a Term on a tile", Category::world,
            std::tuple{
                cursor_param("at", "the tile to write"),
                Param<Ctx, Term>{"term", "what to write"},
            },
            with_world([](WorldState& ws, Coordinate const& at, Term const& term) {
                submit_write(ws, at, term);
                return Reply::ok();
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
                submit_write(ws, at, term_make_null());
                return Reply::ok();
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
                if (_show_map)
                    break;
                // A click off the ground edits nothing.
                if (auto tile = cursor_tile()) {
                    if (e.button == MouseButton::Left) {
                        if (is_holding())
                            (void) _gui.issue_with(command::Origin::gui, "write",
                                                   *tile, _holding_value._value);
                    } else if (e.button == MouseButton::Right) {
                        (void) _gui.issue_with(command::Origin::gui, "erase", *tile);
                    }
                }
                break;

            case WryEventKindScroll: {
                // The map is drawn at 1/_map_zoom scale, so a map-mode
                // swipe traverses _map_zoom times the world distance for
                // the same on-screen travel.  _looking_at is the one shared
                // focus: leaving map mode keeps you looking at the spot you
                // panned to.
                float shuttle = _show_map ? _map_zoom : 1.0f;
                _looking_at.x += e.scroll_delta.x * shuttle;
                _looking_at.y += e.scroll_delta.y * shuttle;
                break;
            }

            case WryEventKindMagnify: {
                // Trackpad pinch zooms the map (map mode only; the world
                // view has no zoom).  Anchored at the cursor: the map point
                // under the fingers stays put, like every maps app.
                //
                // The map quad is drawn with model transform
                //     p_view = (p_world + t) / Z,
                // t = (+_looking_at.x, -_looking_at.y) / 1024.  Holding the
                // cursor's world point q fixed on screen across Z -> Z'
                // needs (q + t) / Z == (q + t') / Z', so
                //     t' = (q + t) * (Z' / Z) - q.
                if (!_show_map)
                    break;
                constexpr float MAP_ZOOM_MIN = 8.0f;    // texels get blocky
                constexpr float MAP_ZOOM_MAX = 256.0f;  // whole map, small
                float factor = 1.0f + e.magnification;
                if (factor < 0.5f) factor = 0.5f;   // one event is a nudge
                if (factor > 2.0f) factor = 2.0f;
                float old_zoom = _map_zoom;
                _map_zoom = std::clamp(_map_zoom / factor,
                                       MAP_ZOOM_MIN, MAP_ZOOM_MAX);
                if (_map_zoom == old_zoom)
                    break;

                // q: the cursor's ground-plane point under the CURRENT map
                // transform (same projection cursor_on_ground does for
                // the world, with the map's model matrix).
                auto lookat = matrix_identity_float4x4;
                lookat.columns[3].x += _looking_at.x / 1024.0f;
                lookat.columns[3].y -= _looking_at.y / 1024.0f;
                simd_float4x4 model = simd_mul(simd_matrix_scale(1.0f / old_zoom),
                                               lookat);
                simd_float4x4 A = simd_mul(_uniforms.viewprojection_transform,
                                           model);
                simd_float4 b = simd_make_float4(_mouse.x, _mouse.y, 0.0f, 1.0f);
                float2 q = project_screen_ray(A, b);

                float s = _map_zoom / old_zoom;
                float tx = +_looking_at.x / 1024.0f;
                float ty = -_looking_at.y / 1024.0f;
                _looking_at.x = +((q.x + tx) * s - q.x) * 1024.0f;
                _looking_at.y = -((q.y + ty) * s - q.y) * 1024.0f;
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
                if (!_show_map &&
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
            bool stack_active = !_show_map || (_stack.size() > 1);
            if (!_gui.overlays.dispatch(e))
                if (!(stack_active && _stack.dispatch(e)))
                    pump_legacy_event(e);
        }
    }

    // ----------------------------------------------------------------
    // Tests.

    // The pump's contract for the hand and the erase gesture: an
    // empty-hand left click places nothing, a right click issues an erase
    // (a null write, which erases the cell's key), a full-hand left click
    // issues a write of what is held, map mode edits nothing, and Escape
    // drops a full hand before it opens the menu.  Gestures are commands,
    // so the history records them.  The server round trip is covered
    // elsewhere (player_null_write_erases_key).
    define_test("world_state_hand_and_erase_input") {
        GuiContext gc;

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

        // Map mode: neither button edits.
        ws._show_map = true;
        issued = gc.history.size();
        ws.pump_legacy_event(mouse_up(gui::MouseButton::Left));
        ws.pump_legacy_event(mouse_up(gui::MouseButton::Right));
        assert(gc.history.size() == issued);
        ws._show_map = false;

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

        co_return;
    };

    // Pipette: with an empty hand Q copies the tile under the cursor into
    // the hand, matter excepted; with a full hand Q drops it.  Sampling
    // reads the displayed world -- here the starting world laid out by
    // make_starting_world, before any step.
    define_test("world_state_pipette") {
        GuiContext gc;
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

        co_return;
    };

    // The clock commands: pause stops stepping; step n grants n steps of
    // budget and replies once they are taken; the console's next line
    // waits behind it; cancelling returns unspent budget.  update() itself
    // is not driven here (it repins and forks the step); its gate is.
    define_test("world_state_clock_commands") {
        GuiContext gc;
        WorldState ws(gc);
        using command::Reply;

        assert(ws._take_step_permission());     // running: always
        gc.issue(command::Origin::console, "pause");
        assert(ws._paused && !ws._take_step_permission());

        gc.submit(gc.console_source, "step 2");
        gc.submit(gc.console_source, "help step");
        assert(ws._step_budget == 2);
        const uint64_t step_seq = gc.history.back().seq;
        assert(gc.history.back().status == Reply::PENDING);
        assert(gc.console_source.queued.size() == 1);   // help waits

        // Take the steps update() would take.
        int steps = 0;
        while (ws._take_step_permission()) {
            ++ws._steps_taken;
            ++steps;
        }
        assert(steps == 2 && ws._step_budget == 0);

        ++gc.frame;
        gc.service_commands();
        assert(!gc.console_source.pending);
        assert(gc.history.back().line == StringView("help \"step\""));
        for (auto const& e : gc.history)
            if (e.seq == step_seq)
                assert(e.status == Reply::OK && e.completed_frame == gc.frame);

        // Cancel: a step 5 that took one step gives back four.
        gc.submit(gc.console_source, "step 5");
        assert(ws._take_step_permission());
        ++ws._steps_taken;
        assert(ws._step_budget == 4);
        gc.cancel(gc.console_source);
        assert(ws._step_budget == 0);
        assert(gc.history.back().status == Reply::CANCELLED);

        // Resume: stepping every frame again, no stale budget.
        gc.issue(command::Origin::console, "resume");
        assert(!ws._paused && ws._take_step_permission());

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
        GuiContext gc;
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
        co_return;
    };

} // namespace wry
