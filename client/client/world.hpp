//
//  world.hpp
//  client
//
//  Created by Antony Searle on 30/7/2023.
//

#ifndef world_hpp
#define world_hpp

#include "simd.hpp"

#include "utility.hpp"
#include "table.hpp"

/*
 
namespace wry {

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
        static constexpr uint64_t mask = 0xAAAA'AAAA'AAAA'AAAA;
        
        uint64_t data;
        
        // when an integer is represented as a subsequence of bits, we can
        // perform addition [subtraction] by setting the uninvolved bits to
        // one [zero] so that the carrys [borrows] propagate up to the next
        // representation bit
        
        coordinate xinc() { return coordinate { simd_bitselect((data |  mask) + 1, data, mask) }; }
        coordinate xdec() { return coordinate { simd_bitselect((data & ~mask) - 1, data, mask) }; }
        coordinate yinc() { return coordinate { simd_bitselect(data, (data | ~mask) + 1, mask) }; }
        coordinate ydec() { return coordinate { simd_bitselect(data, (data &  mask) - 1, mask) }; }
        
        coordinate sum(coordinate other) {
            return coordinate {
                simd_bitselect((data |  mask) + (other.data & ~mask),
                          (data | ~mask) + (other.data &  mask),
                          mask)
            };
        }
        
        coordinate difference(coordinate other) {
            return coordinate {
                simd_bitselect((data & ~mask) - (other.data & ~mask),
                          (data &  mask) - (other.data &  mask),
                          mask)
            };
        }
        
        simd_int2 deinterleave() {
            simd_int2 yx = {};
            uint64_t z = morton2_reverse(data);
            std::memcpy(&yx, &z, 8);
            return yx;
        }
        
        static coordinate interleave(simd_int2 yx) {
            uint64_t z = {};
            std::memcpy(&z, &yx, 8);
            return coordinate { z };
        }
        
    }; // coordinate
    
    
    // 16 bytes, 16 x 16 chunk
    union chunk {
        simd_ulong2 m[16][16];
        simd_ulong2 v[256];
        
    };
    
    struct entity;
    
    struct tile {
        
    };
    
    
    struct world {
        
        // world services
        
        // chunk* chunk_by_location(simd_int2 yx);
        // entity* entity_by_location(simd_int2 yx);
        
        simd_ulong2 tile_read(simd_int2 xy);
        void tile_write(simd_int2 xy, simd_ulong2 expected, simd_ulong2 desired, entity* receiver);
        void tile_wait(simd_int2 xy, entity* receiver);
        
        void entity_schedule(entity* receiver);
        void entity_schedule_when(std::uint64_t, entity* receiver);
        
        // like atomics,
        
        
        
        
        
        table<uint64_t, chunk*> _map;
        simd_ulong2& operator()(simd_int2 yx) {
            uint64_t a;
            std::memcpy(&a, &yx, 8);
            uint64_t b = a & 0xFFFF'FFF0'FFFF'FFF0;
            auto& r = _map[b];
            if (!r) {
                auto p = new chunk;
                for (int j = 0; j != 256; ++j) {
                    *(p->v + j) = simd_make_ulong2(std::rand() & 0x5F, 0);
                }
                r = p;
            }
            return r->m[yx.y & 0xF][yx.x & 0xF];
        }
        
    };
    
    
    
    struct simulation {
        
        using time_type = std::int64_t;
        
        using coordinate_type = simd_int2;
        
        void wait_on_time(void*, time_type);
        void wait_on_coordinate(void*, coordinate_type);
        
    };
    
}
 
 */


#endif /* world_hpp */
