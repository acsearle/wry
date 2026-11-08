//
//  WryRenderContext.mm
//  client
//
//  Created by Antony Searle on 2026-06-24.
//

// Shared Metal header first for no contamination
#include <cmath>
#include "ShaderTypes.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <MetalKit/MetalKit.h>

#include "WryRenderContext.h"

#include "SpriteAtlas.hpp"
#include "font.hpp"
#include "image.hpp"
#include "platform.hpp"
#include "vertex.hpp"

@implementation WryRenderContext
{
    // C++ ivars for the shared 2D services.  The id<MTL*> / scalar
    // properties auto-synthesize their backing ivars; these don't, so they
    // are declared explicitly.  (_atlas / _font also serve as the backing
    // ivars for the same-named readonly properties.)
    wry::SpriteAtlas* _atlas;
    wry::Font* _font;
    wry::Font2 _font2;
    // Single-attachment 2D pipeline with STRAIGHT (non-premultiplied) alpha
    // blending, for backdrop images drawn via -drawImage:... (fade + pan).
    id<MTLRenderPipelineState> _imageRenderPipelineState;
    // mip::downsample_kernel, made on first use
    id<MTLComputePipelineState> _mipPipelineState;
}

- (nonnull instancetype)initWithDevice:(nonnull id<MTLDevice>)device
                   drawablePixelFormat:(MTLPixelFormat)drawablePixelFormat
{
    using namespace ::wry;
    using namespace ::simd;

    if ((self = [super init])) {

        _device = device;
        _drawablePixelFormat = drawablePixelFormat;
        _library = [_device newDefaultLibrary];
        _commandQueue = [_device newCommandQueue];
        _arena = [[WryBufferArena alloc] initWithDevice:_device];

        // Make full-screen quad
        {
            float4 buffer[4] = {
                { -1.0f, -1.0f, 0.0f, 1.0f, },
                { -1.0f, +1.0f, 0.0f, 1.0f, },
                { +1.0f, -1.0f, 0.0f, 1.0f, },
                { +1.0f, +1.0f, 0.0f, 1.0f, },
            };
            _screenTriangleStripVertexBuffer = [_device newBufferWithBytes:buffer
                                                                    length:sizeof(buffer)
                                                                   options:MTLResourceStorageModeShared];
        }

        // Prepare depth-stencil states
        {
            MTLDepthStencilDescriptor* descriptor = [MTLDepthStencilDescriptor new];
            descriptor.depthCompareFunction = MTLCompareFunctionLess;
            descriptor.depthWriteEnabled = YES;
            descriptor.label = @"Enabled depth test";
            _enabledDepthStencilState = [_device newDepthStencilStateWithDescriptor:descriptor];
            descriptor.depthCompareFunction = MTLCompareFunctionAlways;
            descriptor.depthWriteEnabled = NO;
            descriptor.label = @"Disabled depth test";
            _disabledDepthStencilState = [_device newDepthStencilStateWithDescriptor:descriptor];
        }

        // Shared overlay / 2D compositing pipeline
        {
            MTLRenderPipelineDescriptor *renderPipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];

            renderPipelineDescriptor.label                           = @"MyPipeline";

            renderPipelineDescriptor.vertexFunction                  = [self newFunctionWithName:@"vertexShader4"];
            renderPipelineDescriptor.vertexBuffers[0].mutability = MTLMutabilityImmutable;

            renderPipelineDescriptor.fragmentFunction                =  [self newFunctionWithName:@"fragmentShader"];
            renderPipelineDescriptor.fragmentBuffers[0].mutability = MTLMutabilityImmutable;

            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].pixelFormat = drawablePixelFormat;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].blendingEnabled = YES;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].rgbBlendOperation = MTLBlendOperationAdd;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].alphaBlendOperation = MTLBlendOperationAdd;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].sourceRGBBlendFactor = MTLBlendFactorOne;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].sourceAlphaBlendFactor = MTLBlendFactorOne;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            // The overlay draws in the world's second (composite) pass,
            // which binds only the two light attachments and the depth
            // buffer; the pipeline must declare exactly those
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexAmbient].pixelFormat = MTLPixelFormatRGBA16Float;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexAmbient].writeMask = MTLColorWriteMaskNone;
            renderPipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

            _overlayRenderPipelineState = [self newRenderPipelineStateWithDescriptor:renderPipelineDescriptor];

            // GUI quads (SpriteAtlas::commit) inside the world's composite
            // pass: same attachments and blending, instanced quad shaders.
            {
                MTLRenderPipelineDescriptor* quad = [renderPipelineDescriptor copy];
                quad.label = @"Overlay GUI quad pipeline";
                quad.vertexFunction = [self newFunctionWithName:@"quadVertexShader"];
                quad.fragmentFunction = [self newFunctionWithName:@"quadFragmentShader"];
                _overlayQuadRenderPipelineState = [self newRenderPipelineStateWithDescriptor:quad];
            }

            // GUI quads (SpriteAtlas::commit) for scenes that draw directly
            // into a single RGBA16Float color target (menu), rather than
            // into the world's deferred G-buffer pass.  Only
            // colorAttachments[Color] is declared, so it matches a
            // one-attachment, depth-less render pass.  (quadFragmentShader
            // writes only color(AAPLColorIndexColor).)
            {
                MTLRenderPipelineDescriptor* ui = [[MTLRenderPipelineDescriptor alloc] init];
                ui.label = @"UI GUI quad pipeline";
                ui.vertexFunction = [self newFunctionWithName:@"quadVertexShader"];
                ui.vertexBuffers[0].mutability = MTLMutabilityImmutable;
                ui.fragmentFunction = [self newFunctionWithName:@"quadFragmentShader"];
                ui.fragmentBuffers[0].mutability = MTLMutabilityImmutable;
                ui.colorAttachments[AAPLColorIndexColor].pixelFormat = drawablePixelFormat;
                ui.colorAttachments[AAPLColorIndexColor].blendingEnabled = YES;
                ui.colorAttachments[AAPLColorIndexColor].rgbBlendOperation = MTLBlendOperationAdd;
                ui.colorAttachments[AAPLColorIndexColor].alphaBlendOperation = MTLBlendOperationAdd;
                ui.colorAttachments[AAPLColorIndexColor].sourceRGBBlendFactor = MTLBlendFactorOne;
                ui.colorAttachments[AAPLColorIndexColor].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                ui.colorAttachments[AAPLColorIndexColor].sourceAlphaBlendFactor = MTLBlendFactorOne;
                ui.colorAttachments[AAPLColorIndexColor].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                _uiQuadRenderPipelineState = [self newRenderPipelineStateWithDescriptor:ui];
            }

            // Backdrop image pipeline: same shaders as the UI pipeline but with
            // STRAIGHT alpha blending (src*srcAlpha + dst*(1-srcAlpha)), so a
            // white vertex tint with alpha cleanly fades / crossfades an opaque
            // image over whatever is already in the target.
            {
                MTLRenderPipelineDescriptor* img = [[MTLRenderPipelineDescriptor alloc] init];
                img.label = @"Backdrop image pipeline";
                img.vertexFunction = [self newFunctionWithName:@"vertexShader4"];
                img.vertexBuffers[0].mutability = MTLMutabilityImmutable;
                img.fragmentFunction = [self newFunctionWithName:@"imageFragmentShader"];
                img.fragmentBuffers[0].mutability = MTLMutabilityImmutable;
                img.colorAttachments[AAPLColorIndexColor].pixelFormat = drawablePixelFormat;
                img.colorAttachments[AAPLColorIndexColor].blendingEnabled = YES;
                img.colorAttachments[AAPLColorIndexColor].rgbBlendOperation = MTLBlendOperationAdd;
                img.colorAttachments[AAPLColorIndexColor].alphaBlendOperation = MTLBlendOperationAdd;
                img.colorAttachments[AAPLColorIndexColor].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
                img.colorAttachments[AAPLColorIndexColor].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                img.colorAttachments[AAPLColorIndexColor].sourceAlphaBlendFactor = MTLBlendFactorOne;
                img.colorAttachments[AAPLColorIndexColor].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                _imageRenderPipelineState = [self newRenderPipelineStateWithDescriptor:img];
            }

            _atlas = new wry::SpriteAtlas(2048, (__bridge void*)device,
                                          (__bridge void*)_arena);
            _font = new wry::Font(build_font(*_atlas, 2.0f));

            _font2 = build_font2();
        }

    }
    return self;
}

