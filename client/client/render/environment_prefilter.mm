//
//  environment_prefilter.mm
//  client
//
//  The GGX-prefiltered sky cube; see environment_prefilter.hpp.
//

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>

#include <simd/simd.h>

#include "environment_prefilter.hpp"

#include "ShaderTypes.h"
#include "kqueue_reactor.hpp"
#include "metal_event.hpp"
#include "simd.hpp"
#include "test.hpp"

namespace wry {

    id<MTLTexture>
    prefilter_environment(id<MTLDevice> device,
                          id<MTLLibrary> library,
                          id<MTLCommandQueue> queue,
                          id<MTLTexture> input,
                          NSString* label) {

        using namespace simd;

        id<MTLTexture> output = nil;
        {
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
            descriptor.textureType = MTLTextureTypeCube;
            descriptor.width = kEnvironmentPrefilterSize;
            descriptor.height = descriptor.width;
            descriptor.pixelFormat = MTLPixelFormatRGBA32Float;
            descriptor.mipmapLevelCount = kEnvironmentPrefilterLevels;
            descriptor.usage = MTLTextureUsageShaderRead;
            descriptor.resourceOptions = MTLResourceStorageModePrivate;
            output = [device newTextureWithDescriptor:descriptor];
            output.label = label;
        }

        id<MTLRenderPipelineState> pipeline = nil;
        {
            MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
            descriptor.label = @"CubeFilter3";
            descriptor.vertexFunction = [library newFunctionWithName:@"CubeFilterVertex"];
            descriptor.fragmentFunction = [library newFunctionWithName:@"CubeFilterAccumulate3"];
            descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
            descriptor.colorAttachments[0].blendingEnabled = YES;
            descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
            descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
            descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
            descriptor.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
            NSError* error = nil;
            pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
            if (!pipeline) {
                NSLog(@"ERROR: Failed aquiring pipeline state: %@", error);
                abort();
            }
        }

        // The full-screen triangle strip in NDC, XY01
        id<MTLBuffer> strip = nil;
        {
            float4 vertices[4] = {
                { -1.0f, -1.0f, 0.0f, 1.0f, },
                { -1.0f, +1.0f, 0.0f, 1.0f, },
                { +1.0f, -1.0f, 0.0f, 1.0f, },
                { +1.0f, +1.0f, 0.0f, 1.0f, },
            };
            strip = [device newBufferWithBytes:vertices
                                        length:sizeof(vertices)
                                       options:MTLResourceStorageModeShared];
        }

        CubeFilterUniforms uniforms;
        {
            // we have position in NDCs with XY01

            // translate z=0 near clip plane cordinates to z=1
            simd_float4x4 Z
            = simd_matrix(simd_make_float4(1.0f, 0.0f, 0.0f, 0.0f),
                          simd_make_float4(0.0f, 1.0f, 0.0f, 0.0f),
                          simd_make_float4(0.0f, 0.0f, 1.0f, 0.0f),
                          simd_make_float4(0.0f, 0.0f, 1.0f, 1.0f));
            uniforms.transforms[0]
            = simd_mul(simd_matrix_rotate(M_PI_2, simd_make_float3(0.0f, 1.0f, 0.0f)), Z);
            uniforms.transforms[1]
            = simd_mul(simd_matrix_rotate(-M_PI_2, simd_make_float3(0.0f, 1.0f, 0.0f)), Z);
            uniforms.transforms[2]
            = simd_mul(simd_matrix_rotate(M_PI_2, simd_make_float3(-1.0f, 0.0f, 0.0f)), Z);
            uniforms.transforms[3]
            = simd_mul(simd_matrix_rotate(-M_PI_2, simd_make_float3(-1.0f, 0.0f, 0.0f)), Z);
            uniforms.transforms[4] = Z;
            uniforms.transforms[5]
            = simd_mul(simd_matrix_rotate(M_PI, simd_make_float3(0.0f, 1.0f, 0.0f)), Z);
        }

        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        commandBuffer.label = @"Environment prefilter";

        for (NSUInteger level = 0; level != output.mipmapLevelCount; ++level) {

            id<MTLTexture> target;
            {
                MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
                descriptor.textureType = MTLTextureTypeCube;
                descriptor.pixelFormat = MTLPixelFormatRGBA32Float;
                descriptor.width = output.width >> level;
                descriptor.height = output.height >> level;
                descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
                descriptor.resourceOptions = MTLResourceStorageModePrivate;
                target = [device newTextureWithDescriptor:descriptor];
                target.label = @"Cube filter accumulate";
            }

            MTLRenderPassDescriptor* renderPass = [MTLRenderPassDescriptor new];
            renderPass.colorAttachments[0].texture = target;
            renderPass.colorAttachments[0].loadAction = MTLLoadActionClear;
            renderPass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
            renderPass.colorAttachments[0].storeAction = MTLStoreActionStore;
            renderPass.renderTargetArrayLength = 6;

            {
                float roughness = std::exp2(1.0f + (float)level - (float)output.mipmapLevelCount);
                float alpha = roughness * roughness;
                uniforms.alpha2 = alpha * alpha;

                id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPass];

                [encoder setRenderPipelineState:pipeline];
                [encoder setVertexBuffer:strip
                                  offset:0
                                 atIndex:AAPLBufferIndexVertices];
                [encoder setVertexBytes:&uniforms
                                 length:sizeof(CubeFilterUniforms)
                                atIndex:AAPLBufferIndexUniforms];
                [encoder setFragmentBytes:&uniforms
                                   length:sizeof(CubeFilterUniforms)
                                  atIndex:AAPLBufferIndexUniforms];
                [encoder setFragmentTexture:input
                                    atIndex:AAPLTextureIndexColor];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                            vertexStart:0
                            vertexCount:4
                          instanceCount:6];

                [encoder endEncoding];
            }

