//
//  SpriteAtlas.mm
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#include "ShaderTypes.h"

#include <chrono>
#include <thread>

#import <Metal/Metal.h>

#include "SpriteAtlas.hpp"
#include "debug.hpp"

namespace wry {

    // Definition of the opaque pimpl declared in SpriteAtlas.hpp.  Lives in
    // this Objective-C++ TU so its id<MTL...> ivars can be managed by ARC
    // (strong refs by default).  Pure C++ TUs only see the forward
    // declaration `struct Impl;` and never inspect this layout.
    struct SpriteAtlas::Impl {
        id<MTLTexture> texture;
        id<MTLBuffer>  buffers[4];
    };

    SpriteAtlas::SpriteAtlas(std::size_t n, void* device_handle)
    : _size(n)
    , _packer(n) {

        id<MTLDevice> device = (__bridge id<MTLDevice>)device_handle;

        _impl = new Impl;

        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm_sRGB;
        descriptor.width = n;
        descriptor.height = n;
        descriptor.usage = MTLTextureUsageShaderRead;

        // MTL asks for this but it may be an Apple Silicon thing?
        // Non-shared memory GPU: Managed?  Shared memory GPU: Shared?
        descriptor.storageMode = MTLStorageModeShared;

        _impl->texture = [device newTextureWithDescriptor:descriptor];
        _vertices.reserve(65536);
        for (auto& x : _impl->buffers) {
            x = [device newBufferWithLength:sizeof(SpriteVertex) * _vertices.capacity()
                                    options:MTLResourceStorageModeShared];
        }
    }

    SpriteAtlas::~SpriteAtlas() {
        // Deleting the Impl releases the ARC-strong id<MTL...> ivars.
        delete _impl;
    }

    void SpriteAtlas::commit(void* encoder_handle) {
        id<MTLRenderCommandEncoder> renderEncoder =
            (__bridge id<MTLRenderCommandEncoder>)encoder_handle;

        assert(_impl->buffers[0].length >= _vertices.size() * sizeof(SpriteVertex));
        std::memcpy(_impl->buffers[0].contents,
                    _vertices.data(),
                    _vertices.size() * sizeof(SpriteVertex));
        [renderEncoder setVertexBuffer:_impl->buffers[0]
                                offset:0
                               atIndex:AAPLBufferIndexVertices];
        [renderEncoder setFragmentTexture:_impl->texture
                                  atIndex:AAPLTextureIndexColor];
        [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                          vertexStart:0
                          vertexCount:_vertices.size()];
        _vertices.clear();
        std::rotate(_impl->buffers, _impl->buffers + 1, _impl->buffers + 4);
    }

    // Place a sprite within the free space of the atlas.
    Sprite SpriteAtlas::place(matrix_view<const RGBA8Unorm_sRGB> v, float2 origin) {
        auto tl = _packer.place(simd::make<simd::ulong2>(v.major(),
                                                 v.minor()));
        if (v.major() != 0) {
            [_impl->texture replaceRegion:MTLRegionMake2D(tl.x, tl.y,
                                                          v.major(), v.minor())
                              mipmapLevel:0
                                withBytes:v.data()
                              bytesPerRow:v.major_bytes()];
        }
        Sprite s;
        s.a.position = make<float4>(-origin, 0, 1);
        s.a.texCoord = convert<float>(tl) / (float) _size;
        s.b.position = make<float4>(v.major() - origin.x,
                                        v.minor() - origin.y,
                                        0,
                                        1);
        s.b.texCoord = make<float2>(tl.x + v.major(), tl.y + v.minor()) / _size;

        // for debug, also garbage_collected_shade the split regions

        /*
        {
            for (auto&& a : _packer._last_split) {
                auto n = max(a.width(), a.height());
                RGBA8Unorm_sRGB p(0.0f,0.0f,0.0f,0.25f);
                Array<RGBA8Unorm_sRGB> b;
                b.resize(n, p);
                [_impl->texture replaceRegion:MTLRegionMake2D(a.a.x, a.a.y, a.width(), 1)
                                  mipmapLevel:0
                                    withBytes:b.data()
                                  bytesPerRow:a.width() * sizeof(RGBA8Unorm_sRGB)];
                [_impl->texture replaceRegion:MTLRegionMake2D(a.a.x, a.a.y, 1, a.height())
                                  mipmapLevel:0
                                    withBytes:b.data()
                                  bytesPerRow:sizeof(RGBA8Unorm_sRGB)];
                [_impl->texture replaceRegion:MTLRegionMake2D(a.a.x, a.a.y + a.height() - 1, a.width(), 1)
                                  mipmapLevel:0
                                    withBytes:b.data()
                                  bytesPerRow:a.width() * sizeof(RGBA8Unorm_sRGB)];
                [_impl->texture replaceRegion:MTLRegionMake2D(a.a.x + a.width() - 1, a.a.y, 1, a.height())
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
    // somewhere.

    void SpriteAtlas::release(Sprite s) {
        auto a = convert<ulong>(s.a.texCoord * _size);
        auto b = convert<ulong>(s.b.texCoord * _size);
        _packer.release(a, b);
    }

} // namespace wry