- (wry::Font2&)font2 {
    return _font2;
}

- (void)setFontPixelsPerPoint:(float)pixelsPerPoint {
    if (!std::isfinite(pixelsPerPoint))
        return;
    const float clamped = std::clamp(pixelsPerPoint, 0.25f,
                                     wry::Font::max_pixels_per_point);
    if (clamped == _font->pixels_per_point)
        return;
    // The rebuild rewrites the atlas's font texels in place (replaceRegion),
    // which frames already committed may still be sampling.  Wait for them:
    // an empty command buffer on the same queue completes after every
    // buffer committed before it (Metal runs one queue's buffers in commit
    // order; the transient arena relies on the same, WryBufferArena.mm).  A blocking wait of a frame or two, once
    // per scale change.
    id<MTLCommandBuffer> drain = [_commandQueue commandBuffer];
    drain.label = @"Drain before font rebuild";
    [drain commit];
    [drain waitUntilCompleted];
    *_font = wry::build_font(*_atlas, clamped);
}

// ---- Factory helpers ---------------------------------------------------

- (id<MTLFunction>)newFunctionWithName:(NSString*)name
{
    id <MTLFunction> function = [_library newFunctionWithName:name];
    if (!function) {
        NSLog(@"ERROR: newFunctionWithName:@\"%@\"", name);
        abort();
    }
    function.label = name;
    return function;
}

