//
//  vertex.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef vertex_hpp
#define vertex_hpp

#include <simd/simd.h>

namespace wry {
        
    struct subvertex {
        simd_float2 position; // 8
        simd_float2 texCoord; // 8
    };
    
    struct vertex {
        subvertex v; // 16
        simd_uchar4 color; // 4
        static void bind();
    };
    
    static_assert(sizeof(vertex) == 24);
    static_assert(alignof(vertex) == 8);
    
} // namespace wry

#endif /* vertex_hpp */

