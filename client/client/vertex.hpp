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
#include "sRGB.hpp"

namespace wry {
    
    // these are specific to the texture atlas, make their name less intrusive
        
    struct SpriteSubVertex {
        simd_float4 position; // 16
        float2 texCoord; // 8
    };
    
    struct SpriteVertex {
        
        SpriteSubVertex v; // 24
        RGBA8Unorm_sRGB color; // 4
        
        SpriteVertex() = default;
        SpriteVertex(SpriteSubVertex s, RGBA8Unorm_sRGB c)
        : v(s)
        , color(c) {
        }
        
    };
    
    static_assert(sizeof(SpriteVertex) == 48);
    static_assert(alignof(SpriteVertex) == 16);

    static_assert(sizeof(SpriteVertex) == sizeof(MyVertex4));
    static_assert(alignof(SpriteVertex) == alignof(MyVertex4));

} // namespace wry

#endif /* vertex_hpp */