- (id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor*)descriptor
{
    id<MTLRenderPipelineState> state = nil;
    NSError* error = nil;
    state = [_device newRenderPipelineStateWithDescriptor:descriptor
                                                    error:&error];
    if (!state) {
        NSLog(@"ERROR: Failed aquiring pipeline state: %@", error);
        abort();
    }
    return state;
}

- (id<MTLRenderPipelineState>)newRenderPipelineStateWithMeshDescriptor:(MTLMeshRenderPipelineDescriptor*)descriptor
{
    id<MTLRenderPipelineState> state = nil;
    NSError* error = nil;
    state =  [_device newRenderPipelineStateWithMeshDescriptor:descriptor
                                                       options:MTLPipelineOptionNone
                                                    reflection:nil
                                                         error:&error];
    if (!state) {
        NSLog(@"ERROR: Failed aquiring pipeline state: %@", error);
        abort();
    }
    return state;
}

- (id<MTLTexture>)newTextureFromResource:(NSString*)name
                                  ofType:(NSString*)ext
{
    return [self newTextureFromResource:name ofType:ext withPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB];
}

- (id<MTLTexture>)newTextureFromResource:(NSString*)name
                                  ofType:(NSString*)ext
                         withPixelFormat:(MTLPixelFormat)pixelFormat
{
    return [self newTextureFromResource:name ofType:ext withPixelFormat:pixelFormat mipmapped:NO];
}