            {
                id<MTLBlitCommandEncoder> encoder = [commandBuffer blitCommandEncoder];

                [encoder copyFromTexture:target
                             sourceSlice:0
                             sourceLevel:0
                               toTexture:output
                        destinationSlice:0
                        destinationLevel:level
                              sliceCount:6
                              levelCount:1 ];
                [encoder optimizeContentsForGPUAccess:output];

                [encoder endEncoding];
            }

        } // end loop over mip-map levels

        wry::abort_on_command_buffer_error(commandBuffer);
        [commandBuffer commit];

        return output;

    }

    // ---- Tests ----------------------------------------------------------------

    namespace {

        // Every face of every level of `cube`, read back to the CPU: level
        // k is levels[k][face * w * w + row * w + column], w = size >> k
        coroutine::Future<std::vector<std::vector<simd_float4>>>
        read_back_cube(id<MTLDevice> device, id<MTLCommandQueue> queue, id<MTLTexture> cube) {
            using namespace std::chrono;
            id<MTLSharedEvent> event = [device newSharedEvent];
            id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
            std::vector<id<MTLBuffer>> buffers;
            for (NSUInteger level = 0; level != cube.mipmapLevelCount; ++level) {
                NSUInteger w = cube.width >> level;
                NSUInteger face_bytes = w * w * sizeof(simd_float4);
                id<MTLBuffer> buffer = [device newBufferWithLength:face_bytes * 6
                                                           options:MTLResourceStorageModeShared];
                for (NSUInteger face = 0; face != 6; ++face)
                    [blit copyFromTexture:cube
                              sourceSlice:face
                              sourceLevel:level
                             sourceOrigin:MTLOriginMake(0, 0, 0)
                               sourceSize:MTLSizeMake(w, w, 1)
                                 toBuffer:buffer
                        destinationOffset:face * face_bytes
                   destinationBytesPerRow:w * sizeof(simd_float4)
                 destinationBytesPerImage:face_bytes];
                buffers.push_back(buffer);
            }
            [blit endEncoding];
            [commandBuffer encodeSignalEvent:event value:1];
            wry::abort_on_command_buffer_error(commandBuffer);
            [commandBuffer commit];
            auto r = co_await with_deadline(steady_clock::now() + seconds(30),
                                            shared_event_wait(event, 1));
            if (!r.has_value())
                shared_event_abandon(event);   // retire the stopped wait
            assert(r.has_value());
            (void) r;
            std::vector<std::vector<simd_float4>> levels;
            for (NSUInteger level = 0; level != cube.mipmapLevelCount; ++level) {
                NSUInteger w = cube.width >> level;
                simd_float4 const* p = (simd_float4 const*)buffers[level].contents;
                levels.emplace_back(p, p + 6 * w * w);
            }
            co_return levels;
        }

    } // namespace

    // The prefilter of a synthetic sky: red = (1 + z) / 2 (a function of
    // the equirect's v alone, v = acos(z) / pi), green = 1/4, blue = 1 -
    // red.  The prefilter is a normalized weighted mean, so every texel of
    // every level must be finite, green must stay 1/4 and red + blue 1.
    // Level 0 (roughness 1/16) must approximate the source: its face means
    // of red, sorted (the test does not depend on the face order), match
    // the texel means of (1 + z) / 2 over a cube face, 1/2 on the four
    // sides and (1 +- m) / 2 on the poles, m the mean of 1 / sqrt(1 + x^2
    // + y^2) over the face's texel centres.  Regression: level 0 was NaN at
    // every texel (sample_Trowbridge_Reitz's sqrt(1 - cos^2) of a cos^2
    // rounded above 1 by fast-math division at alpha2 = 2^-16).
    define_test("environment_prefilter") {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        id<MTLLibrary> library = [device newDefaultLibrary];
        id<MTLCommandQueue> queue = [device newCommandQueue];

        constexpr NSUInteger W = 64, H = 256;
        id<MTLTexture> sky = nil;
        {
            MTLTextureDescriptor* descriptor
            = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                                 width:W
                                                                height:H
                                                             mipmapped:NO];
            descriptor.usage = MTLTextureUsageShaderRead;
            descriptor.storageMode = MTLStorageModeShared;
            sky = [device newTextureWithDescriptor:descriptor];
            std::vector<simd_float4> texels(W * H);
            for (NSUInteger row = 0; row != H; ++row) {
                float z = std::cos(M_PI * (row + 0.5) / H);
                float red = 0.5f + 0.5f * z;
                for (NSUInteger column = 0; column != W; ++column)
                    texels[row * W + column] = simd_make_float4(red, 0.25f, 1.0f - red, 1.0f);
            }
            [sky replaceRegion:MTLRegionMake2D(0, 0, W, H)
                   mipmapLevel:0
                     withBytes:texels.data()
                   bytesPerRow:W * sizeof(simd_float4)];
        }

        id<MTLTexture> cube = prefilter_environment(device, library, queue, sky, @"test sky");
        auto levels = co_await read_back_cube(device, queue, cube);
        require(levels.size() == kEnvironmentPrefilterLevels);

        for (size_t level = 0; level != levels.size(); ++level) {
            size_t nonfinite = 0;
            float worst_green = 0.0f, worst_sum = 0.0f;
            for (simd_float4 t : levels[level]) {
                if (!std::isfinite(t.x) || !std::isfinite(t.y) || !std::isfinite(t.z)) {
                    ++nonfinite;
                    continue;
                }
                worst_green = std::max(worst_green, std::abs(t.y - 0.25f));
                worst_sum = std::max(worst_sum, std::abs(t.x + t.z - 1.0f));
            }
            if (nonfinite || worst_green > 1e-4f || worst_sum > 1e-3f)
                printf("environment_prefilter: level %zu: %zu non-finite, |g - 1/4| <= %g, |r + b - 1| <= %g\n",
                       level, nonfinite, worst_green, worst_sum);
            require(nonfinite == 0);
            require(worst_green <= 1e-4f);
            require(worst_sum <= 1e-3f);   // fp32 sums of 16384 samples
        }

        // Level 0 against the source
        {
            constexpr NSUInteger w = kEnvironmentPrefilterSize;
            double m = 0.0;
            for (NSUInteger j = 0; j != w; ++j)
                for (NSUInteger i = 0; i != w; ++i) {
                    double x = (i + 0.5) * 2.0 / w - 1.0;
                    double y = (j + 0.5) * 2.0 / w - 1.0;
                    m += 1.0 / std::sqrt(1.0 + x * x + y * y);
                }
            m /= w * w;
            std::array<double, 6> expected = { 0.5 - 0.5 * m, 0.5, 0.5, 0.5, 0.5, 0.5 + 0.5 * m };
            std::array<double, 6> measured = {};
            for (size_t face = 0; face != 6; ++face) {
                for (size_t k = 0; k != w * w; ++k)
                    measured[face] += levels[0][face * w * w + k].x;
                measured[face] /= w * w;
            }
            std::sort(measured.begin(), measured.end());
            double worst = 0.0;
            for (size_t face = 0; face != 6; ++face)
                worst = std::max(worst, std::abs(measured[face] - expected[face]));
            if (worst > 2e-3)
                printf("environment_prefilter: level 0 face means of red off the source by %g "
                       "(poles %g %g, expected %g %g)\n",
                       worst, measured[0], measured[5], expected[0], expected[5]);
            require(worst <= 2e-3);
        }
    };

} // namespace wry
