//
//  WryRenderContext.mm
//  client
//
//  Created by Antony Searle on 2026-06-24.
//

// Shared Metal header first for no contamination
#include "ShaderTypes.h"

#include <bit>

#include <MetalKit/MetalKit.h>

#include "WryRenderContext.h"

#include "SpriteAtlas.hpp"
#include "font.hpp"
#include "image.hpp"
#include "platform.hpp"

@implementation WryRenderContext
{
    // C++ ivars for the shared 2D services.  The id<MTL*> / scalar
    // properties auto-synthesize their backing ivars; these don't, so they
    // are declared explicitly.  (_atlas / _font also serve as the backing
    // ivars for the same-named readonly properties.)
    wry::SpriteAtlas* _atlas;
    wry::Font* _font;
    wry::Font2 _font2;
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
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].pixelFormat = MTLPixelFormatRGBA16Float;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexNormalRoughness].pixelFormat = MTLPixelFormatRGBA16Float;
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
    descriptor.mipmapLevelCount = std::countr_zero(descriptor.width | descriptor.height);
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageShaderRead;

    id<MTLTexture> texture = [_device newTextureWithDescriptor:descriptor];
    [texture replaceRegion:MTLRegionMake2D(0, 0, image.major(), image.minor())
               mipmapLevel:0
                 withBytes:image.data()
               bytesPerRow:image.stride_bytes()];
    texture.label = name;

    id<MTLCommandBuffer> buffer = [_commandQueue commandBuffer];

    id<MTLBlitCommandEncoder> encoder = [buffer blitCommandEncoder];
    [encoder generateMipmapsForTexture:texture];
    [encoder optimizeContentsForGPUAccess:texture];
    [encoder endEncoding];
    return texture;
}

@end