// Load `name.ext` into level 0 of a new shared texture, with either one
// level or storage for the full mip chain (left unfilled).
//
// History: an earlier loader allocated a mip chain and encoded
// generateMipmaps + optimizeContentsForGPUAccess but never committed the
// blit, so the mips were never filled.  Committing it as-is would have been
// a trap: some textures (_symbols) are CPU-edited by the caller right after
// loading, and an in-flight blit (let alone optimizeContentsForGPUAccess)
// would race those getBytes / replaceRegion calls.  So mip generation is
// the caller's explicit, blocking step after its last level-0 edit.
- (id<MTLTexture>)newTextureFromResource:(NSString*)name
                                  ofType:(NSString*)ext
                         withPixelFormat:(MTLPixelFormat)pixelFormat
                               mipmapped:(BOOL)mipmapped
{
    return [self newTextureFromResource:name
                                 ofType:ext
                        withPixelFormat:pixelFormat
                              mipmapped:mipmapped
                          premultiplied:YES];
}

- (id<MTLTexture>)newTextureFromResource:(NSString*)name
                                  ofType:(NSString*)ext
                         withPixelFormat:(MTLPixelFormat)pixelFormat
                               mipmapped:(BOOL)mipmapped
                           premultiplied:(BOOL)premultiplied
{
    wry::matrix<wry::RGBA8Unorm_sRGB> image = wry::from_png(wry::path_for_resource([name UTF8String], [ext UTF8String]).c_str());
    // PNGs are straight alpha; the 2D pipelines blend premultiplied, so
    // the usual load multiplies through.  The symbol atlas is kept
    // straight (premultiplied:NO): its transparent margins carry the
    // colour the slabs' walls read (Shaders.metal symbol_march), and the
    // 2D draws of it premultiply in the shader (WryQuadFlagStraight)
    if (premultiplied)
        wry::multiply_alpha_inplace(image);

    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
    descriptor.textureType = MTLTextureType2D;
    descriptor.pixelFormat = pixelFormat;
    descriptor.width = image.major();
    descriptor.height = image.minor();
    descriptor.mipmapLevelCount = mipmapped
        ? 1 + (NSUInteger)floor(log2((double)std::max(image.major(), image.minor())))
        : 1;
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageShaderRead;
    // For -generateMipmapsForTexture:cellSize:straightAlpha:, which writes
    // the levels from a compute kernel
    if (mipmapped)
        descriptor.usage |= MTLTextureUsageShaderWrite;

    id<MTLTexture> texture = [_device newTextureWithDescriptor:descriptor];
    [texture replaceRegion:MTLRegionMake2D(0, 0, image.major(), image.minor())
               mipmapLevel:0
                 withBytes:image.data()
               bytesPerRow:image.stride_bytes()];
    texture.label = name;

    return texture;
}

- (id<MTLTexture>)newMipmappedTextureFromResource:(NSString*)name
                                           ofType:(NSString*)ext
                                  withPixelFormat:(MTLPixelFormat)pixelFormat
{
    // Only for textures that are never CPU-mutated after load; the returned
    // texture is complete and immutable.
    id<MTLTexture> texture = [self newTextureFromResource:name
                                                   ofType:ext
                                          withPixelFormat:pixelFormat
                                                mipmapped:YES];
    [self generateMipmapsForTexture:texture];
    return texture;
}

- (id<MTLTexture>)newMipmappedTextureFromResource:(NSString*)name
                                           ofType:(NSString*)ext
                                  withPixelFormat:(MTLPixelFormat)pixelFormat
                                         cellSize:(NSUInteger)cell
{
    id<MTLTexture> texture = [self newTextureFromResource:name
                                                   ofType:ext
                                          withPixelFormat:pixelFormat
                                                mipmapped:YES];
    [self generateMipmapsForTexture:texture cellSize:cell straightAlpha:NO];
    return texture;
}

// The symbol atlas: mip storage for the renderer to fill once level 0 is
// final, and straight alpha (see above)
- (id<MTLTexture>)newTextureWithMipStorageFromResource:(NSString*)name
                                                ofType:(NSString*)ext
{
    return [self newTextureFromResource:name
                                 ofType:ext
                        withPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB
                              mipmapped:YES
                          premultiplied:NO];
}

