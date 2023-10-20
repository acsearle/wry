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

#include "array.hpp"
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
        
        // The model holds all the app state, including the sim::world, but
        // also the visualization-only parts of the app state
        
        // We'll try to keep the model / WryRenderer distinction to roughly be
        // the platform-independent / platform-specific code dividing line,
        // though things like simd_ make this ambiguous
        
        // simulation state
        
        sim::World _world;

        
        // debug state
        
        array<string> _console;
        std::multimap<std::chrono::steady_clock::time_point, string> _logs;
        
        bool _console_active = false;        
        bool _show_jacobian = false;
        bool _show_points = false;
        bool _show_wireframe = false;
        

        // user interface state
        
        bool _outstanding_click = false;
        sim::Value _holding_value = {};
        difference_type _selected_i = -1;
        difference_type _selected_j = -1;
        simd_float2 _looking_at = {};
        simd_float2 _mouse = {};
        simd_float4 _mouse4 = {};
        
        // visualization state
        
        
        // Camera and sun projections
        
        simd_float2 _viewport_size;

        MeshUniforms _uniforms;

        model() {

            _console.emplace_back("WryApplication");
            _console.emplace_back("");

            using namespace sim;
            // new machine spawner at origin
            Entity* p = new Spawner;
            _world._all_entities.push_back(p);
            // owns the lock at the origin
            _world._tiles[Coordinate{0,0}]._lock_queue.push_back(p);
            // ready to run its lock-acquired-action
            _world._location_locked.emplace_back(Coordinate{0,0}, p);
            
            _uniforms.camera_position_world = simd_make_float4(0.0f, -8.0f, 16.0f, 1.0f);
            _regenerate_uniforms();

        }
        
        void _regenerate_uniforms();
        
        ~model() {
            fprintf(stderr, "%s\n", __PRETTY_FUNCTION__);
        }
        
        void append_log(string_view v,
                        std::chrono::steady_clock::duration endurance = std::chrono::seconds(5)) {
            _logs.emplace(std::chrono::steady_clock::now() + endurance, v);
        }
        
        
        
    };
    
} // namespace wry

#endif /* model_hpp */
