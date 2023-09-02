//
//  model.hpp
//  client
//
//  Created by Antony Searle on 8/7/2023.
//

#ifndef model_hpp
#define model_hpp

#include <unordered_map>
#include <memory>
#include <mutex>


#include "simd.hpp"
#include "array.hpp"
#include "hash.hpp"
#include "string.hpp"
#include "world.hpp"

namespace wry {

    struct model {
                
        std::mutex _mutex;

        array<string> _console;
        std::multimap<std::chrono::steady_clock::time_point, string> _logs;
        
        bool _console_active = false;        
        bool _show_jacobian = false;
        bool _show_points = false;
        bool _show_wireframe = false;

        simd_float2 _yx = {};
        world _world;

        model() {
            _console.emplace_back("\"Behold, a [console]!\"");
            _console.emplace_back("");
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
