//
//  mesh.cpp
//  client
//
//  Created by Antony Searle on 17/8/2023.
//

#include "mesh.hpp"

namespace wry {

    /*
    struct mesh {
        
        array<simd_float3> _vertices;
        array<int> _indices;
        
        static mesh box(simd_float3 a, simd_float3 b) {
            
            array<simd_float3> v({
                simd_select(a, b, simd_make_int3(0, 0, 0)),
                simd_select(a, b, simd_make_int3(0, 0, 1)),
                simd_select(a, b, simd_make_int3(0, 1, 0)),
                simd_select(a, b, simd_make_int3(0, 1, 1)),
                simd_select(a, b, simd_make_int3(1, 0, 0)),
                simd_select(a, b, simd_make_int3(1, 0, 1)),
                simd_select(a, b, simd_make_int3(1, 1, 0)),
                simd_select(a, b, simd_make_int3(1, 1, 1)),
            });
            
            array<int> u({
                0, 1, 2, 2, 1, 3,
                0, 5, 1, 1, 5, 4,
                0, 6, 2, 2, 6, 4,
                1, 7, 3, 3, 7, 6,
                2, 6, 3, 3, 6, 7,
                4, 6, 5, 5, 6, 7,
            });

            return mesh{v, u};
        }
        
    };
     */
    
    
} // namespace wry
