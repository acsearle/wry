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
#include "hash.hpp"
#include "machine.hpp"
#include "palette.hpp"
#include "simd.hpp"
#include "sim.hpp"
#include "spawner.hpp"
#include "string.hpp"
#include "table.hpp"
#include "world.hpp"

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
        World* _world;

        
        // debug state
        
        ContiguousDeque<String> _console;
        std::multimap<std::chrono::steady_clock::time_point, String> _logs;
        
        bool _console_active = false;        
        bool _show_jacobian = false;
        bool _show_points = false;
        bool _show_wireframe = false;
        

        // user interface state
        
        bool _outstanding_click = false;
        Value _holding_value = {};
        difference_type _selected_i = -1;
        difference_type _selected_j = -1;
        float2 _looking_at = {};
        float2 _mouse = {};
        simd_float4 _mouse4 = {};
        
        String _outstanding_keysdown;
        
        // visualization state
        
        
        // Camera and sun projections
        
        float2 _viewport_size;

        MeshUniforms _uniforms;

        model() {
            
            _world = new World;

            _console.emplace_back("WryApplication");
            _console.emplace_back("");
                        
            auto insert_localized_entity = [&](LocalizedEntity const* entity_ptr) {
                EntityID entity_id = entity_ptr->_entity_id;
                printf("insert_localized_entity %lld\n", entity_id.data);
                _world->_entity_for_entity_id.set(entity_id,
                                                  entity_ptr);
                _world->_entity_id_for_coordinate.set(entity_ptr->_location,
                                                      entity_id);
                // _world->_ready.set(entity_id);
                // TODO: clumsy
                PersistentSet<EntityID> q;
                _world->_waiting_on_time.try_get(Time{0}, q);
                q.set(entity_id);
                _world->_waiting_on_time.set(Time{0}, q);
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
                q->_of_this = Value(1);
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

            //_world->_value_for_coordinate.write(Coordinate{-2, -2}, value_make_integer_with(7));
            _world->_value_for_coordinate.set(Coordinate{-2, -2},
                                              value_make_integer_with((7)));
            //_world->_value_for_coordinate.write(Coordinate{-2, -2}, value_make_array());
            // _world->_value_for_coordinate.set(Coordinate{-2, -2}, value_make_array());
            _uniforms.camera_position_world = make<float4>(0.0f, -8.0f, 16.0f, 1.0f);
            _regenerate_uniforms();

        }
        
        void _regenerate_uniforms();
        
        ~model() {
            fprintf(stderr, "%s\n", __PRETTY_FUNCTION__);
        }
        
        void append_log(StringView v,
                        std::chrono::steady_clock::duration endurance = std::chrono::seconds(5)) {
            _logs.emplace(std::chrono::steady_clock::now() + endurance, v);
        }
        
        void shade_roots();
                
    };
    
} // namespace wry

#endif /* model_hpp */
