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
#include "gui_context.hpp"
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
        
        // Borrowed, host-owned generic GUI state (events, viewport, log /
        // console, notifications).  Set in the constructor; see gui_context.hpp.
        GuiContext& _gui;

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


        // (debug toggles _show_jacobian / _show_points / _show_wireframe moved
        //  to WryWorldScene.)


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

        // _holding_value (the opcode in hand) and _mouse (NDC cursor) stay here
        // for now: the palette overlay writes / reads them.  The other legacy
        // world-input fields (_outstanding_click / _outstanding_keysdown,
        // _looking_at, _mouse4) moved to WryWorldScene in 5.4b.
        Root<Term> _holding_value = {};
        float2 _mouse = {};
        
        // visualization state
        
        
        // Camera and sun projections
        
        // (_viewport_size moved to GuiContext / _gui.)

        MeshUniforms _uniforms;

        explicit model(GuiContext& gui) : _gui(gui) {

            // Overlay wiring.  Stack order, bottom up: floating log, palette,
            // console (modal-on-top).  Push order = paint order; dispatch is
            // the reverse walk.  Log and console live on the borrowed
            // GuiContext now; the palette / main menu / save-list are
            // model-owned (main menu / save-list pushed dynamically).
            _gui.console_overlay.set_log(&_gui.log_overlay);
            _palette_overlay.set_model(this);
            _main_menu_overlay.set_model(this);
            _save_list_overlay.set_model(this);
            _stack.push(&_gui.log_overlay);
            _stack.push(&_palette_overlay);
            _stack.push(&_gui.console_overlay);

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
        
        // The log / console / notifications API moved to GuiContext (_gui):
        //   _gui.append_log(...)
        //   _gui.post_notification(...)   (any thread)
        //   _gui.drain_notifications()    (main thread, once per frame)

    };
    
} // namespace wry

#endif /* model_hpp */
