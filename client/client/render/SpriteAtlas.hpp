//
//  atlas.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef atlas_hpp
#define atlas_hpp

#include <bit>
#include <cstddef>

#include "contiguous_deque.hpp"
#include "image.hpp"
#include "packer.hpp"
#include "vertex.hpp"

// This header is intentionally Metal-free.  The texture and vertex-buffer
// ring that back the atlas live in an opaque Impl owned by SpriteAtlas
// (the pimpl pattern), defined and constructed in SpriteAtlas.mm.  The
// hot path -- accumulating quad instances via push_quad -- stays inline
// in the header and is pure C++, so any TU (regardless of whether it ends
// up compiled as C++ or Objective-C++) can `#include` this header without
// dragging <Metal/Metal.h> in transitively.
//
// The construction and commit hooks accept `void*` rather than id<MTL...>
// so the header has no Objective-C surface.  ObjC++ callers bridge-cast at
// the call site, e.g.
//
//     _atlas = new wry::SpriteAtlas(2048, (__bridge void*)device,
//                                   (__bridge void*)arena);
//     _atlas->commit((__bridge void*)encoder, (__bridge void*)pipeline);

namespace wry {


    // Sprite has to store axis-aligned texture rect and screen-space rect,
    // so 8x floats is minimal for full generality.  We store in the format
    // that is closest to the vertices we will emit -- just add the offset
    // and construct the opposite corners.

    struct Sprite {
        SpriteSubVertex a;
        SpriteSubVertex b;
    };

    inline Sprite operator+(Sprite s, float2 xy) {
        s.a.position.xy += xy;
        s.b.position.xy += xy;
        return s;
    }


    struct SpriteAtlas {

        // Pure C++ state.  Quad accumulation is the hot path and happens
        // entirely in pure C++ via push_quad below.  One WryQuadInstance per
        // GUI primitive (glyph, fill, rounded box); see ShaderTypes.h.
        std::size_t _size;
        packer<std::size_t> _packer;
        ContiguousDeque<WryQuadInstance> _quads;

        // Opaque pimpl pointer.  Holds the id<MTLTexture> and the
        // WryBufferArena that commit draws instance memory from.  Defined
        // and constructed in SpriteAtlas.mm so this header stays
        // Metal-free.
        struct Impl;
        Impl* _impl = nullptr;


        // device_handle and arena_handle are opaque handles that ObjC++
        // callers obtain by bridge-casting an id<MTLDevice> and a
        // WryBufferArena*; see file comment.
        SpriteAtlas(std::size_t n, void* device_handle, void* arena_handle);
        ~SpriteAtlas();

        // Non-copyable, non-movable (would require pimpl ownership semantics
        // we don't need yet).
        SpriteAtlas(SpriteAtlas const&) = delete;
        SpriteAtlas& operator=(SpriteAtlas const&) = delete;


        Sprite as_sprite() const {
            return Sprite{
                {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
                {{(float) _size, (float)_size, 0.0f, 1.0f}, {1.0f, 1.0f}},
            };
        }

        // Untextured, unmasked: a solid-color rectangle.  Fields not
        // relevant to a given primitive are zero.
        static WryQuadInstance make_quad(float4 rect, RGBA8Unorm_sRGB c) {
            WryQuadInstance q = {};
            q.shape = rect;
            q.draw = rect;
            q.color = std::bit_cast<unsigned int>(c);
            return q;
        }

        // Textured, unmasked: a sprite tinted by c.
        static WryQuadInstance make_quad(Sprite s, RGBA8Unorm_sRGB c) {
            WryQuadInstance q = make_quad(simd_make_float4(s.a.position.xy,
                                                           s.b.position.xy), c);
            q.uv = simd_make_float4(s.a.texCoord, s.b.texCoord);
            q.flags = WryQuadFlagTextured;
            return q;
        }

        void push_quad(WryQuadInstance const& q) {
            _quads.push_back(q);
        }

        void push_sprite(Sprite s, RGBA8Unorm_sRGB c = RGBA8Unorm_sRGB(1.0f, 1.0f, 1.0f, 1.0f)) {
            push_quad(make_quad(s, c));
        }

        void discard() {
            _quads.clear();
        }

        // Draw and clear the accumulated quads.  encoder_handle is
        // (__bridge void*)id<MTLRenderCommandEncoder>; pipeline_handle is
        // (__bridge void*)id<MTLRenderPipelineState>, a quadVertexShader /
        // quadFragmentShader pipeline matching the encoder's attachments.
        // Leaves that pipeline bound.  Expects the screen-space MyUniforms
        // already bound at AAPLBufferIndexUniforms.
        void commit(void* encoder_handle, void* pipeline_handle);

        Sprite place(matrix_view<const RGBA8Unorm_sRGB>, float2 origin = { 0, 0 });
        void release(Sprite);

    }; // SpriteAtlas

} // namespace wry

#endif /* atlas_hpp */
