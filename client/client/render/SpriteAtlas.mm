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
#import "WryBufferArena.h"
#include "debug.hpp"

namespace wry {

    // Definition of the opaque pimpl declared in SpriteAtlas.hpp.  Lives in
    // this Objective-C++ TU so its id<MTL...> ivars can be managed by ARC
    // (strong refs by default).  Pure C++ TUs only see the forward
    // declaration `struct Impl;` and never inspect this layout.
    struct SpriteAtlas::Impl {
        id<MTLTexture> texture;
        WryBufferArena* arena;
    };

    SpriteAtlas::SpriteAtlas(std::size_t n, void* device_handle, void* arena_handle)
    : _size(n)
    , _packer(n) {

        id<MTLDevice> device = (__bridge id<MTLDevice>)device_handle;

        _impl = new Impl;
        _impl->arena = (__bridge WryBufferArena*)arena_handle;

        MTLTextureDescriptor *descriptor = [[MTLTextureDescriptor alloc] init];
        descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm_sRGB;
        descriptor.width = n;
        descriptor.height = n;
        descriptor.usage = MTLTextureUsageShaderRead;

        // MTL asks for this but it may be an Apple Silicon thing?
        // Non-shared memory GPU: Managed?  Shared memory GPU: Shared?
        descriptor.storageMode = MTLStorageModeShared;

        _impl->texture = [device newTextureWithDescriptor:descriptor];
        _quads.reserve(16384);
    }

    SpriteAtlas::~SpriteAtlas() {
        // Deleting the Impl releases the ARC-strong id<MTL...> ivars.
        delete _impl;
    }

    void SpriteAtlas::commit(void* encoder_handle, void* pipeline_handle) {
        id<MTLRenderCommandEncoder> renderEncoder =
            (__bridge id<MTLRenderCommandEncoder>)encoder_handle;
        id<MTLRenderPipelineState> pipeline =
            (__bridge id<MTLRenderPipelineState>)pipeline_handle;

        [renderEncoder setRenderPipelineState:pipeline];
        if (_quads.empty())
            return;
        WryBufferSlice slice = [_impl->arena copyBytes:_quads.data()
                                                length:_quads.size() * sizeof(WryQuadInstance)];
        [renderEncoder setVertexBuffer:slice.buffer
                                offset:slice.offset
                               atIndex:AAPLBufferIndexVertices];
        [renderEncoder setFragmentTexture:_impl->texture
                                  atIndex:AAPLTextureIndexColor];
        [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                          vertexStart:0
                          vertexCount:4
                        instanceCount:_quads.size()];
        _quads.clear();
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
