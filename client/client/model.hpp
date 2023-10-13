//
//  model.hpp
//  client
//
//  Created by Antony Searle on 8/7/2023.
//

#ifndef model_hpp
#define model_hpp

#include <memory>
#include <mutex>
#include <map>


#include "array.hpp"
#include "entity.hpp"
#include "hash.hpp"
#include "machine.hpp"
#include "sim.hpp"
#include "simd.hpp"
#include "spawner.hpp"
#include "string.hpp"
#include "table.hpp"
#include "world.hpp"

namespace wry {
    
    
   
   

    struct model {
        
        // local state
        
        std::mutex _mutex;

        array<string> _console;
        std::multimap<std::chrono::steady_clock::time_point, string> _logs;
        
        bool _console_active = false;        
        bool _show_jacobian = false;
        bool _show_points = false;
        bool _show_wireframe = false;
        
        bool _outstanding_click = false;
        sim::Value _holding_value = {};
        difference_type _selected_i = -1;
        difference_type _selected_j = -1;


        simd_float2 _looking_at = {};
        simd_float2 _mouse = {};
        simd_float4 _mouse4 = {};
        
        // simulation state
        
        sim::World _world;

        model() {
            
            _console.emplace_back("\"Behold, a [console]!\"");
            _console.emplace_back("");

            using namespace sim;
            /*
            _world.set({0, 1}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({1, 1}, {DISCRIMINANT_OPCODE, OPCODE_LOAD});
            _world.set({2, 1}, {DISCRIMINANT_NUMBER, 1});
            _world.set({3, 1}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({3, 0}, {DISCRIMINANT_OPCODE, OPCODE_ADD});
            _world.set({3, -1}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({0, -1}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
             */
            // _world.set(simd_make_int2(1, -1), OPCODE_HALT);

            _world.set({0, 4}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({2, 4}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({2, 2}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({-4, 2}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({-4, 4}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({-2, 4}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({-2, -2}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({-4, -2}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({-4, 0}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({2, 0}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({2, -2}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});
            _world.set({0, -2}, {DISCRIMINANT_OPCODE, OPCODE_TURN_RIGHT});


            // new machine spawner at origin
            Entity* p = new Spawner;
            _world._all_entities.push_back(p);
            
            // owns the lock at the origin
            _world._tiles[Coordinate{0,0}]._lock_queue.push_back(p);
            
            // ready to run its lock-acquired-action
            _world._location_locked.emplace_back(Coordinate{0,0}, p);


        }
        
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
