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
    
    sprite atlas::place(matrix_view<const RGBA8Unorm_sRGB> v, simd_float2 origin) {
        auto tl = _packer.place(simd_make_ulong2(v.get_major(),
                                                 v.get_minor()));
        [_texture replaceRegion:MTLRegionMake2D(tl.x, tl.y,
                                                v.get_major(), v.get_minor())
                    mipmapLevel:0
                      withBytes:v.data()
                    bytesPerRow:v.bytes_per_row()];
        sprite s;
        s.a.position = simd_make_float4(-origin, 0, 1);
        s.a.texCoord = simd_float(tl) / (float) _size;
        s.b.position = simd_make_float4(v.get_major() - origin.x,
                                        v.get_minor() - origin.y,
                                        0,
                                        1);
        s.b.texCoord = simd_make_float2(tl.x + v.get_major(), tl.y + v.get_minor()) / _size;
        
        // for debug, also shade the split regions
        
        {
            for (auto&& a : _packer._last_split) {
                auto n = max(a.width(), a.height());
                RGBA8Unorm_sRGB p(0.0f,0.0f,0.0f,0.25f);
                array<RGBA8Unorm_sRGB> b;
                b.resize(n, p);
                //DUMP(a.a.x);
                //DUMP(a.a.y);
                //DUMP(a.width());
                //DUMP(a.height());
                //DUMP(b.size());
                [_texture replaceRegion:MTLRegionMake2D(a.a.x, a.a.y, a.width(), 1)
                            mipmapLevel:0
                              withBytes:b.data()
                            bytesPerRow:b.size() * sizeof(RGBA8Unorm_sRGB)];
                [_texture replaceRegion:MTLRegionMake2D(a.a.x, a.a.y, 1, a.height())
                            mipmapLevel:0
                              withBytes:b.data()
                            bytesPerRow:sizeof(RGBA8Unorm_sRGB)];
                [_texture replaceRegion:MTLRegionMake2D(a.a.x, a.a.y + a.height() - 1, a.width(), 1)
                            mipmapLevel:0
                              withBytes:b.data()
                            bytesPerRow:b.size() * sizeof(RGBA8Unorm_sRGB)];
                [_texture replaceRegion:MTLRegionMake2D(a.a.x + a.width() - 1, a.a.y, 1, a.height())
                            mipmapLevel:0
                              withBytes:b.data()
                            bytesPerRow:sizeof(RGBA8Unorm_sRGB)];
            }
        }
        
        
        return s;
    }
    
    // todo:
    // The atlas only knows what space is free, not how the used space is used or
    // what it represents.  This is kinda cool but for debugging purposes we
    // should maybe keep track of what asset is where, in some cold storage
    // somewhere
    
    
    void atlas::release(sprite s) {
        auto a = simd_ulong(s.a.texCoord * _size);
        auto b = simd_ulong(s.b.texCoord * _size);
        _packer.release(a, b);
    }
    
} // namespace manic

