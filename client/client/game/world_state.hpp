//
//  world_state.hpp
//  client
//
//  Created by Antony Searle on 8/7/2023.
//

#ifndef world_state_hpp
#define world_state_hpp

#include "ShaderTypes.h"

#include <chrono>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <map>
#include <vector>

#include "concurrent_queue.hpp"
#include "contiguous_deque.hpp"
#include "entity.hpp"
#include "gui_context.hpp"
#include "gui_event.hpp"
#include "gui_keymap.hpp"
#include "gui_overlay.hpp"
#include "hash.hpp"
#include "machine.hpp"
#include "palette.hpp"
#include "simd.hpp"
#include "sim.hpp"
#include "spawner.hpp"
#include "string.hpp"
#include "world.hpp"
#include "world_map.hpp"
#include "player.hpp"
#include "server.hpp"
#include "host.hpp"

namespace wry {
    
    struct WorldState {
        
        // The model holds all the app state, including the World, but
        // also the visualization-only parts of the app state
        
        // We'll try to keep the model / WryRenderer distinction to roughly be
        // the platform-independent / platform-specific code dividing line,
        // though things like simd_ make this ambiguous
        
        // TODO: This is not some singular world state; it is specifcally the
        // world state for display to the user (and against which user inputs
        // should be interpreted).  Other states will be be present; old states
        // being serialized to save game or initialize a multiplayer peer; and
        // new states that have been computed but not yet displayed
        
        // Borrowed, host-owned generic GUI state (events, viewport, log /
        // console, notifications).  Set in the constructor; see gui_context.hpp.
        GuiContext& _gui;

        // simulation state
        // World* _world;
        // BlockingDeque<World const*> _worlds;
        BlockingDeque<Root<World const*>> _worlds;



        // debug toggles ('j' / 'p' / 'w')
        bool _show_jacobian = false;
        bool _show_points = false;
        bool _show_wireframe = false;


        // user interface state

        // (_events, the floating log and the console moved to GuiContext / _gui.)

        // Persistent GUI elements.  Each owns its own state (console lines,
        // floating log entries, palette selection / hover); the stack
        // borrows pointers to them and runs the dispatch + paint walks.
        // The main menu and save-list both live here so their lifetimes
        // span push/pop cycles -- the stack only borrows pointers, it
        // doesn't own them.
        gui::PaletteOverlay _palette_overlay;
        gui::MainMenuOverlay _main_menu_overlay;
        gui::SaveListOverlay _save_list_overlay;
        gui::OverlayStack _stack;

        // Local player's session view / input state.
        Root<Term> _holding_value = {};   // the opcode in hand (palette writes it)
        float2 _mouse = {};               // NDC cursor (palette reads it)
        float2 _looking_at = {};          // scroll-pan accumulator
        simd_float4 _mouse4 = {};         // cursor projected onto the ground plane

        // Map mode (TAB): the renderer draws the world-map quad instead of
        // the world, the palette hides, scroll pans at the map scale, and
        // world edits are suppressed.  The minimap shows whenever this is
        // off.
        bool _show_map = false;

        // Map-mode zoom: how many world units span one normal-view unit
        // (64 = the canonical map scale; pinch varies it, clamped).  Kept
        // across TAB toggles.  Scroll-pan speed follows it so a swipe
        // always traverses what it visually crosses.
        float _map_zoom = 64.0f;

        // World-map pipeline: ~1 Hz background rebuild of the
        // one-pixel-per-tile map image, double-buffered into textures on
        // the render side.  shared_ptr so an in-flight build outlives us.
        std::shared_ptr<WorldMapHandoff> _map_handoff =
            std::make_shared<WorldMapHandoff>();
        std::chrono::steady_clock::time_point _map_last_start{};
        
        // visualization state
        
        
        // Camera and sun projections

        // (_viewport_size moved to GuiContext / _gui.)

        MeshUniforms _uniforms;

        // Shadow map footprint in texels.  The renderer owns the texture and
        // mirrors its actual size here whenever it (re)allocates it
        // (WryWorldScene -drawableResize:, which then reruns
        // _regenerate_uniforms); _regenerate_uniforms uses it to fit the
        // screen rect into the map.  The default only covers the
        // constructor's regenerate call, before any renderer exists.
        float2 _shadow_map_size = { 2048.0f, 2048.0f };

        // The world advanced one step by update(), read by the renderer the
        // same frame (the _worlds deque stays the source of truth).
        Root<World*> _world_to_render;

        // Steps this WorldState has applied: what `step n` waits on
        uint64_t _steps_taken = 0;

        // Finalized steps taken from the server and not yet applied: a
        // frame applies at most max_steps_per_frame, so catching up (a
        // join replaying the stream) keeps the GUI live
        std::deque<Step> _backlog;
        static constexpr size_t max_steps_per_frame = 64;

