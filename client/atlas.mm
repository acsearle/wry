//
//  atlas.cpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#include <chrono>
#include <thread>

#include "atlas.hpp"
#include "debug.hpp"
#include "ShaderTypes.h"

namespace wry {
    
    atlas::atlas(std::size_t n, id<MTLDevice> device)
    : _size(n)
    , _packer(n) {
        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm_sRGB;
        descriptor.width = n;
        descriptor.height = n;
        
        // MTL asks for this but it may be an Apple Silicon thing?
         // Non-shared memory GPU: Managed ?
        // Shared memory GP: Shared ?
        descriptor.storageMode = MTLStorageModeShared;
        
        _texture = [device newTextureWithDescriptor:descriptor];
        _vertices.reserve(65536);
        _buffer = [device newBufferWithLength:sizeof(vertex) * _vertices.capacity()
                                      options:MTLResourceStorageModeShared];
        _buffer2 = [device newBufferWithLength:sizeof(vertex) * _vertices.capacity()
                                       options:MTLResourceStorageModeShared];
        _semaphore = dispatch_semaphore_create(2);
    }
    
    void atlas::commit(id<MTLRenderCommandEncoder> renderEncoder) {
        
        dispatch_semaphore_wait(_semaphore, DISPATCH_TIME_FOREVER);
        
        assert(_buffer.length >= _vertices.size() * sizeof(vertex));
        std::memcpy(_buffer.contents,
                    _vertices.data(),
                    _vertices.size() * sizeof(vertex));
        [renderEncoder setVertexBuffer:_buffer
                                offset:0
                               atIndex:MyVertexInputIndexVertices];
        [renderEncoder setFragmentTexture:_texture
                                  atIndex:AAPLTextureIndexBaseColor];
        [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                          vertexStart:0
                          vertexCount:_vertices.size()];
        _vertices.clear();
        std::swap(_buffer, _buffer2);
        
    }
    
    void atlas::discard() {
        _vertices.clear();
    }
    
    // Place a sprite within the free space of the atlas
    
    sprite atlas::place(const_matrix_view<pixel> v, simd_float2 origin) {
        auto tl = _packer.place(simd_make_ulong2(v.columns(), v.rows()));
        [_texture replaceRegion:MTLRegionMake2D(tl.x, tl.y,
                                                v.columns(), v.rows())
                    mipmapLevel:0
                      withBytes:v.data()
                    bytesPerRow:v.stride() * sizeof(pixel)];
        sprite s;
        s.a.position = simd_make_float4(-origin, 0, 1);
        s.a.texCoord = simd_float(tl) / (float) _size;
        s.b.position = simd_make_float4(v.columns() - origin.x,
                                        v.rows() - origin.y,
                                        0,
                                        1);
        s.b.texCoord = simd_make_float2(tl.x + v.columns(), tl.y + v.rows()) / _size;
        
        // for debug, also shade the split regions
        
        {
            for (auto&& a : _packer._last_split) {
                auto n = max(a.width(), a.height());
                pixel p{0,0,0,64};
                array<pixel> b;
                b.resize(n, p);
                //DUMP(a.a.x);
                //DUMP(a.a.y);
                //DUMP(a.width());
                //DUMP(a.height());
                //DUMP(b.size());
                [_texture replaceRegion:MTLRegionMake2D(a.a.x, a.a.y, a.width(), 1)
                            mipmapLevel:0
                              withBytes:b.data()
                            bytesPerRow:b.size() * sizeof(pixel)];
                [_texture replaceRegion:MTLRegionMake2D(a.a.x, a.a.y, 1, a.height())
                            mipmapLevel:0
                              withBytes:b.data()
                            bytesPerRow:sizeof(pixel)];
                [_texture replaceRegion:MTLRegionMake2D(a.a.x, a.a.y + a.height() - 1, a.width(), 1)
                            mipmapLevel:0
                              withBytes:b.data()
                            bytesPerRow:b.size() * sizeof(pixel)];
                [_texture replaceRegion:MTLRegionMake2D(a.a.x + a.width() - 1, a.a.y, 1, a.height())
                            mipmapLevel:0
                              withBytes:b.data()
                            bytesPerRow:sizeof(pixel)];
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

