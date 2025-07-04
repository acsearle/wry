//
//  atlas.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef atlas_hpp
#define atlas_hpp

#import <Metal/Metal.h>

#include "array.hpp"
#include "image.hpp"
#include "packer.hpp"
#include "vertex.hpp"

namespace wry {
    
    
    // sprite has to store axis-aligned texture rect and screen-space rect, so 8x floats
    // is minimal for full generality.  We store in the format that is closest
    // to the vertices that will be emitted - just add the offset and construct
    // the opposite corners.
    
    struct Sprite {
        SpriteSubVertex a;
        SpriteSubVertex b;
    };
    
    inline Sprite operator+(Sprite s, float2 xy) {
        s.a.position.xy += xy;
        s.b.position.xy += xy;
        return s;
    }
    
    // large groups of squares or axis-aligned rectangles can be processed on
    // the GPU with object-mesh shaders, so rethink what we need to pass
    
    // virtualize atlas to hide the explicitly Metal texture?
            
    // atlas bundles together some functionality, some of which should move elsewhere
    //
    // allocate subregions of the texture atlas, returning sprite keys
    //
    // managing the lifetime of the texture
    //
    // provide the interface for gethering sprite draws
    //
    // setting the texture passing the sprites to the GPU
    //
    // managing the round-robin use of GPU vertex buffers
    
    struct SpriteAtlas {
        
        std::size_t _size;
        packer<std::size_t> _packer;
        
        ContiguousDeque<SpriteVertex> _vertices;
        
        id<MTLTexture> _texture;
        id<MTLBuffer> _buffers[4];
        
        SpriteAtlas(std::size_t n, id<MTLDevice> device);
        
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
            // Draw an arbitrary quad, such as one resulting from a rotation
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
        
        void commit(id<MTLRenderCommandEncoder> renderEncoder);
        
        void discard();
        
        Sprite place(matrix_view<const RGBA8Unorm_sRGB>, float2 origin = { 0, 0 });
        
        void release(Sprite);
        
    }; // atlas
    
} // wry

#endif /* atlas_hpp */
