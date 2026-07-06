//
//  WryRenderContext.mm
//  client
//
//  Created by Antony Searle on 2026-06-24.
//

// Shared Metal header first for no contamination
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
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexAlbedo].pixelFormat = MTLPixelFormatRGBA16Float;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexNormal].pixelFormat = MTLPixelFormatRGBA16Float;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexMaterial].pixelFormat = MTLPixelFormatRGBA8Unorm;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexDepth].pixelFormat = MTLPixelFormatR32Float;
            renderPipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

            _overlayRenderPipelineState = [self newRenderPipelineStateWithDescriptor:renderPipelineDescriptor];

            // A 2D UI pipeline for scenes that draw directly into a single
            // RGBA16Float color target (splash, menu), rather than into the
            // world's deferred G-buffer pass.  Same shaders as the overlay
            // pipeline, but only colorAttachments[Color] is declared, so it
            // matches a one-attachment, depth-less render pass.  (fragmentShader
            // writes only color(AAPLColorIndexColor), so the other G-buffer
            // attachments aren't needed here.)
            {
                MTLRenderPipelineDescriptor* ui = [[MTLRenderPipelineDescriptor alloc] init];
                ui.label = @"UI 2D pipeline";
                ui.vertexFunction = [self newFunctionWithName:@"vertexShader4"];
                ui.vertexBuffers[0].mutability = MTLMutabilityImmutable;
                ui.fragmentFunction = [self newFunctionWithName:@"fragmentShader"];
                ui.fragmentBuffers[0].mutability = MTLMutabilityImmutable;
                ui.colorAttachments[AAPLColorIndexColor].pixelFormat = drawablePixelFormat;
                ui.colorAttachments[AAPLColorIndexColor].blendingEnabled = YES;
                ui.colorAttachments[AAPLColorIndexColor].rgbBlendOperation = MTLBlendOperationAdd;
                ui.colorAttachments[AAPLColorIndexColor].alphaBlendOperation = MTLBlendOperationAdd;
                ui.colorAttachments[AAPLColorIndexColor].sourceRGBBlendFactor = MTLBlendFactorOne;
                ui.colorAttachments[AAPLColorIndexColor].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                ui.colorAttachments[AAPLColorIndexColor].sourceAlphaBlendFactor = MTLBlendFactorOne;
                ui.colorAttachments[AAPLColorIndexColor].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                _uiRenderPipelineState = [self newRenderPipelineStateWithDescriptor:ui];
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

            _atlas = new wry::SpriteAtlas(2048, (__bridge void*)device);
            _font = new wry::Font(build_font(*_atlas));

            _font2 = build_font2();
        }

    }
    return self;
}

- (wry::Font2&)font2 {
    return _font2;
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
    wry::matrix<wry::RGBA8Unorm_sRGB> image = wry::from_png(wry::path_for_resource([name UTF8String], [ext UTF8String]).c_str());
    wry::multiply_alpha_inplace(image);

    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
    descriptor.textureType = MTLTextureType2D;
    descriptor.pixelFormat = pixelFormat;
    descriptor.width = image.major();
    descriptor.height = image.minor();
    // Single level: every current sampler reads the base level (mip_filter::none),
    // so we don't generate mipmaps.  An earlier version allocated a mip chain and
    // encoded generateMipmaps + optimizeContentsForGPUAccess but never committed
    // the blit, so the mips were never filled.  Committing it as-is would be a
    // trap: some textures (e.g. _symbols / "assets.png") are CPU-mutated by the
    // caller right after loading, and optimizeContentsForGPUAccess on an
    // async-in-flight texture would race those getBytes/replaceRegion calls.
    // If a future sampler wants mips (e.g. trilinear for a minified backdrop),
    // add an opt-in mipmapped path that excludes CPU-mutated textures and that
    // commits its own blit.
    descriptor.mipmapLevelCount = 1;
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageShaderRead;

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
    // The opt-in mipmapped path anticipated by the comment in
    // newTextureFromResource:.  Only for textures that are never
    // CPU-mutated after load; the blit is committed and waited on here,
    // so the returned texture is complete and immutable.
    wry::matrix<wry::RGBA8Unorm_sRGB> image = wry::from_png(wry::path_for_resource([name UTF8String], [ext UTF8String]).c_str());
    wry::multiply_alpha_inplace(image);

    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
    descriptor.textureType = MTLTextureType2D;
    descriptor.pixelFormat = pixelFormat;
    descriptor.width = image.major();
    descriptor.height = image.minor();
    descriptor.mipmapLevelCount = 1 + (NSUInteger)floor(log2((double)std::max(image.major(), image.minor())));
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageShaderRead;

    id<MTLTexture> texture = [_device newTextureWithDescriptor:descriptor];
    [texture replaceRegion:MTLRegionMake2D(0, 0, image.major(), image.minor())
               mipmapLevel:0
                 withBytes:image.data()
               bytesPerRow:image.stride_bytes()];
    texture.label = name;

    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    id<MTLBlitCommandEncoder> encoder = [commandBuffer blitCommandEncoder];
    [encoder generateMipmapsForTexture:texture];
    [encoder endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];

    return texture;
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
