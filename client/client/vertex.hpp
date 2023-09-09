//
//  vertex.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef vertex_hpp
#define vertex_hpp

#include "ShaderTypes.h"

#include "simd.hpp"

namespace wry {
        
    struct subvertex {
        simd_float4 position; // 16
        simd_float2 texCoord; // 8
    };
    
    struct vertex {
        
        subvertex v; // 24
        RGBA8Unorm_sRGB color; // 4
        static void bind();
        
        vertex(subvertex s, RGBA8Unorm_sRGB c)
        : v(s)
        , color(c) {
        }
        
    };
    
    static_assert(sizeof(vertex) == 48);
    static_assert(alignof(vertex) == 16);

    static_assert(sizeof(vertex) == sizeof(MyVertex4));
    static_assert(alignof(vertex) == alignof(MyVertex4));

} // namespace wry

#endif /* vertex_hpp */