        // The session (multiplayer.md section 2): a Host -- the relay and
        // this client over a socket pair -- when playing alone or hosting
        // others, or a bare RemoteServer when joined into another's game.
        // _server is the seam in either case: input is submitted there and
        // update() takes the finalized steps back as the step's input.
        // This client has no Player until the relay's JOIN rides a step
        // (requested whenever a world is installed).
        std::unique_ptr<Host> _host;
        std::unique_ptr<RemoteServer> _remote;
        Server* _server = nullptr;
        RemoteServer::State _logged_state = RemoteServer::State::CONNECTING;

        // A joiner holds its stream until the donated save has landed
        // (multiplayer.md section 6): the displayed world is a placeholder
        // meanwhile.  A host's client serves the relay's donation requests
        // once its World is at the requested boundary
        bool _awaiting_save = false;
        std::vector<RemoteServer::Donation> _donations;
        void _serve_donations();

        RemoteServer* _client();
        void _start_session();
        void _start_hosting(uint64_t head);
        void _start_joining(String const& address, uint16_t port);
        void _end_session(std::chrono::milliseconds grace);
        void _apply_step(Step const& step);
        [[nodiscard]] coroutine::Task _apply_step_async(Step const& step);

        // The session commands (commands.md section 7): listen for players
        // (the bound port), join another's game at address:port (the
        // scenario afresh, its stream replayed), or leave it and go on
        // alone from here
        std::expected<uint16_t, String> host_listen(uint16_t port);
        void join(String const& address, uint16_t port);
        void leave();

        // Whether finalized steps or the connection's news are waiting
        bool has_server_input();

        // Graceful end of the session, for an owner that can await it
        // (tests); the destructor does the same, blocking, if it has not
        // been done
        [[nodiscard]] coroutine::Task end_session(std::chrono::milliseconds grace);

        explicit WorldState(GuiContext& gui) : _gui(gui) {

            // Overlay wiring.  The floating log + console are app-tier now:
            // GuiContext owns them (its ctor wires console->log and pushes them
            // into GuiContext::overlays), and the host + scenes dispatch / paint
            // that stack.  This scene stack holds only the world's palette (+ the
            // in-game menu / save-list, pushed dynamically).
            _palette_overlay.set_model(this);
            _main_menu_overlay.set_model(this);
            _save_list_overlay.set_model(this);
            _stack.push(&_palette_overlay);

            // The session and the first world: our own host and the
            // starting scenario, or, on --join, a client of another's
            // game.  At construction _worlds is empty, so the drain inside
            // is a no-op.
            _start_session();

            _uniforms.camera_position_world = make<float4>(0.0f, -8.0f, 16.0f, 1.0f);
            _regenerate_uniforms();

            // Command handlers reach the world through the app tier.
            _gui.world = this;

        }

        // Replace the displayed world wholesale, re-pointing _local_player at
        // the new world's Player.  new_game builds the starting scenario;
        // load_from_save deserializes a save file.  Both run on the main
        // thread between the event pump and the renderer, when _worlds holds
        // exactly the one displayed world, so draining and refilling it is
        // race-free.  Defined in model.cpp (needs io/save.hpp).
        void new_game();
        void load_from_save(int id);

        // Serialize the displayed world to a new save file.  Synchronous for
        // now; the world is read non-destructively (popped and re-pushed).
        void save_current();

        void _regenerate_uniforms();

        // Per-frame logic, driven by the scene (which is now a thin Metal
        // renderer over this state): advance the simulation and process input.
        // Defined in model.cpp.
        void update(double dt);
        void handle_events(float2 view_size_pt);
        void pump_legacy_event(gui::Event const& e);  // legacy world-input fallback

        // The cursor projected onto the ground plane, from the current
        // _mouse; project_cursor() stores it in _mouse4 for the renderer.
        // cursor_tile() is the tile under it: the computed default of the
        // commands a gesture issues (write, erase, pipette).  Nullopt when
        // the cursor ray misses the ground or lands beyond the coordinate
        // range (a non-finite or huge projection).
        simd_float4 cursor_on_ground() const;
        void project_cursor();
        std::optional<Coordinate> cursor_tile() const;

        // The hand.  Holding a term is a modal state: left click places
        // it, R / H / V reorient it (opcodes), Escape or Q drops it (and
        // only with an empty hand does Escape open the menu).  Q with an
        // empty hand is the pipette: it copies the tile under the cursor
        // into the hand, matter excepted.
        bool is_holding() const { return !term_is_null(_holding_value._value); }
        void drop_hand();
        void hold(Term t);
        bool pipette_at(Coordinate xy);

        ~WorldState();
        
        // The log / console / notifications API moved to GuiContext (_gui):
        //   _gui.append_log(...)
        //   _gui.post_notification(...)   (any thread)
        //   _gui.drain_notifications()    (main thread, once per frame)

    };
    
} // namespace wry

#endif /* world_state_hpp */
