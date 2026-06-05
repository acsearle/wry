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
        
        
        
        Player* _local_player;


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
            _stack.push(&_log_overlay);
            _stack.push(&_palette_overlay);
            _stack.push(&_console_overlay);

            World* world = new World;

            {
                // new player
                Player* p = new Player;
                world->_entity_for_entity_id.set(p->_entity_id, p);
                // PersistentSet<EntityID> q;
                // world->_waiting_on_time.try_get(Time{0}, q);
                // q.set(p->_entity_id);
                // world->_waiting_on_time.set(Time{0}, q);
                world->_waiting_on_time.set({Time{0}, p->_entity_id});
                _local_player = p;
            }
                        
            auto insert_localized_entity = [&](LocalizedEntity const* entity_ptr) {
                EntityID entity_id = entity_ptr->_entity_id;
                // printf("insert_localized_entity %lld\n", entity_id.data);
                world->_entity_for_entity_id.set(entity_id,
                                                  entity_ptr);
                //_world->_entity_id_for_coordinate.set(entity_ptr->_location,
                //                                      entity_id);
                // _world->_ready.set(entity_id);
                // TODO: clumsy
                // PersistentSet<EntityID> q;
                // world->_waiting_on_time.try_get(Time{0}, q);
                // q.set(entity_id);
                // world->_waiting_on_time.set(Time{0}, q);
                world->_waiting_on_time.set({Time{0}, entity_id});
            };
            
            {
                // new machine spawner at origin
                Spawner* p = new Spawner;
                p->_location = Coordinate{0, 0};
                //_world->_entities.push_back(p);
                // entity_ready_on_world(p, _world);
                insert_localized_entity(p);
            }
            
            {
                // value source
                Source* q = new Source;
                q->_location = Coordinate{2, 2};
                q->_of_this = Term(1);
                // _world->_entities.push_back(q);
                // entity_ready_on_world(q, _world);
                insert_localized_entity(q);
            }
            
            {
                // value sink
                Sink* r = new Sink;
                r->_location = Coordinate{4, 2};
                // _world->_entities.push_back(r);
                // entity_ready_on_world(r, _world);
                insert_localized_entity(r);
            }

//            {
//                Counter* s = new Counter;
//                s->_location = Coordinate{-2, 2};
//                insert_localized_entity(s);
//            }
//
//            {
//                // a second counter to contest the transaction
//                Counter* s = new Counter;
//                s->_location = Coordinate{-2, 2};
//                insert_localized_entity(s);
//            }
//            
//            {
//                Evenator* s = new Evenator;
//                s->_location = Coordinate{-2, 2};
//                insert_localized_entity(s);
//            }

            //_world->_term_for_coordinate.write(Coordinate{-2, -2}, term_make_integer_with(7));
            world->_term_for_coordinate.set(Coordinate{-2, -2},
                                              term_make_integer_with((7)));
            //_world->_term_for_coordinate.write(Coordinate{-2, -2}, term_make_array());
            // _world->_term_for_coordinate.set(Coordinate{-2, -2}, term_make_array());
            world->_term_for_coordinate.set(Coordinate{0, +1},
                                              term_make_integer_with((1)));
            world->_term_for_coordinate.set(Coordinate{0, +2},
                                              term_make_integer_with((2)));
            world->_term_for_coordinate.set(Coordinate{0, +3},
                                              term_make_integer_with((3)));
            world->_term_for_coordinate.set(Coordinate{0, +4},
                                              term_make_opcode(OPCODE_FLIP_FLOP));
            world->_term_for_coordinate.set(Coordinate{0, +5},
                                              term_make_integer_with((5)));
            
            _worlds.emplace_back(world);
            
            _uniforms.camera_position_world = make<float4>(0.0f, -8.0f, 16.0f, 1.0f);
            _regenerate_uniforms();

        }
        
        void _regenerate_uniforms();
        
        ~model() {
            fprintf(stderr, "%s\n", __PRETTY_FUNCTION__);
        }
        
        void append_log(StringView v,
                        std::chrono::steady_clock::duration endurance = std::chrono::seconds(5)) {
            _log_overlay.append(v, endurance);
        }
        
    };
    
} // namespace wry

#endif /* model_hpp */
