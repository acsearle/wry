//
//  world_state.cpp
//  client
//
//  Created by Antony Searle on 8/7/2023.
//

#include "world_state.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <random>

#include "base36.hpp"
#include "coroutine.hpp"
#include "ctype.hpp"
#include "garbage_collected.hpp"
#include "matter.hpp"
#include "save.hpp"

namespace wry {

    namespace {

        // The starting scenario.  Extracted verbatim from the original model
        // constructor so NEW, restart, and the round-trip tests can all build
        // the same world.  The player is just one of the entities; callers
        // recover the local-player handle via find_local_player below.
        //
        // TEMP: unreferenced while new_game uses make_starting_world_big.
        [[maybe_unused]] World* make_starting_world() {
            World* world = new World;

            {
                Player* p = new Player;
                world->_entity_for_entity_id.set(p->_entity_id, p);
                world->_waiting_on_time.set({Time{0}, p->_entity_id});
            }

            auto insert_localized_entity = [&](LocalizedEntity const* entity_ptr) {
                EntityID entity_id = entity_ptr->_entity_id;
                world->_entity_for_entity_id.set(entity_id, entity_ptr);
                world->_waiting_on_time.set({Time{0}, entity_id});
            };

            {
                // new machine spawner at origin
                Spawner* p = new Spawner;
                p->_location = Coordinate{0, 0};
                insert_localized_entity(p);
            }

            {
                // value source
                Source* q = new Source;
                q->_location = Coordinate{2, 2};
                q->_of_this = Term(1);
                insert_localized_entity(q);
            }

            {
                // value sink
                Sink* r = new Sink;
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

            return world;
        }

        // TEMP: stress-test scenario.  Machines with random headings,
        // randomly part-way through a move between adjacent tiles, scattered
        // over a square region centered on the origin whose tiles each have
        // a 10% chance of holding the flip-flop opcode.  Fixed seed, and
        // modulo rather than uniform_int_distribution (whose mapping is
        // implementation-defined), so every run and every peer builds the
        // identical world.
        World* make_starting_world_big() {
            World* world = new World;

            {
                Player* p = new Player;
                world->_entity_for_entity_id.set(p->_entity_id, p);
                world->_waiting_on_time.set({Time{0}, p->_entity_id});
            }

            std::mt19937_64 gen{20260709};

            constexpr i32 region_extent = 3000;     // [-1500, 1500) each axis
            constexpr i32 half = region_extent / 2;
            constexpr int machine_count = 100000;

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
                machine->_phase = Machine::PHASE_TRAVELLING;
                machine->_old_heading = heading;
                machine->_new_heading = heading;
                machine->_old_location = xy;
                machine->_new_location = destination;
                machine->_old_time = arrival - travel_ticks;
                machine->_new_time = arrival;
                world->_entity_for_entity_id.set(machine->_entity_id, machine);
                world->_entity_id_for_coordinate.set(xy, machine->_entity_id);
                world->_entity_id_for_coordinate.set(destination, machine->_entity_id);
                world->_waiting_on_time.set({arrival, machine->_entity_id});
                ++placed;
            }

            return world;
        }

        // The displayed world owns a single Player; the model borrows a handle
        // to it for input routing.  The entity map holds entities as const
        // Entity*; the const here reflects that storage, not the Player (whose
        // input _queue is a mutable member).
        Player const* find_local_player(World const* world) {
            Player const* found = nullptr;
            world->_entity_for_entity_id.kv.for_each(
                [&found](EntityID, Entity const* e) {
                    if (Player const* p = dynamic_cast<Player const*>(e))
                        found = p;
                });
            return found;
        }

    } // anonymous namespace

    // Swap in `world` as the only entry in _worlds, re-pointing the local
    // player.  Single-threaded at the call sites (main thread, between pump
    // and render), so the brief drain is unobservable to the renderer.
    static void install_displayed_world(WorldState& m, World* world) {
        m._local_player = find_local_player(world);
        Root<World const*> discard;
        while (m._worlds.try_pop_front(discard)) { }
        m._worlds.emplace_back(world);
    }