- (void)generateMipmapsForTexture:(id<MTLTexture>)texture
{
    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    id<MTLBlitCommandEncoder> encoder = [commandBuffer blitCommandEncoder];
    [encoder generateMipmapsForTexture:texture];
    [encoder endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
}

- (void)generateMipmapsForTexture:(id<MTLTexture>)texture
                         cellSize:(NSUInteger)cell
                    straightAlpha:(BOOL)straightAlpha
{
    auto power_of_two = [](NSUInteger n) { return n && !(n & (n - 1)); };
    const MTLPixelFormat format = texture.pixelFormat;
    const bool srgb = format == MTLPixelFormatRGBA8Unorm_sRGB;
    // Shader writes to an sRGB texture need an Apple GPU
    if (!(format == MTLPixelFormatRGBA8Unorm || srgb)
        || (srgb && ![_device supportsFamily:MTLGPUFamilyApple2])
        || !power_of_two(texture.width) || !power_of_two(texture.height)
        || !(texture.usage & MTLTextureUsageShaderWrite)
        || texture.textureType != MTLTextureType2D) {
        NSLog(@"WARNING: %@: box-filtered mips (no kernel path for this texture)", texture.label);
        [self generateMipmapsForTexture:texture];
        return;
    }
    if (!_mipPipelineState) {
        NSError* error = nil;
        _mipPipelineState = [_device newComputePipelineStateWithFunction:[self newFunctionWithName:@"mip::downsample_kernel"]
                                                                   error:&error];
        if (!_mipPipelineState) {
            NSLog(@"ERROR: mip::downsample_kernel: %@", error);
            abort();
        }
    }

    WryMipUniforms uniforms = {};
    // Binomial (1 + z)^3 / 8: see the header.  Compared at zoom 50-70%
    // (2026-11-08) against the box and (1 + z)^5 / 32: the 6-tap smooths
    // the aliasing equally but visibly softens the small tile captions
    const float taps[] = { 1.0f, 3.0f, 3.0f, 1.0f };
    static_assert(sizeof(taps) / sizeof(*taps) <= WRY_MIP_MAX_TAPS);
    uniforms.count = (int)(sizeof(taps) / sizeof(*taps));
    for (int k = 0; k != uniforms.count; ++k)
        uniforms.taps[k] = taps[k] / 8.0f;
    uniforms.straight = straightAlpha ? 1 : 0;
    uniforms.epsilon = 1.0f / 256.0f;

    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
    encoder.label = @"Mip chain";
    [encoder setComputePipelineState:_mipPipelineState];
    for (NSUInteger level = 1; level < texture.mipmapLevelCount; ++level) {
        id<MTLTexture> source = [texture newTextureViewWithPixelFormat:format
                                                           textureType:MTLTextureType2D
                                                                levels:NSMakeRange(level - 1, 1)
                                                                slices:NSMakeRange(0, 1)];
        id<MTLTexture> destination = [texture newTextureViewWithPixelFormat:format
                                                                textureType:MTLTextureType2D
                                                                     levels:NSMakeRange(level, 1)
                                                                     slices:NSMakeRange(0, 1)];
        const NSUInteger c = cell >> (level - 1);
        uniforms.cell = (cell && c >= 2) ? (int)c : 0;
        [encoder setTexture:source atIndex:0];
        [encoder setTexture:destination atIndex:1];
        [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:0];
        [encoder dispatchThreadgroups:MTLSizeMake((destination.width + WRY_MIP_TILE - 1) / WRY_MIP_TILE,
                                                  (destination.height + WRY_MIP_TILE - 1) / WRY_MIP_TILE,
                                                  1)
                threadsPerThreadgroup:MTLSizeMake(WRY_MIP_TILE, WRY_MIP_TILE, 1)];
        // The next level reads this one
        [encoder memoryBarrierWithScope:MTLBarrierScopeTextures];
    }
    [encoder endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
}

// ---- Scene backdrop helpers --------------------------------------------

- (NSArray<NSString*>*)textureStemsWithPrefix:(NSString*)prefix
                                       ofType:(NSString*)ext
{
    const std::string pfx = prefix.UTF8String;
    const std::string dotext = std::string(".") + ext.UTF8String;

    // Resources resolve relative to the working directory (the assets folder;
    // see main.mm).  Collect matching stems, sorted, so a stable cycle order.
    std::vector<std::string> stems;
    try {
        for (auto const& entry :
             std::filesystem::directory_iterator(std::filesystem::current_path())) {
            const auto& p = entry.path();
            if (p.extension() != dotext)
                continue;
            const std::string fname = p.filename().string();
            if (fname.size() >= pfx.size() && fname.compare(0, pfx.size(), pfx) == 0)
                stems.push_back(p.stem().string());
        }
    } catch (...) {
        // Missing / unreadable directory: return whatever we have (maybe none).
    }
    std::sort(stems.begin(), stems.end());

    NSMutableArray<NSString*>* out =
        [NSMutableArray arrayWithCapacity:stems.size()];
    for (auto const& s : stems)
        [out addObject:@(s.c_str())];
    return out;
}

- (NSArray<id<MTLTexture>>*)loadTexturesWithPrefix:(NSString*)prefix
                                            ofType:(NSString*)ext
{
    NSArray<NSString*>* stems = [self textureStemsWithPrefix:prefix ofType:ext];
    NSMutableArray<id<MTLTexture>>* out =
        [NSMutableArray arrayWithCapacity:stems.count];
    for (NSString* s in stems)
        [out addObject:[self newTextureFromResource:s ofType:ext]];
    return out;
}

- (void)drawImage:(id<MTLTexture>)texture
           window:(simd_float4)window
            alpha:(float)alpha
         viewport:(simd_float2)viewportPx
      withEncoder:(id<MTLRenderCommandEncoder>)encoder
{
    [encoder setRenderPipelineState:_imageRenderPipelineState];

    // Pixel coords -> NDC, y-down (same transform the UI / overlay use).
    MyUniforms uniforms;
    uniforms.position_transform = matrix_float4x4{{
        {2.0f / viewportPx.x, 0.0f, 0.0f},
        {0.0f, -2.0f / viewportPx.y, 0.0f, 0.0f},
        { 0.0f, 0.0f, 1.0f, 0.0f },
        {-1.0f, +1.0f, 0.0f, 1.0f},
    }};
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:AAPLBufferIndexUniforms];

    // Full-viewport quad; texCoords are the supplied (u0,v0)-(u1,v1) window.
    const float w = viewportPx.x, h = viewportPx.y;
    const float u0 = window.x, v0 = window.y, u1 = window.z, v1 = window.w;
    const wry::RGBA8Unorm_sRGB tint(1.0f, 1.0f, 1.0f, alpha);
    auto vert = [&](float px, float py, float u, float v) {
        wry::SpriteVertex sv;
        sv.v.position = simd_make_float4(px, py, 0.0f, 1.0f);
        sv.v.texCoord = simd_make_float2(u, v);
        sv.color = tint;
        return sv;
    };
    const wry::SpriteVertex quad[6] = {
        vert(0.0f, 0.0f, u0, v0),
        vert(w,    0.0f, u1, v0),
        vert(w,    h,    u1, v1),
        vert(0.0f, 0.0f, u0, v0),
        vert(w,    h,    u1, v1),
        vert(0.0f, h,    u0, v1),
    };
    id<MTLBuffer> vb = [_device newBufferWithBytes:quad
                                            length:sizeof(quad)
                                           options:MTLStorageModeShared];
    [encoder setVertexBuffer:vb offset:0 atIndex:AAPLBufferIndexVertices];
    [encoder setFragmentTexture:texture atIndex:AAPLTextureIndexColor];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
}

@end
