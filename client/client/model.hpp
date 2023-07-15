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

namespace wry {
    
    // 16 bytes, 16 x 16 chunk
    struct chunk {
        simd_ulong2 cells[16][16] = {};
    };
        
    struct world {
        std::unordered_map<u64, chunk*> _map;
        simd_ulong2& operator()(simd_int2 yx) {
            u64 a;
            std::memcpy(&a, &yx, 8);
            u64 b = a & 0xFFFF'FFF0'FFFF'FFF0;
            auto i = _map.find(b);
            if (i == _map.end()) {
                auto p = new chunk;
                for (int j = 0; j != 256; ++j) {
                    *(p->cells[0] + j) = simd_make_ulong2(std::rand() & 0x5F, 0);
                }
                i = _map.emplace_hint(i, b, p);
            }
            return i->second->cells[yx.y & 0xF][yx.x & 0xF];
        }
        
    };

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
            printf("~model\n");
        }
        
    };
    
} // namespace wry

#endif /* model_hpp */
