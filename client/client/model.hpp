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
    union chunk {
        simd_ulong2 m[16][16];
        simd_ulong2 v[256];
        
    };
            
    // coordinates are stored as a u64 representing the interleaved bits of
    // two i32s.  we can use the low bits to directly index the chunk storage
    // and achieve z-order locality.  we can use the high bits as the hash
    // table key.  we can perform arithmetic by some masking, we do not need to
    // convert back to orthogonal representations.
    
    // however, this is an implementation detail, we should strive to expose it
    // little and test if it makes any performance difference
    
    struct coordinate {

        // ~mask selects bits 0, 2, 4, ... which contains x
        //  mask selects bits 1, 3, 5, ... which contains y
        static constexpr u64 mask = 0xAAAA'AAAA'AAAA'AAAA;

        u64 data;
        
        // when an integer is represented as a subsequence of bits, we can
        // perform addition [subtraction] by setting the uninvolved bits to
        // one [zero] so that the carrys [borrows] propagate up to the next
        // representation bit
        
        coordinate xinc() { return coordinate { bitselect((data |  mask) + 1, data, mask) }; }
        coordinate xdec() { return coordinate { bitselect((data & ~mask) - 1, data, mask) }; }
        coordinate yinc() { return coordinate { bitselect(data, (data | ~mask) + 1, mask) }; }
        coordinate ydec() { return coordinate { bitselect(data, (data &  mask) - 1, mask) }; }
        
        coordinate sum(coordinate other) {
            return coordinate {
                bitselect((data |  mask) + (other.data & ~mask),
                          (data | ~mask) + (other.data &  mask),
                          mask)
            };
        }

        coordinate difference(coordinate other) {
            return coordinate {
                bitselect((data & ~mask) - (other.data & ~mask),
                          (data &  mask) - (other.data &  mask),
                          mask)
            };
        }
        
        simd_int2 deinterleave() {
            simd_int2 yx = {};
            u64 z = morton2_reverse(data);
            std::memcpy(&yx, &z, 8);
            return yx;
        }
        
        static coordinate interleave(simd_int2 yx) {
            u64 z = {};
            std::memcpy(&z, &yx, 8);
            return coordinate { z };
        }
        
    }; // coordinate
        
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
                    *(p->v + j) = simd_make_ulong2(std::rand() & 0x5F, 0);
                }
                i = _map.emplace_hint(i, b, p);
            }
            return i->second->m[yx.y & 0xF][yx.x & 0xF];
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
