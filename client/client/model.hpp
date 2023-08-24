//
//  model.hpp
//  client
//
//  Created by Antony Searle on 8/7/2023.
//

#ifndef model_hpp
#define model_hpp

#include <simd/simd.h>

#include <unordered_map>
#include <memory>
#include <mutex>

#include "array.hpp"
#include "hash.hpp"
#include "string.hpp"
#include "world.hpp"

namespace wry {

    struct model {
        
        std::mutex _mutex;
        array<string> _console;
        simd_float2 _yx = {};
        world _world;
                
        model() {
            _console.emplace_back("\"Behold, a [console]!\"");
            _console.emplace_back("");
        }
        
        ~model() {
            fprintf(stderr, "%s\n", __PRETTY_FUNCTION__);
        }
        
    };
    
} // namespace wry

#endif /* model_hpp */
