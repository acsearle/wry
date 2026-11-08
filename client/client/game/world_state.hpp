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
#include <filesystem>
#include <memory>
#include <mutex>
#include <map>
#include <optional>
#include <vector>

#include "concurrent_queue.hpp"
#include "contiguous_deque.hpp"
#include "entity.hpp"
#include "functional.hpp"
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
#include "session_log.hpp"

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
        // Screen-space bloom (WryWorldScene: a Gaussian of the lit image,
        // added at 1/8).  Off: undecided whether we want it at all, and
        // computed-but-unshown it cost a full-screen blur every frame
        bool _show_bloom = false;
        // The image-based light's environment: an equirectangular PNG in
        // assets, by resource name (day.png, the outdoor default; room.png,
        // indoors with a window, tools/make_room_env.py).  The renderer
        // re-prefilters when it changes (`environment NAME`)
        std::string _environment = "day";
        // The symbol slabs' ambient-occlusion decals (toggle-symbol-decals)
        bool _show_symbol_decals = true;
        // Screen-space ambient occlusion (the `ssao` command): WRY_SSAO_*
        // mode (GTAO by default; see docs/todo.md on retiring the others),
        // whether to show the factor itself, the world radius and the
        // strength (1 = as estimated)
        int _ssao_mode = WRY_SSAO_GTAO;
        bool _ssao_show = false;
        float _ssao_radius = 0.5f;
        float _ssao_strength = 1.0f;
        int _ssao_slices = 3;           // GTAO tap budget: slices x 2 sides x steps
        int _ssao_steps = 4;
        bool _ssao_pyramid = true;      // GTAO taps read the depth pyramid
        int _ssao_blur = WRY_SSAO_BLUR_SEPARABLE;   // which blur kernel
        int _ssao_blur_radius = 2;      // blur taps per side (2: a 5 x 5 window)
        // Encode the occlusion this many times per frame (ssao-repeat): a
        // measuring aid, to make it dominate the frame and the GPU's clock
        int _ssao_repeat = 1;
        // The board (the `board` command; render/ShaderTypes.h
        // WryTerrainUniforms): the terrain as a printed board.  World
        // units unless named otherwise.
        float _board_line_width = 0.035f;
        float _board_line_min_pixels = 1.0f;
        float _board_line_alpha = 0.85f;
        float _board_mark_alpha = 0.7f;
        float _board_misregister_x = 0.05f;
        float _board_misregister_y = -0.03f;
        float _board_tint_blur = 0.06f;
        float _board_tint = 1.0f;           // 0 = a white board, 1 = the palette
        float _board_mark_bias = 0.0f;      // grows (negative) or shrinks the marks
        // Scene exposure (the `exposure` command): scales the sun and the sky
        // alike; 1 lights a white board to about mid grey
        float _exposure = 2.0f;
        // The symbol atlas the renderer shows (assets/NAME.png and the field
        // beside it): "assets" is the cutout glyphs, "assets_tile" the same
        // glyphs printed on Scrabble tiles (tools/make_tile_atlas.py)
        wry::String _symbol_atlas = wry::String("assets_tile");
        // The playing pieces (the machine's pentagon token): the glyph's
        // width in world units (2 * TOKEN_A / 24 in make_assets.py: the
        // tile box), how tall the piece stands, and its edge roundover
        float _token_width = 21.0f / 24.0f;
        float _token_height = 21.0f / 24.0f;
        float _token_fillet = 0.05f;
        // The slabs' ambient-occlusion decals on the ground under them
        // (Shaders.metal symbol_decal_fragment_function); off, GTAO does
        // the job, and the decal's stroke model overreaches for tiles
        bool _symbol_decals = false;


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
        // The camera focus: the world point at the centre of the view, as
        // an integer tile plus a sub-tile fraction, each component in
        // [-0.5, 0.5].  The renderer works in RENDER SPACE, world space
        // translated to put _focus_tile at the origin, so everything it
        // transforms is a small float however far out the focus is and
        // large coordinates suffer no catastrophic cancellation.  The tile
        // is added back only where world content is looked up (world_tile)
        // and taken off where entity locations are read (render_position).
        // The fraction is the camera's own offset from the origin and rides
        // in the view transform (_regenerate_uniforms): no model transform
        // carries the pan.
        Coordinate _focus_tile = {};
        float2 _focus_fraction = {};

        // The machine the camera follows: set by a left click on a truck
        // with an empty hand (the `follow` command), cleared by panning,
        // by `follow` off any truck, or when the machine is gone.  The
        // renderer writes where it drew the machine this frame (render
        // space), and the next frame's follow_camera() centres on it.
        EntityID _follow = {};
        std::optional<float2> _follow_drawn_at;
        void follow_camera();
        EntityID machine_at(Coordinate xy);
        simd_float4 _mouse4 = {};         // cursor on the ground plane, render space

        // The camera's position relative to the focus, in world units
        float3 _camera_offset = make<float3>(0.0f, -8.0f, 16.0f);

        // Render-space position of a world tile: its offset from the focus
        // tile, exact within 2^24 tiles of it (and modular across the i32
        // range, like the coordinates themselves)
        float2 render_position(Coordinate xy) const;

        // The world tile at a render-space point, or none if it is nowhere
        // (not finite, or beyond the i32 range of offsets)
        std::optional<Coordinate> world_tile(float2 render) const;

        // Move the focus by a render-space delta, carrying whole tiles into
        // _focus_tile so the fraction stays sub-tile, and regenerate the
        // camera for the new fraction
        void pan_focus(float2 delta);

        // Map view's model transform: render space scaled by `zoom` about
        // the focus (the renderer draws the map quad through it, and the
        // pinch handler anchors its zoom with it)
        simd_float4x4 map_model_transform(float zoom) const;

        // The zoom: the view's magnification relative to the default.
        // A separate scalar, applied to the camera every time the
        // uniforms are regenerated, never accumulated into a matrix.
        //
        // In world view (zoom >= ZOOM_MAP) the camera keeps its field of
        // view and sits at _camera_offset / zoom from the focus: dolly,
        // not telephoto, so zooming out never distorts.  Below ZOOM_MAP
        // the world gives way to map view: the renderer draws the world-
        // map quad scaled by zoom about the focus under the default
        // camera (so the view direction and lighting stay the world's
        // own), the palette hides, and world edits are suppressed.  The
        // two meet exactly at ZOOM_MAP (a dolly about the focus is a
        // similarity transform), so pinching across it is seamless.  The
        // minimap shows whenever map view is off.
        //
        // Pinch varies it, anchored at the cursor; TAB classifies it as
        // world or map and jumps to the other's default; scroll-pan
        // speed follows it so a swipe always traverses what it visually
        // crosses.
        static constexpr float ZOOM_DEFAULT = 1.0f;
        static constexpr float ZOOM_MAX = 2.0f;          // closest world view
        static constexpr float ZOOM_MAP = 0.5f;          // below: map view
        static constexpr float ZOOM_MAP_DEFAULT = 1.0f / 64.0f;  // TAB's map
        static constexpr float ZOOM_MIN = 1.0f / 256.0f; // whole map, small
        float _zoom = ZOOM_DEFAULT;
        bool show_map() const { return _zoom < ZOOM_MAP; }
        void set_zoom(float zoom);

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

        // The session log (multiplayer.md section 5, game/session_log.hpp):
        // the installed World as the checkpoint, then every applied step
        // and the hash it produced here.  Opened when a session gets its
        // World, closed with the session; the file appears at the first
        // step.  The directory is the saves' unless set before the
        // session's World is installed (tests)
        std::filesystem::path _log_directory;
        std::unique_ptr<SessionLog> _log;
        void _open_log(World const* world);

        // A replay (the `replay` command): the log's checkpoint installed,
        // its steps the backlog, each applied step's hash checked against
        // the recorded one.  Sessionless, since the stream comes from the
        // file; `result` is the report once it is done, or stopped
        struct Replay {
            std::filesystem::path path;
            uint64_t first = 0;             // the first step applied (head + 1)
            uint64_t through = 0;           // the last to apply
            std::vector<uint64_t> hashes;   // recorded, from `first`
            bool same_build = true;         // this build wrote the log
            std::optional<String> result;
        };
        std::optional<Replay> _replay;
        std::expected<void, String> replay(String const& path, std::optional<uint64_t> through);
        void _replay_check(Step const& step);

        // The displayed World as a game of our own from here: a fork
        // (leave), or a replayed World played on (host)
        void _host_displayed_world();

        // A deliberate desync (the `corrupt` command): `term` on `at` in
        // the displayed World only, off the stream
        void corrupt(Coordinate at, Term term);

        RemoteServer* _client();
        void _start_session(std::optional<int> save);
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
        // The connection's inbox, for a host that idles on it; null
        // without a session
        coroutine::MultiProducerSingleConsumerQueue<Transport::Event>* server_inbox();

        // Graceful end of the session, for an owner that can await it
        // (tests); the destructor does the same, blocking, if it has not
        // been done
        [[nodiscard]] coroutine::Task end_session(std::chrono::milliseconds grace);

        // The game from the starting scenario, or from save `save`
        // (save_ID.wry; the main menu's `load`)
        explicit WorldState(GuiContext& gui, std::optional<int> save = std::nullopt) : _gui(gui) {

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
            // starting scenario or the save, or, on --join and no save, a
            // client of another's game.  At construction _worlds is empty,
            // so the drain inside is a no-op.
            _start_session(save);

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

        // Serialize the displayed world to a new save file, in the
        // background (io/save.hpp): `on_done(id)` runs on a worker thread
        // once it has published save_ID.wry, or failed (id 0).  The world
        // is read non-destructively (popped and re-pushed).  False, and
        // nothing saved, if there is no displayed world
        bool save_current(move_only_function<void(int)> on_done = {});

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
        // The keys that act on what is in hand, for the hint line over the
        // palette: only the reorientations that change it (by its
        // symmetry class, opcode.hpp), bound as the live keymap says; then
        // place and drop.  Empty with an empty hand.
        std::string hand_hint() const;
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