    void WorldState::new_game() {
        install_displayed_world(*this, make_starting_world_big());
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
        save_game_async(w, [this](bool ok) {
            _gui.post_notification(ok ? "Saved." : "Save failed.");
        });
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
        // texture.
        
        // :todo: what if the viewport is odd-sized?
        // :todo:
        
        simd_float4x4 C = simd_matrix_scale(_gui.viewport_size.x / 2048.0f,
                                            _gui.viewport_size.y / 2048.0f,
                                            1.0f);
        
        
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

        // Apply this step's authoritative commands (from the Server) to the
        // players' queues before stepping.  World::step() drains the queues
        // exactly as before -- the Server is just who fills them.  Single
        // player: the local player's own actions, round-tripped through
        // LocalServer (the one-frame submit->apply latency, as before).
        for (auto&& cmd : _server->poll())
            _local_player->_queue.push_back(std::move(cmd.action));

        // Advance the displayed world one simulation step: pop it, step it,
        // push the result back, and stash it in _world_to_render for the
        // renderer to draw this same frame.
        Root<World const*> old_world;
        (void) _worlds.try_pop_front(old_world);
        assert(old_world);
        Coroutine::Nursery nursery;
        nursery.soon(_world_to_render, old_world->step());
        sync_wait(nursery.join());
        _worlds.emplace_back(_world_to_render);
        assert(_world_to_render);

        // Turn this frame's input into commands (this also projects the
        // cursor).  After the step, so a submission applies next frame.
        submit_local_commands();
    }

    void WorldState::submit_local_commands() {
        using namespace ::simd;

        // Cursor -> ground plane (the projection the renderer also reads).
        auto lookat = matrix_identity_float4x4;
        lookat.columns[3].x += _looking_at.x / 1024.0f;
        lookat.columns[3].y -= _looking_at.y / 1024.0f;
        simd_float4x4 A = simd_mul(_uniforms.viewprojection_transform, lookat);
        simd_float4 b = make<float4>(_mouse, 0.0f, 1.0f);
        _mouse4 = make<float4>(project_screen_ray(A, b), 0.0f, 1.0f);

        // Click: write the held value at the tile under the cursor.  (The
        // palette claims palette-area clicks before _outstanding_click is set,
        // so anything here is for the world tile.)
        if (_outstanding_click) {
            int i = round(_mouse4.x);
            int j = round(_mouse4.y);
            Coordinate xy{i, j};
            Player::Action a;
            a.tag = Player::Action::WRITE_VALUE_FOR_COORDINATE;
            a.coordinate = xy;
            a.value = _holding_value;
            _server->submit(std::move(a));
            printf(" Clicked world (%d, %d)\n", i, j);
            _outstanding_click = false;
        }

        // Hex keys: write the digit at the tile under the cursor.
        while (!_outstanding_keysdown.empty()) {
            char32_t ch = _outstanding_keysdown.front_and_pop_front();
            if (wry::isascii((int) ch) && isxdigit(ch)) {
                int64_t k = wry::base36::from_base36_table[ch];
                int i = round(_mouse4.x);
                int j = round(_mouse4.y);
                Coordinate xy{i, j};
                Player::Action a;
                a.tag = Player::Action::WRITE_VALUE_FOR_COORDINATE;
                a.coordinate = xy;
                a.value = k;
                _server->submit(std::move(a));
            }
        }
    }

    // Whatever the overlay stack didn't claim: the world's ground-plane click,
    // scroll-pan, ESC-opens-the-in-game-menu, hex-key writes, and debug toggles.
    void WorldState::pump_legacy_event(gui::Event const& e) {
        using namespace ::wry::gui;

        switch (e.kind) {

            case WryEventKindMouseUp:
                if (e.button == MouseButton::Left)
                    _outstanding_click = true;
                break;

            case WryEventKindScroll:
                _looking_at.x += e.scroll_delta.x;
                _looking_at.y += e.scroll_delta.y;
                break;

            case WryEventKindKeyDown: {
                char buffer[100];
                switch (e.key) {
                    case key::Escape:
                        _stack.push(&_main_menu_overlay);
                        break;
                    case 'j':
                        _show_jacobian = !_show_jacobian;
                        std::snprintf(buffer, sizeof(buffer), "%s [J]acobians",
                                      _show_jacobian ? "Show" : "Hide");
                        _gui.append_log(buffer);
                        break;
                    case 'p':
                        _show_points = !_show_points;
                        std::snprintf(buffer, sizeof(buffer), "%s [P]oints",
                                      _show_points ? "Show" : "Hide");
                        _gui.append_log(buffer);
                        break;
                    case 'w':
                        _show_wireframe = !_show_wireframe;
                        std::snprintf(buffer, sizeof(buffer), "%s [W]ireframe",
                                      _show_wireframe ? "Show" : "Hide");
                        _gui.append_log(buffer);
                        break;
                    default:
                        if ((e.key >= '0' && e.key <= '9') ||
                            (e.key >= 'a' && e.key <= 'f')) {
                            _outstanding_keysdown.push_back((char32_t)e.key);
                        }
                        break;
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
                 e.kind == WryEventKindScroll);

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
            if (!_gui.overlays.dispatch(e))
                if (!_stack.dispatch(e))
                    pump_legacy_event(e);
        }
    }

} // namespace wry
