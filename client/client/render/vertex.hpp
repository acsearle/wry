//
//  vertex.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef vertex_hpp
#define vertex_hpp

#include <cstddef>

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
        
        SpriteSubVertex v; // 24 (+8 padding)
        RGBA8Unorm_sRGB color; // 4

        // Rounded-box parameters; see MyVertex4.  Zero half_extent means
        // an ordinary textured sprite, so every existing producer that
        // doesn't set these keeps its old behavior.
        float radius = 0.0f; // 4
        float2 half_extent = {0.0f, 0.0f}; // 8

        SpriteVertex() = default;
        SpriteVertex(SpriteSubVertex s, RGBA8Unorm_sRGB c)
        : v(s)
        , color(c) {
        }

    };

    static_assert(sizeof(SpriteVertex) == 48);
    static_assert(alignof(SpriteVertex) == 16);
    static_assert(offsetof(SpriteVertex, color) == offsetof(MyVertex4, color));
    static_assert(offsetof(SpriteVertex, radius) == offsetof(MyVertex4, radius));
    static_assert(offsetof(SpriteVertex, half_extent) == offsetof(MyVertex4, half_extent));

    static_assert(sizeof(SpriteVertex) == sizeof(MyVertex4));
    static_assert(alignof(SpriteVertex) == alignof(MyVertex4));

} // namespace wry

#endif /* vertex_hpp */

