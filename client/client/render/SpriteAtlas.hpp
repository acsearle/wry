//
//  atlas.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef atlas_hpp
#define atlas_hpp

#include <cstddef>

#include "contiguous_deque.hpp"
#include "image.hpp"
#include "packer.hpp"
#include "vertex.hpp"

// This header is intentionally Metal-free.  The texture and vertex-buffer
// ring that back the atlas live in an opaque Impl owned by SpriteAtlas
// (the pimpl pattern), defined and constructed in SpriteAtlas.mm.  The
// hot path -- accumulating sprite vertices via push_sprite -- stays inline
// in the header and is pure C++, so any TU (regardless of whether it ends
// up compiled as C++ or Objective-C++) can `#include` this header without
// dragging <Metal/Metal.h> in transitively.
//
// The construction and commit hooks accept `void*` rather than id<MTL...>
// so the header has no Objective-C surface.  ObjC++ callers bridge-cast at
// the call site, e.g.
//
//     _atlas = new wry::SpriteAtlas(2048, (__bridge void*)device);
//     _atlas->commit((__bridge void*)encoder);

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

        // Pure C++ state.  Vertex accumulation is the hot path and happens
        // entirely in pure C++ via push_sprite below.
        std::size_t _size;
        packer<std::size_t> _packer;
        ContiguousDeque<SpriteVertex> _vertices;

        // A 1x1 white pixel reserved in the atlas at construction.  Lets
        // widgets draw solid-color rectangles via push_sprite by stretching
        // a copy of this sprite to the desired rect and passing the colour
        // through the vertex tint.  See Painter::fill_rect.
        Sprite _white = {};

        // Opaque pimpl pointer.  Holds id<MTLTexture> _texture and a ring
        // of id<MTLBuffer> _buffers[4].  Defined and constructed in
        // SpriteAtlas.mm so this header stays Metal-free.
        struct Impl;
        Impl* _impl = nullptr;


        // device_handle is an opaque handle that ObjC++ callers obtain by
        // bridge-casting an id<MTLDevice>; see file comment.
        SpriteAtlas(std::size_t n, void* device_handle);
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

        void push_sprite(Sprite s, RGBA8Unorm_sRGB c = RGBA8Unorm_sRGB(1.0f, 1.0f, 1.0f, 1.0f)) {
            // a - x
            // | \ | => abx ayb
            // y - b
            _vertices.emplace_back(s.a, c);
            _vertices.push_back({{
                { s.b.position.x, s.a.position.y, 0.0f, 1.0f},
                { s.b.texCoord.x, s.a.texCoord.y}}, c });
            _vertices.push_back({s.b, c});
            _vertices.push_back({s.a, c});
            _vertices.push_back({s.b, c});
            _vertices.push_back({{
                {s.a.position.x, s.b.position.y, 0.0f, 1.0f},
                {s.a.texCoord.x, s.b.texCoord.y}}, c});
        }

        void push_quad(SpriteVertex v[]) {
            // Draw an arbitrary quad, such as one resulting from a rotation.
            _vertices.push_back(v[0]);
            _vertices.push_back(v[1]);
            _vertices.push_back(v[2]);
            _vertices.push_back(v[2]);
            _vertices.push_back(v[1]);
            _vertices.push_back(v[3]);
        }

        void push_triangle(SpriteVertex v[]) {
            _vertices.push_back(v[0]);
            _vertices.push_back(v[1]);
            _vertices.push_back(v[2]);
        }

        void discard() {
            _vertices.clear();
        }

        // encoder_handle is (__bridge void*)id<MTLRenderCommandEncoder>.
        void commit(void* encoder_handle);

        Sprite place(matrix_view<const RGBA8Unorm_sRGB>, float2 origin = { 0, 0 });
        void release(Sprite);

    }; // SpriteAtlas

} // namespace wry

#endif /* atlas_hpp */
