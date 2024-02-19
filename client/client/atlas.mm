//
//  atlas.cpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#include "ShaderTypes.h"

#include <chrono>
#include <thread>

#include "atlas.hpp"
#include "debug.hpp"

namespace wry {
    
    atlas::atlas(std::size_t n, id<MTLDevice> device)
    : _size(n)
    , _packer(n) {
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm_sRGB;
        descriptor.width = n;
        descriptor.height = n;
        descriptor.usage = MTLTextureUsageShaderRead;
        
        // MTL asks for this but it may be an Apple Silicon thing?
         // Non-shared memory GPU: Managed ?
        // Shared memory GP: Shared ?
        descriptor.storageMode = MTLStorageModeShared;
        
        _texture = [device newTextureWithDescriptor:descriptor];
        _vertices.reserve(65536);
        for (auto& x : _buffers) {
            x = [device newBufferWithLength:sizeof(vertex) * _vertices.capacity()
                                    options:MTLResourceStorageModeShared];
        }
        _semaphore = dispatch_semaphore_create(4);
    }
    
    void atlas::commit(id<MTLRenderCommandEncoder> renderEncoder) {
        
        dispatch_semaphore_wait(_semaphore, DISPATCH_TIME_FOREVER);
        
        assert(_buffers[0].length >= _vertices.size() * sizeof(vertex));
        std::memcpy(_buffers[0].contents,
                    _vertices.data(),
                    _vertices.size() * sizeof(vertex));
        [renderEncoder setVertexBuffer:_buffers[0]
                                offset:0
                               atIndex:AAPLBufferIndexVertices];
        [renderEncoder setFragmentTexture:_texture
                                  atIndex:AAPLTextureIndexColor];
        [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                          vertexStart:0
                          vertexCount:_vertices.size()];
        _vertices.clear();
        std::rotate(_buffers, _buffers + 1, _buffers + 4);
        
    }
    
    void atlas::discard() {
        _vertices.clear();
    }
    
    // Place a sprite within the free space of the atlas
    
    sprite atlas::place(matrix_view<const RGBA8Unorm_sRGB> v, float2 origin) {
        auto tl = _packer.place(simd::make<simd::ulong2>(v.major(),
                                                 v.minor()));
        [_texture replaceRegion:MTLRegionMake2D(tl.x, tl.y,
                                                v.major(), v.minor())
                    mipmapLevel:0
                      withBytes:v.data()
                    bytesPerRow:v.major_bytes()];
        sprite s;
        s.a.position = make<float4>(-origin, 0, 1);
        s.a.texCoord = convert<float>(tl) / (float) _size;
        s.b.position = make<float4>(v.major() - origin.x,
                                        v.minor() - origin.y,
                                        0,
                                        1);
        s.b.texCoord = make<float2>(tl.x + v.major(), tl.y + v.minor()) / _size;
        
        // for debug, also shade the split regions
        
        /*
        {
            for (auto&& a : _packer._last_split) {
                auto n = max(a.width(), a.height());
                RGBA8Unorm_sRGB p(0.0f,0.0f,0.0f,0.25f);
                Array<RGBA8Unorm_sRGB> b;
                b.resize(n, p);
                //DUMP(a.a.x);
                //DUMP(a.a.y);
                //DUMP(a.width());
                //DUMP(a.height());
                //DUMP(b.size());
                [_texture replaceRegion:MTLRegionMake2D(a.a.x, a.a.y, a.width(), 1)
                            mipmapLevel:0
                              withBytes:b.data()
                            bytesPerRow:a.width() * sizeof(RGBA8Unorm_sRGB)];
                [_texture replaceRegion:MTLRegionMake2D(a.a.x, a.a.y, 1, a.height())
                            mipmapLevel:0
                              withBytes:b.data()
                            bytesPerRow:sizeof(RGBA8Unorm_sRGB)];
                [_texture replaceRegion:MTLRegionMake2D(a.a.x, a.a.y + a.height() - 1, a.width(), 1)
                            mipmapLevel:0
                              withBytes:b.data()
                            bytesPerRow:a.width() * sizeof(RGBA8Unorm_sRGB)];
                [_texture replaceRegion:MTLRegionMake2D(a.a.x + a.width() - 1, a.a.y, 1, a.height())
                            mipmapLevel:0
                              withBytes:b.data()
                            bytesPerRow:sizeof(RGBA8Unorm_sRGB)];
            }
        }*/
        
        
        return s;
    }
    
    // todo:
    // The atlas only knows what space is free, not how the used space is used or
    // what it represents.  This is kinda cool but for debugging purposes we
    // should maybe keep track of what asset is where, in some cold storage
    // somewhere
    
    
    void atlas::release(sprite s) {
        auto a = convert<ulong>(s.a.texCoord * _size);
        auto b = convert<ulong>(s.b.texCoord * _size);
        _packer.release(a, b);
    }
    
} // namespace manic

