//
//  model.hpp
//  client
//
//  Created by Antony Searle on 8/7/2023.
//

#ifndef model_hpp
#define model_hpp

#include "ShaderTypes.h"

#include <memory>
#include <mutex>
#include <map>
#include <vector>

#include "contiguous_deque.hpp"
#include "entity.hpp"
#include "gui_event.hpp"
#include "gui_overlay.hpp"
#include "hash.hpp"
#include "machine.hpp"
#include "palette.hpp"
#include "simd.hpp"
#include "sim.hpp"
#include "spawner.hpp"
#include "string.hpp"
#include "world.hpp"
#include "player.hpp"

namespace wry {
    
    struct model {
        
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
        
        // simulation state
        // World* _world;
        // BlockingDeque<World const*> _worlds;
        BlockingDeque<Root<World const*>> _worlds;



        // Borrowed from the displayed world's entity map (which holds it as
        // const Entity*); the model drives input through its mutable _queue.
        // Re-pointed whenever the displayed world is replaced wholesale
        // (new_game / load_from_save); across ordinary World::step() it stays
        // valid because the Player entry is shared, not rebuilt.
        Player const* _local_player = nullptr;


        // debug state

        bool _show_jacobian = false;
        bool _show_points = false;
        bool _show_wireframe = false;


        // user interface state

        // Raw NSEvents arrive on the main thread via WryDelegate, which
        // translates each into a wry::gui::Event and pushes it here.  The
        // per-frame pump in [WryDelegate render] drains this queue, walking
        // each event through `_stack` and (for whatever the stack hasn't
        // claimed) into the legacy fields below.  Single-threaded; no lock.
        gui::EventQueue _events;

        // Persistent GUI elements.  Each owns its own state (console lines,
        // floating log entries, palette selection / hover); the stack
        // borrows pointers to them and runs the dispatch + paint walks.
        // The main menu and save-list both live here so their lifetimes
        // span push/pop cycles -- the stack only borrows pointers, it
        // doesn't own them.
        gui::LogOverlay _log_overlay;
        gui::ConsoleOverlay _console_overlay;
        gui::PaletteOverlay _palette_overlay;
        gui::MainMenuOverlay _main_menu_overlay;
        gui::SaveListOverlay _save_list_overlay;
        gui::OverlayStack _stack;

        // Legacy fields still read by WryRenderer.  These shrink as
        // overlays take over their responsibilities:
        //   _outstanding_click / _mouse4   -> WorldOverlay (later phase)
        //   _outstanding_keysdown          -> WorldOverlay (later phase)
        //   _looking_at                    -> WorldOverlay (later phase)
        bool _outstanding_click = false;
        Root<Term> _holding_value = {};
        float2 _looking_at = {};
        float2 _mouse = {};
        simd_float4 _mouse4 = {};

        String _outstanding_keysdown;
        
        // visualization state
        
        
        // Camera and sun projections
        
        float2 _viewport_size;

        MeshUniforms _uniforms;

        model() {

            // Overlay wiring.  Stack order, bottom up: floating log,
            // palette, console (modal-on-top).  Push order = paint order;
            // dispatch is the reverse walk.  The main menu and save-list
            // overlays are pushed dynamically (by ESC and by the main
            // menu's LOAD button respectively), not at startup.
            _console_overlay.set_log(&_log_overlay);
            _palette_overlay.set_model(this);
            _main_menu_overlay.set_model(this);
            _save_list_overlay.set_model(this);
            _stack.push(&_log_overlay);
            _stack.push(&_palette_overlay);
            _stack.push(&_console_overlay);

            // Install the starting world as the displayed world.  Same path
            // the NEW button uses; at construction _worlds is empty, so the
            // drain inside is a no-op.
            new_game();

            _uniforms.camera_position_world = make<float4>(0.0f, -8.0f, 16.0f, 1.0f);
            _regenerate_uniforms();

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
        
        ~model() {
            fprintf(stderr, "%s\n", __PRETTY_FUNCTION__);
        }
        
        void append_log(StringView v,
                        std::chrono::steady_clock::duration endurance = std::chrono::seconds(5)) {
            _log_overlay.append(v, endurance);
        }

        // Background work (e.g. an async save completing on a worker thread)
        // can't touch the log overlay directly, so it posts a short message
        // here; the per-frame pump drains them on the main thread.  Messages are
        // string literals (static lifetime), so storing the pointer is safe.
        std::mutex _notifications_mutex;
        std::vector<const char*> _notifications;

        void post_notification(const char* message) {  // any thread
            std::scoped_lock lock{_notifications_mutex};
            _notifications.push_back(message);
        }

        void drain_notifications() {  // main thread, once per frame
            std::vector<const char*> pending;
            {
                std::scoped_lock lock{_notifications_mutex};
                pending.swap(_notifications);
            }
            for (const char* m : pending)
                append_log(m);
        }

    };
    
} // namespace wry

#endif /* model_hpp */
