//
//  WryRenderer.mm
//  client
//
//  Created by Antony Searle on 1/7/2023.
//

// Shared Metal header first for no contamination
#include "ShaderTypes.h"

#include <bit>
#include <random>

#include <MetalKit/MetalKit.h>
#include <MetalPerformanceShaders/MetalPerformanceShaders.h>

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"

#include "WryMesh.h"
#include "WryRenderer.h"

#include "atlas.hpp"
#include "csv.hpp"
#include "debug.hpp"
#include "font.hpp"
#include "json.hpp"
#include "mesh.hpp"
#include "palette.hpp"
#include "platform.hpp"
#include "sdf.hpp"
#include "Wavefront.hpp"

@implementation WryRenderer
{
    
    // link to rest of program
    
    std::shared_ptr<wry::model> _model;
    
    
    
    MTLPixelFormat _drawablePixelFormat;

    // view-only state
        
    size_t _frame_count;
            
    id<MTLBuffer> _screenTriangleStripVertexBuffer;
    id<MTLCommandQueue> _commandQueue;
    id<MTLDepthStencilState> _disabledDepthStencilState;
    id<MTLDepthStencilState> _enabledDepthStencilState;
    id<MTLDevice> _device;
    id<MTLLibrary> _library;
    
    // shadow map pass
    
    id<MTLRenderPipelineState> _shadowMapRenderPipelineState;
    id<MTLTexture> _shadowMapTarget;
    
    // deferred physically-based lighting render pass
    
    id <MTLRenderPipelineState> _deferredGBufferRenderPipelineState;
    id <MTLRenderPipelineState> _deferredLinesRenderPipelineState;
    id <MTLRenderPipelineState> _deferredPointsRenderPipelineState;

    id <MTLRenderPipelineState> _deferredLightImageBasedRenderPipelineState;
    id <MTLRenderPipelineState> _deferredLightDirectionalShadowcastingRenderPipelineState;
    id <MTLRenderPipelineState> _deferredLightPointRenderPipelineState;

    id<MTLTexture> _deferredLightColorAttachmentTexture;
    id<MTLTexture> _deferredAlbedoMetallicColorAttachmentTexture;
    id<MTLTexture> _deferredNormalRoughnessColorAttachmentTexture;
    id<MTLTexture> _deferredDepthColorAttachmentTexture;
    id<MTLTexture> _deferredDepthAttachmentTexture;
    
    id<MTLTexture> _deferredLightImageBasedTexture;
    id<MTLTexture> _deferredLightImageBasedFresnelTexture;
        
    // conventional compositing for overlay
    
    id <MTLRenderPipelineState> _overlayRenderPipelineState;
        
    wry::atlas* _atlas;
    wry::font* _font;
                    
    
    // bloom
    
    MPSImageGaussianBlur* _gaussianBlur;
    id<MTLTexture> _blurredTexture;
    MPSImageAdd* _imageAdd;
    id<MTLTexture> _addedTexture;

    // symbols?
    
    id<MTLTexture> _symbols;
    WryMesh* _hackmesh;
    
    id<MTLTexture> _black;
    id<MTLTexture> _white;
    id<MTLTexture> _blue;
    id<MTLBuffer> _instanced_things;
    id<MTLTexture> _darkgray;
    id<MTLTexture> _orange;

    wry::Table<ulong, simd_float4> _opcode_to_coordinate;
    
    WryMesh* _truck_mesh;
    WryMesh* _mine_mesh;
    WryMesh* _furnace_mesh;
    
    // controls
    
    wry::Palette<wry::sim::Value> _controls;

    NSCursor* _cursor;
    
    NSView* _view;
    
}


-(void) dealloc {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

-(id<MTLFunction>) newFunctionWithName:(NSString*)name
{
    id <MTLFunction> function = [_library newFunctionWithName:name];
    if (!function) {
        NSLog(@"ERROR: newFunctionWithName:@\"%@\"", name);
        exit(EXIT_FAILURE);
    }
    function.label = name;
    return function;
}

-(id<MTLRenderPipelineState>) newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor*)descriptor
{
    id<MTLRenderPipelineState> state = nil;
    NSError* error = nil;
    state = [_device newRenderPipelineStateWithDescriptor:descriptor
                                                    error:&error];
    if (!state) {
        NSLog(@"ERROR: Failed aquiring pipeline state: %@", error);
        exit(EXIT_FAILURE);
    }
    return state;
}

-(id<MTLTexture>)newTextureFromResource:(NSString*)name
                                 ofType:(NSString*)ext
{
    return [self newTextureFromResource:name ofType:ext withPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB];
}


-(id<MTLTexture>)newTextureFromResource:(NSString*)name
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
    
    [buffer commit];
    
    return texture;
}

-(id<MTLTexture>)prefilteredEnvironmentMapFromResource:(NSString*)name ofType:ext {
    
    using namespace simd;
    
    id<MTLTexture> input = [self newTextureFromResource:name ofType:ext];
    
    id<MTLTexture> output = nil;
    {
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
        descriptor.textureType = MTLTextureTypeCube;
        descriptor.width = 256;
        descriptor.height = descriptor.width;
        descriptor.pixelFormat = MTLPixelFormatRGBA32Float;
        descriptor.mipmapLevelCount = 5; //__builtin_ctzl(descriptor.width) + 1;
        descriptor.usage = MTLTextureUsageShaderRead;
        descriptor.resourceOptions = MTLResourceStorageModePrivate;
        output = [_device newTextureWithDescriptor:descriptor];
        output.label = name;
    }
    
    id<MTLRenderPipelineState> pipeline = nil;
    {
        MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.label = @"CubeFilter3";
        descriptor.vertexFunction = [self newFunctionWithName:@"CubeFilterVertex"];
        descriptor.fragmentFunction = [self newFunctionWithName:@"CubeFilterAccumulate3"];
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
        descriptor.colorAttachments[0].blendingEnabled = YES;
        descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
        descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
        descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
        descriptor.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
        pipeline = [self newRenderPipelineStateWithDescriptor:descriptor];
    }
    
    CubeFilterUniforms uniforms;
    {
        // we have position in NDCs with XY01
        //
        
        // translate z=0 near clip plane cordinates to z=1
        simd_float4x4 Z
        = simd_matrix(make<float4>(1.0f, 0.0f, 0.0f, 0.0f),
                      make<float4>(0.0f, 1.0f, 0.0f, 0.0f),
                      make<float4>(0.0f, 0.0f, 1.0f, 0.0f),
                      make<float4>(0.0f, 0.0f, 1.0f, 1.0f));
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
    
    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    
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
            target = [_device newTextureWithDescriptor:descriptor];
            target.label = @"Cube filter accumulate";
        }
        
        MTLRenderPassDescriptor* renderPass = [MTLRenderPassDescriptor new];
        renderPass.colorAttachments[0].texture = target;
        renderPass.colorAttachments[0].loadAction = MTLLoadActionClear;
        renderPass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
        renderPass.colorAttachments[0].storeAction = MTLStoreActionStore;
        renderPass.renderTargetArrayLength = 6;
        
        {
            float roughness = pow(2.0, 1.0 + level - output.mipmapLevelCount);
            float alpha = roughness * roughness;
            uniforms.alpha2 = alpha * alpha;

            id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPass];

            [encoder setRenderPipelineState:pipeline];
            [encoder setVertexBuffer:_screenTriangleStripVertexBuffer
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
    
    [commandBuffer commit];
    
    return output;

}

-(void) computeFresnelLUT
{
    MTLTextureDescriptor* texture_descriptor = [MTLTextureDescriptor new];
    texture_descriptor.textureType = MTLTextureType2D;
    texture_descriptor.pixelFormat = MTLPixelFormatRG16Float;
    texture_descriptor.width = 256;
    texture_descriptor.height = 256;
    texture_descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    texture_descriptor.resourceOptions = MTLStorageModeShared;
    _deferredLightImageBasedFresnelTexture = [_device newTextureWithDescriptor:texture_descriptor];
    _deferredLightImageBasedFresnelTexture.label = @"Fresnel integral look-up-table";
    
    id<MTLRenderPipelineState> render_pipeline_state = nil;
    MTLRenderPipelineDescriptor* render_pipeline_descriptor = [MTLRenderPipelineDescriptor new];
    render_pipeline_descriptor.vertexFunction = [self newFunctionWithName:@"split_sum_vertex_function"];
    render_pipeline_descriptor.fragmentFunction = [self newFunctionWithName:@"split_sum_fragment_function"];
    render_pipeline_descriptor.colorAttachments[0].pixelFormat = _deferredLightImageBasedFresnelTexture.pixelFormat;
    render_pipeline_descriptor.label = @"SplitSum";
    render_pipeline_state = [self newRenderPipelineStateWithDescriptor:render_pipeline_descriptor];
        
    MTLRenderPassDescriptor* render_pass_descriptor = [MTLRenderPassDescriptor new];
    render_pass_descriptor.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    render_pass_descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    render_pass_descriptor.colorAttachments[0].texture = _deferredLightImageBasedFresnelTexture;
    
    id<MTLCommandBuffer> command_buffer = [_commandQueue commandBuffer];
    
    id<MTLRenderCommandEncoder> command_encoder = [command_buffer renderCommandEncoderWithDescriptor:render_pass_descriptor];
    [command_encoder setRenderPipelineState:render_pipeline_state];
    [command_encoder setVertexBuffer:_screenTriangleStripVertexBuffer offset:0 atIndex:AAPLBufferIndexVertices];
    [command_encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [command_encoder endEncoding];
    
    [command_buffer commit];
    
}

-(nonnull instancetype)initWithMetalDevice:(nonnull id<MTLDevice>)device
                        drawablePixelFormat:(MTLPixelFormat)drawablePixelFormat
                                      model:(std::shared_ptr<wry::model>)model_
                                       view:(NSView*)view_
{

    using namespace ::wry;
    using namespace ::simd;
        
    if ((self = [super init])) {
        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
                
        auto newBufferWithArray = [&](auto& a) {
            void* bytes = a.data();
            size_t length = a.size() * sizeof(typename std::decay_t<decltype(a)>::value_type);
            id<MTLBuffer> buffer = [_device newBufferWithBytes:bytes length:length options:MTLStorageModeShared];
            assert(buffer);
            return buffer;
        };
        
        _model = model_;
        _view = view_;
        _drawablePixelFormat = drawablePixelFormat;
                
        _device = device;
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
                        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
        
        // Prepare image base lighting lookup-table
        [self computeFresnelLUT];
        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);

        // Prepare shadow map resources
        {
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
            descriptor.textureType = MTLTextureType2D;
            descriptor.width = 2048;
            descriptor.height = 2048;
            descriptor.pixelFormat = MTLPixelFormatDepth32Float;
            descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            descriptor.resourceOptions = MTLResourceStorageModePrivate;
            _shadowMapTarget = [_device newTextureWithDescriptor:descriptor];
            _shadowMapTarget.label = @"Shadow map texture";
        }
        {
            // todo: make a dedicated ShadowVertexShader so that we can use the
            // same uniforms
            // todo: use vertex amplification to render the gbuffer and the
            // shadow buffer from the same draw command?  probably not, even if
            // the fragment shader could handle missing render targets, it would
            // still not be optimized to not compute them
            
            MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
            descriptor.label = @"Shadow map pipeline";
            descriptor.vertexFunction = [self newFunctionWithName:@"deferred::shadow_vertex_function"];
            descriptor.fragmentFunction = [self newFunctionWithName:@"deferred::shadow_fragment_function"];
            descriptor.vertexBuffers[AAPLBufferIndexVertices].mutability = MTLMutabilityImmutable;
            descriptor.vertexBuffers[AAPLBufferIndexUniforms].mutability = MTLMutabilityImmutable;
            descriptor.depthAttachmentPixelFormat = _shadowMapTarget.pixelFormat;
            _shadowMapRenderPipelineState = [self newRenderPipelineStateWithDescriptor:descriptor];
        }
        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);

        // Prefilter the environment map to approximate spectral lobes
        _deferredLightImageBasedTexture = [self prefilteredEnvironmentMapFromResource:@"day" ofType:@"png"];

        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
        
        
        {
            MTLRenderPipelineDescriptor *descriptor = [[MTLRenderPipelineDescriptor alloc] init];
            
            // Render meshes to GBuffer

            descriptor.colorAttachments[AAPLColorIndexColor].pixelFormat = drawablePixelFormat;
            descriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].pixelFormat = MTLPixelFormatRGBA16Float;
            descriptor.colorAttachments[AAPLColorIndexNormalRoughness].pixelFormat = MTLPixelFormatRGBA16Float;
            descriptor.colorAttachments[AAPLColorIndexDepth].pixelFormat = MTLPixelFormatR32Float;
            descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

            descriptor.vertexFunction = [self newFunctionWithName:@"deferred::vertex_function"];
            descriptor.fragmentFunction = [self newFunctionWithName:@"deferred::fragment_function"];
            descriptor.label = @"Deferred G-buffer";
            _deferredGBufferRenderPipelineState = [self newRenderPipelineStateWithDescriptor:descriptor];
            
            descriptor.vertexFunction = [self newFunctionWithName:@"whiskerVertexShader"];
            descriptor.fragmentFunction = [self newFunctionWithName:@"whiskerFragmentShader"];
            descriptor.label = @"Deferred lines (DEBUG)";
            _deferredLinesRenderPipelineState = [self newRenderPipelineStateWithDescriptor:descriptor];

            descriptor.vertexFunction = [self newFunctionWithName:@"pointsVertexShader"];
            descriptor.fragmentFunction = [self newFunctionWithName:@"pointsFragmentShader"];
            descriptor.label = @"Deferred points (DEBUG)";
            _deferredPointsRenderPipelineState = [self newRenderPipelineStateWithDescriptor:descriptor];

            // Full-screen deferred lighting passes
            descriptor.colorAttachments[AAPLColorIndexColor].blendingEnabled = YES;
            descriptor.colorAttachments[AAPLColorIndexColor].rgbBlendOperation = MTLBlendOperationAdd;
            descriptor.colorAttachments[AAPLColorIndexColor].alphaBlendOperation = MTLBlendOperationAdd;
            descriptor.colorAttachments[AAPLColorIndexColor].sourceRGBBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[AAPLColorIndexColor].sourceAlphaBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[AAPLColorIndexColor].destinationRGBBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[AAPLColorIndexColor].destinationAlphaBlendFactor = MTLBlendFactorOne;
        
            descriptor.vertexFunction = [self newFunctionWithName:@"deferred::lighting_vertex_function"];
            descriptor.fragmentFunction = [self newFunctionWithName:@"deferred::image_based_lighting_fragment_function"];
            descriptor.label = @"Deferred image-based light";
            _deferredLightImageBasedRenderPipelineState = [self newRenderPipelineStateWithDescriptor:descriptor];

            descriptor.vertexFunction = [self newFunctionWithName:@"deferred::lighting_vertex_function"];
            descriptor.fragmentFunction = [self newFunctionWithName:@"deferred::directional_lighting_fragment_function"];
            descriptor.label = @"Deferred shadowcasting directional light";
            _deferredLightDirectionalShadowcastingRenderPipelineState = [self newRenderPipelineStateWithDescriptor:descriptor];

            descriptor.vertexFunction = [self newFunctionWithName:@"deferred::lighting_vertex_function"];
            descriptor.fragmentFunction = [self newFunctionWithName:@"deferred::point_lighting_fragment_function"];
            descriptor.label = @"Deferred point light";
            _deferredLightPointRenderPipelineState = [self newRenderPipelineStateWithDescriptor:descriptor];

        }
        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);

                
        {
            // Create a pipeline state descriptor to create a compiled pipeline state object
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
            
            NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
            
            _atlas = new wry::atlas(2048, device);
            _font = new wry::font(build_font(*_atlas));
        }

        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
        
        _gaussianBlur = [[MPSImageGaussianBlur alloc] initWithDevice:_device sigma:32.0];
        _imageAdd = [[MPSImageAdd alloc] initWithDevice:_device];
        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);

        {
            _symbols = [self newTextureFromResource:@"assets" ofType:@"png"];
            _hackmesh = [[WryMesh alloc] initWithDevice:_device];
            _black = [self newTextureFromResource:@"black" ofType:@"png"];
            _white = [self newTextureFromResource:@"white" ofType:@"png"];
            _blue = [self newTextureFromResource:@"blue"
                                          ofType:@"png"
                                 withPixelFormat:MTLPixelFormatRGBA8Unorm];
            _darkgray = [self newTextureFromResource:@"gray_sRGB" ofType:@"png"];
            //_darkgray = [self newTextureFromResource:@"darkgray" ofType:@"png"];
            _orange = [self newTextureFromResource:@"orange" ofType:@"png"];

            
            MeshInstanced i;
            i.model_transform = simd_matrix_rotate(-M_PI_2, simd_make_float3(-1.0f, 0.0f, 0.0f));
            i.inverse_transpose_model_transform = simd_inverse(simd_transpose(i.model_transform));
            i.albedo = make<float4>(1.0f, 1.0f, 1.0f, 1.0f);
            _instanced_things = [_device newBufferWithBytes:&i length:sizeof(i) options:MTLStorageModeShared];
            
        }
        
        {
            auto f = [&](WryMesh *__strong& p, std::filesystem::path v) {
                auto m = wry::from_obj(v);
                m.MeshVertexify();
                
                p = [[WryMesh alloc] initWithDevice:_device];
                p.vertexBuffer = newBufferWithArray(m.hack_MeshVertex);
                p.indexBuffer = newBufferWithArray(m.hack_triangle_strip);
                
                p.emissiveTexture = _black;
                p.albedoTexture = _white; // [self newTextureFromResource:@"PaintedMetal009_1K-PNG_Color" ofType:@"png"];
                p.metallicTexture = _white; // [self newTextureFromResource:@"PaintedMetal009_1K-PNG_Metalness" ofType:@"png"];
                p.normalTexture = _blue; // [self newTextureFromResource:@"PaintedMetal009_1K-PNG_NormalGL" ofType:@"png" withPixelFormat:MTLPixelFormatRGBA8Unorm];
                p.roughnessTexture = _darkgray; // [self newTextureFromResource:@"PaintedMetal009_1K-PNG_Roughness" ofType:@"png"];;
                p.instanceCount = 0;
            };
            
            f(_furnace_mesh, "/Users/antony/Desktop/assets/furnace.obj");
            f(_mine_mesh, "/Users/antony/Desktop/assets/mine.obj");
            f(_truck_mesh, "/Users/antony/Desktop/assets/truck2.obj");
        }
                    
        {
            
            Table<wry::String, i64> _name_to_opcode;
            Table<i64, wry::String> _opcode_to_name;

            /*
            try {
                auto x = json::from_file<Array<String>>("/Users/antony/Desktop/assets/opcodes.json");
                ulong i = 0;
                for (const String& y : x)
                    _name_to_opcode[y] = i++;
                
                json::serializer s;
                serialize(x.as_view(), s);
                printf("%.*s\n", (int) s.s.chars.size(), (char*) s.s.chars.data());
            } catch (...) {
                
            }
             */
            
            for (auto&& [k, v] : wry::sim::OPCODE_NAMES) {
                _opcode_to_name.emplace(k, v);
                _name_to_opcode.emplace(v + 7, k);
            }
            
                
            try {
                auto x = json::from_file<Array<Array<String>>>("/Users/antony/Desktop/assets/assets.json");
                ulong i = 0;
                for (const Array<String>& y : x) {
                    ulong j = 0;
                    for (const String& z : y) {
                        printf("%.*s\n", (int) z.chars.size(), (const char*) z.chars.data());
                        simd_float4 coordinate = make<float4>(j / 32.0f, i / 32.0f, 0.0f, 1.0f);
                        auto p = _name_to_opcode.find(z);
                        if (p != _name_to_opcode.end()) {
                            _opcode_to_coordinate[p->second] = coordinate;
                            
                            
                            matrix<RGBA8Unorm_sRGB> tile(64, 64);
                            MTLRegion region = MTLRegionMake2D(j*64, i*64, 64, 64);
                            [_symbols getBytes:tile.data()
                                   bytesPerRow:tile.stride_bytes()
                                    fromRegion:region
                                   mipmapLevel:0];
                            
                            float2 cursor{0.0f, 8.0f};
                            for (char32_t charcode : z) {
                                auto [offset, view, advance] = get_glyph(charcode);
                                cursor.y += advance.y;
                                if (charcode == u8'_') {
                                    cursor.x = 0;
                                    cursor.y += 4;
                                    continue;
                                }
                                if (cursor.x + advance.x >= 64) {
                                    cursor.x = 0;
                                    cursor.y += advance.y + 4;
                                }
                                if ((view.major() < 3) || (view.minor() < 3))
                                    continue;
                                compose(tile, view, (cursor - offset).yx);
                                cursor.x += advance.x;
                                cursor.y -= advance.y;
                            }
                            
                            [_symbols replaceRegion:region
                                        mipmapLevel:0
                                          withBytes:tile.data()
                                        bytesPerRow:tile.stride_bytes()];

                            
                        }
                        ++j;
                    }
                    ++i;
                }
            } catch (...) {
                
            }

            size_type nn = 24;
            _controls._payload = wry::matrix<wry::sim::Value>(nn, 2);
            
            i64 j = 0;
            for (i64 i = 0; i != _name_to_opcode.size(); ++i) {
                if (_opcode_to_coordinate.contains(i)) {
                    
                    _controls._payload[j % nn, j / nn] = gc::value_make_opcode((int)i);
                    ++j;
                                        
                } else {
                    auto s = _opcode_to_name[i].chars.as_view();
                    printf("image not found for %.*s\n", (int) s.size(), (char*) s.data());
                }
            }
            
            printf("%lld\n", j);
            
            _controls._transform
            =
            simd_matrix_scale(1.0f / 16.0f)
            * simd_matrix_translate(simd_make_float3(nn*-0.5f, -2.0f, 0.0f));
            
            {
                int n = 64;
                matrix<float> mm(n, n);
                wry::sdf::render_arrow(mm);
                //for (auto&& a : mm) {
                    //for (auto&& b : a) {
                        //printf("%g\n", b);
                    //}
                //}
                
                matrix<RGBA8Unorm_sRGB> nn(n, n);
                nn = mm;
                _atlas->place(nn);
                
            }
            
        }
        
    }
        
    NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);

    return self;
}


- (void) drawOverlay:(id<MTLRenderCommandEncoder>)encoder {
    
    using namespace simd;
    using namespace wry;
    using namespace wry::sim;
    
    
    MyUniforms uniforms;
    
    [encoder setRenderPipelineState:_overlayRenderPipelineState];
    
    {
        // render Palettes
        
        uniforms.position_transform = simd_mul(matrix_float4x4{{
            {1.0f, 0.0f, 0.0f},
            {0.0f, -_model->_viewport_size.x / _model->_viewport_size.y, 0.0f, 0.0f},
            { 0.0f, 0.0f, 1.0f, 0.0f },
            {0.0f, -1.0f, 0.0f, 1.0f},
        }}, _controls._transform);
        
        wry::Array<wry::vertex> v;
        
        simd_float4 b = make<float4>(_model->_mouse, 0.0f, 1.0f);
        float2 mmm = project_screen_ray(uniforms.position_transform, b);
        auto& m = _controls._payload;
        
        if (_model->_outstanding_click) {
            
            difference_type i = floor(mmm.x);
            difference_type j = floor(mmm.y);
            
            if ((0 <= i) && (i < m.minor()) && (0 <= j) && (j < m.minor())) {
                // we have clicked on the palette
                _model->_selected_i = i;
                _model->_selected_j = j;
                _model->_holding_value = m[i, j];
                printf(" Clicked palette (%td, %td)\n", i, j);
                
                // replace cursor
                {
                    
                    // we need to swap the cursor image based on gui
                    
                    // ImGui::GetIO().WantCaptureMouse
                    
                    auto coordinate = _opcode_to_coordinate[value_as_opcode(_model->_holding_value)];
                    matrix<RGBA8Unorm_sRGB> tile(64, 64);
                    
                    MTLRegion region = MTLRegionMake2D(coordinate.x * _symbols.width, coordinate.y * _symbols.height, 64, 64);
                    [_symbols getBytes:tile.data()
                           bytesPerRow:tile.stride_bytes()
                            fromRegion:region
                           mipmapLevel:0];
                    
                    unsigned char* p[1];
                    p[0] = (unsigned char*) tile.data();
                    
                    NSBitmapImageRep* a = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:p
                                                                                  pixelsWide:64
                                                                                  pixelsHigh:64
                                                                               bitsPerSample:8
                                                                             samplesPerPixel:4
                                                                                    hasAlpha:YES
                                                                                    isPlanar:NO
                                                                              colorSpaceName:NSCalibratedRGBColorSpace
                                                                                 bytesPerRow:tile.stride_bytes()
                                                                                bitsPerPixel:32];
                    NSImage* b = [[NSImage alloc] initWithSize:NSMakeSize(32.0, 32.0)];
                    [b addRepresentation:a];
                    NSCursor* c = [[NSCursor alloc] initWithImage:b hotSpot:NSMakePoint(0.0f, 0.0f)];
                    [c set];
                    _cursor = c;
                    
                }
                
                
                
            } else {
                // we clicked outside the palette
                int i = round(_model->_mouse4.x);
                int j = round(_model->_mouse4.y);
                Coordinate xy{i, j};
                // auto& the_tile = _model->_world._value_for_coordinate[xy];
                // the_tile = _model->_holding_value;
                _model->_world._value_for_coordinate.write(xy, _model->_holding_value);
                
                // these notifications happen logically between steps and are
                // excused from transactions (hopefully)
                
                // the_tile.notify_occupant(&_model->_world);
                notify_by_world_coordinate(&_model->_world, xy);
                printf(" Clicked world (%d, %d)\n", i, j);
            }
            _model->_outstanding_click = false;
            
        }
        
        while (!_model->_outstanding_keysdown.empty()) {
            char32_t ch = _model->_outstanding_keysdown.front_and_pop_front();
            if (wry::isascii((int) ch) && isxdigit(ch)) {
                int64_t k = wry::base36::from_base36_table[ch];
                int i = round(_model->_mouse4.x);
                int j = round(_model->_mouse4.y);
                Coordinate xy{i, j};
                // auto& the_tile = _model->_world._value_for_coordinate[xy];
                // the_tile = k;
                _model->_world._value_for_coordinate.write(xy, k);
                // the_tile.notify_occupant(&_model->_world);
                notify_by_world_coordinate(&_model->_world, xy);
            }
        }
        
        
        for (difference_type j = 0; j != m.major(); ++j) {
            for (difference_type i = 0; i != m.minor(); ++i) {
                Value a = m[i, j];
                if (a.is_opcode()) {
                    
                    vertex c;
                    
                    simd_float4 position = make<float4>(i, j, 0, 1);
                    float2 texCoord;
                    
                    c.color = RGBA8Unorm_sRGB(0.0f, 0.0f, 0.0f, 0.875f);
                    texCoord = simd_make_float2(9, 9) / 32.0f;
                    
                    if ((floor(mmm.x) == i) && floor(mmm.y) == j) {
                        c.color.g.write(0.5f);
                    }
                    
                    if ((_model->_selected_i == i) && _model->_selected_j == j) {
                        c.color.r.write(0.5f);
                    }
                    
                    
                    c.v.position = make<float4>(0, 0, 0, 0) + position;
                    c.v.texCoord = simd_make_float2(0, 0)/32.0f + texCoord;
                    v.push_back(c);
                    
                    c.v.position = make<float4>(1, 0, 0, 0) + position;
                    c.v.texCoord = simd_make_float2(1, 0) / 32.0f + texCoord;
                    v.push_back(c);
                    
                    c.v.position = make<float4>(1, 1, 0, 0) + position;
                    c.v.texCoord = simd_make_float2(1, 1) / 32.0f + texCoord;
                    v.push_back(c);
                    
                    c.v.position = make<float4>(0, 0, 0, 0) + position;
                    c.v.texCoord = simd_make_float2(0, 0)/32.0f + texCoord;
                    v.push_back(c);
                    
                    c.v.position = make<float4>(1, 1, 0, 0) + position;
                    c.v.texCoord = simd_make_float2(1, 1) / 32.0f + texCoord;
                    v.push_back(c);
                    
                    c.v.position = make<float4>(0, 1, 0, 0) + position;
                    c.v.texCoord = simd_make_float2(0, 1) / 32.0f + texCoord;
                    v.push_back(c);
                    
                    texCoord = _opcode_to_coordinate[value_as_opcode(a)].xy;
                    c.color = RGBA8Unorm_sRGB(1.0f, 1.0f, 1.0f, 1.0f);
                    
                    c.v.position = make<float4>(0, 0, 0, 0) + position;
                    c.v.texCoord = simd_make_float2(0, 0)/32.0f + texCoord;
                    v.push_back(c);
                    
                    c.v.position = make<float4>(1, 0, 0, 0) + position;
                    c.v.texCoord = simd_make_float2(1, 0) / 32.0f + texCoord;
                    v.push_back(c);
                    
                    c.v.position = make<float4>(1, 1, 0, 0) + position;
                    c.v.texCoord = simd_make_float2(1, 1) / 32.0f + texCoord;
                    v.push_back(c);
                    
                    c.v.position = make<float4>(0, 0, 0, 0) + position;
                    c.v.texCoord = simd_make_float2(0, 0)/32.0f + texCoord;
                    v.push_back(c);
                    
                    c.v.position = make<float4>(1, 1, 0, 0) + position;
                    c.v.texCoord = simd_make_float2(1, 1) / 32.0f + texCoord;
                    v.push_back(c);
                    
                    c.v.position = make<float4>(0, 1, 0, 0) + position;
                    c.v.texCoord = simd_make_float2(0, 1) / 32.0f + texCoord;
                    v.push_back(c);
                }
            }
        }
        
        id<MTLBuffer> vb = [_device newBufferWithLength:v.size_in_bytes() options:MTLStorageModeShared];
        memcpy(vb.contents, v.data(), v.size_in_bytes());
        
        
        [encoder setVertexBytes:&uniforms
                         length:sizeof(uniforms)
                        atIndex:AAPLBufferIndexUniforms ];
        [encoder setVertexBuffer:vb
                          offset:0
                         atIndex:AAPLBufferIndexVertices];
        [encoder setFragmentTexture:_symbols
                            atIndex:AAPLTextureIndexColor];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:v.size()];
        
        
    }
    
    
    uniforms.position_transform = matrix_float4x4{{
        {2.0f / _model->_viewport_size.x, 0.0f, 0.0f},
        {0.0f, -2.0f / _model->_viewport_size.y, 0.0f, 0.0f},
        { 0.0f, 0.0f, 1.0f, 0.0f },
        {-1.0f, +1.0f, 0.0f, 1.0f},
    }};
    [encoder setVertexBytes:&uniforms
                     length:sizeof(uniforms)
                    atIndex:AAPLBufferIndexUniforms ];
    
    auto draw_text = [=](wry::rect<float> x, wry::StringView v, wry::RGBA8Unorm_sRGB color) {
        
        auto valign = (_font->height + _font->ascender + _font->descender) / 2; // note descender is negative
        
        auto xy = x.a;
        xy.y += valign;
        while (!v.empty()) {
            auto c = v.front();
            v.pop_front();
            auto q = _font->charmap.find(c);
            if (q != _font->charmap.end()) {
                
                if (xy.x + q->second.advance > x.b.x) {
                    xy.x = x.a.x;
                    xy.y += _font->height;
                }
                if (xy.y - _font->descender > x.b.y) {
                    return xy;
                }
                
                wry::sprite s = q->second.sprite_;
                _atlas->push_sprite(s + xy, color);
                xy.x += q->second.advance;
                
            } else if (c == '\n') {
                xy.x = x.a.x;
                xy.y += _font->height;
            }
        }
        xy.y -= valign;
        return xy;
    };
    
    {
        wry::RGBA8Unorm_sRGB color(0.5f, 0.5f, 0.5f, 1.0f);
        // draw logs
        float y = _font->height / 2;
        float2 z;
        auto t = std::chrono::steady_clock::now();
        for (auto p = _model->_logs.begin(); p != _model->_logs.end();) {
            if (p->first < t) {
                p = _model->_logs.erase(p);
            } else {
                z = draw_text({_font->height / 2, y, 1920, 1080}, p->second, color);
                y += _font->height;
                ++p;
            }
        }
    }
    
    
    if (_model->_console_active) {
        // draw console
        wry::RGBA8Unorm_sRGB color(1.0f, 1.0f, 1.0f, 1.0f);
        float y = 1080 - _font->height / 2;
        float2 z;
        bool first = true;
        for (auto p = _model->_console.end(); (y >= 0) && (p != _model->_console.begin());) {
            --p;
            y -= _font->height;
            z = draw_text({_font->height / 2, y, 1920, 1080}, *p, color);
            if (first) {
                draw_text(wry::rect<float>{z.x, z.y, 1920, 1080 }, (_frame_count & 0x40) ? "_" : " ", color);
                first = false;
            }
        }
    }
    _atlas->commit(encoder);
}

-(void)resetCursor {
    [_cursor set];
}

- (void)renderToMetalLayer // :(nonnull CAMetalDisplayLinkUpdate*)update
{
    wry::gc::mutator_handshake();
    _model->shade_roots();

    using namespace ::simd;
    using namespace ::wry;

    // advance the simulation
    
    _model->_world.step();

    id<MTLCommandBuffer> command_buffer = [_commandQueue commandBuffer];
    
    // Construct camera transforms
    MeshUniforms uniforms = _model->_uniforms;
    
    // Construct ground plane transforms
    auto lookat_transform =  matrix_identity_float4x4;
    lookat_transform.columns[3].x += _model->_looking_at.x / 1024.0f;
    lookat_transform.columns[3].y -= _model->_looking_at.y / 1024.0f;

    MeshInstanced mesh_instanced_things = {};
    {
        mesh_instanced_things.model_transform = lookat_transform;
        mesh_instanced_things.inverse_transpose_model_transform = simd_inverse(simd_transpose(mesh_instanced_things.model_transform));
        mesh_instanced_things.albedo = make<float4>(1.0f, 1.0f, 1.0f, 1.0f);
        memcpy([_instanced_things contents], &mesh_instanced_things, sizeof(mesh_instanced_things));
    }
    
    // Project things onto ground plane
    
    // TODO: more generally
    // - bound mesh
    // - transform bounding volumes
    // - bounding volume intersect frustum
    // - transform to frustum planes
    // - ray-intersect mesh

    rect<i32> grid_bounds;
    {
        simd_float4x4 A = simd_mul(uniforms.viewprojection_transform,
                                   mesh_instanced_things.model_transform);
        
        // Mouse
        simd_float4 b = make<float4>(_model->_mouse, 0.0f, 1.0f);
        _model->_mouse4 = make<float4>(project_screen_ray(A, b), 0.0f, 1.0f);
        // b now contains the screen space coordinates of the
        // intersection, aka b.z is now the depth
        //assert((0.0f <= b.z) && (b.z <= 1.0f));
        
        // Screen corners
        simd_float4x2 c = project_screen_frustum(A);
        rect<float> uv(c.columns[0], c.columns[0]);
        for (int i = 1; i != 4; ++i) {
            uv.a = simd_min(uv.a, c.columns[i]);
            uv.b = simd_max(uv.b, c.columns[i]);
        }
        grid_bounds.a = simd_make_int2(floor(uv.a.x), floor(uv.a.y));
        grid_bounds.b = simd_make_int2(ceil(uv.b.x) + 1, ceil(uv.b.y) + 1);
        
        // TODO: the screen corners are only correct for geometry on the ground
        // plane, geometry outside this region but *above* the plane may appear
        // in the camera and/or shadow frustums
        
    }
    
    
    
    
    
    
    id<MTLBuffer> vertices = nil;
    id<MTLBuffer> indices = nil;
    NSUInteger index_count = 0;
    _furnace_mesh.instanceCount = 0;
    _mine_mesh.instanceCount = 0;
    _truck_mesh.instanceCount = 0;
    // raid model for data
    {
        auto tnow = world_time(&_model->_world);
        const auto& entities = _model->_world._entities;
        
        NSUInteger quad_count = entities.size() * 4 + 1000 + 2;
        NSUInteger vertex_count = quad_count * 4;
        index_count = quad_count * 6;
        vertices = [_device newBufferWithLength:vertex_count * sizeof(MeshVertex) options:MTLStorageModeShared];
        indices = [_device newBufferWithLength:index_count * sizeof(uint) options:MTLStorageModeShared];
        
        MeshVertex* pv = (MeshVertex*) vertices.contents;
        uint* pi = (uint*) indices.contents;
        MeshVertex v;
        v.tangent = make<float4>(1.0f, 0.0f, 0.0f, 0.0f);
        v.bitangent = make<float4>(0.0f, 1.0f, 0.0f, 0.0f);
        v.normal = make<float4>(0.0f, 0.0f, 1.0f, 0.0f);
        uint k = 0;
        for (size_t qi = 0; qi != entities.size(); ++qi) {
            sim::Entity* q = entities[qi];
            
            if (auto p = dynamic_cast<wry::sim::Machine*>(q)) { // ugh
                
                auto h0 = (p->_old_heading & 3) * M_PI_2;
                auto h1 = (p->_new_heading & 3) * M_PI_2;
                auto x0 = make<float2>(p->_old_location.x, p->_old_location.y);
                auto x1 = make<float2>(p->_new_location.x, p->_new_location.y);
                auto dx0 = make<float2>(sin(h0), cos(h0));
                auto dx1 = make<float2>(sin(h1), cos(h1));
                                
                float t = 1.0;
                if (tnow < p->_new_time) {
                    assert(tnow >= p->_old_time);
                    t = (tnow - p->_old_time) / (float) (p->_new_time - p->_old_time);
                }
                assert(0.0f <= t);
                assert(t <= 1.0f);
                //printf("%lld %lld %lld %g\n", p->_old_time, p->_new_time, tnow, t);
                
                /*interpolate_wheeled_vehicle(x0, make<float2>(dx0.y, -dx0.x),
                                            x1, make<float2>(dx1.y, -dx1.x));*/

                
                // now we do some crappy interpolation
                {
                    
                    
                    auto a = x0 + dx0 * t;
                    auto b = x1 + dx1 * (t - 1.0f);

                    auto s = smoothstep5(t);
                    auto ds = dsmoothstep5(t);
                    auto c = simd_mix(a, b, make<float2>(s, s));
                    auto dc = simd_mix(dx0, dx1, s) + (b - a) * ds;
                    x1 = c;
                    dx1 = dc;                    
                }
                
                {
                    /*
                    // now we do some different crappy interpolation
                    simd_float4 xdx = interpolate_wheeled_vehicle(x0,
                                                                  make<float2>(dx0.y, -dx0.x),
                                                                  x1,
                                                                  make<float2>(dx1.y, -dx1.x),
                                                                  t);
                    
                    x1 = xdx.xy;
                    dx1 = xdx.zw;
                     */

                }

                simd_float4 location = make<float4>(0.0, 0.0, 0.0, 1.0f);
                location.xy = x1;
                
                
                auto A = simd_matrix_translate(location) * lookat_transform;
                
                A = A * simd_matrix_rotate(atan2(dx1.x, dx1.y), make<float3>(0.0, 0.0, -1.0));
                
                {
                    // now make the instance
                    MeshInstanced m;
                    m.model_transform = A;
                    m.inverse_transpose_model_transform = inverse(transpose(A));
                    m.albedo = make<float4>(1.0, 1.0, 1.0, 1.0);
                    _truck_mesh.instances[_truck_mesh.instanceCount++] = m;
                }
                
                // now make the stack
                location.z += 0.8;
                for (int i = 0; i != p->_stack.size(); ++i) {
                    location.z += 0.5;
                    wry::sim::Value value = p->_stack[i];
                    simd_float4 coordinate;
                    if (value.is_opcode()) {
                        coordinate = _opcode_to_coordinate[value.as_opcode()];
                    } else {
                        // number, as hex
                        coordinate = make<float4>((value.as_int64_t() & 15) / 32.0f, 13.0f / 32.0f, 0.0f, 1.0f);
                    }
                    v.position = make<float4>(-0.5f, -0.5f, 0.0f, 0.0f) + location;
                    v.coordinate = make<float4>(0.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                    *pv++ = v;
                    v.position = make<float4>(+0.5f, -0.5f, 0.0f, 0.0f) + location;
                    v.coordinate = make<float4>(1.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                    *pv++ = v;
                    v.position = make<float4>(+0.5f, +0.5f, 0.0f, 0.0f) + location;
                    v.coordinate = make<float4>(1.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                    *pv++ = v;
                    v.position = make<float4>(-0.5f, +0.5f, 0.0f, 0.0f) + location;
                    v.coordinate = make<float4>(0.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                    *pv++ = v;
                    *pi++ = k;
                    *pi++ = k;
                    *pi++ = k + 1;
                    *pi++ = k + 3;
                    *pi++ = k + 2;
                    *pi++ = k + 2;
                    k += 4;
                }
                
            } else if (auto p = dynamic_cast<sim::LocalizedEntity*>(q)){
                
                simd_float4 location = make<float4>(p->_location.x, p->_location.y + 1.0, 0.0, 1.0f);
                auto A = simd_matrix_translate(location) * lookat_transform * simd_matrix_scale(0.5f);

                /*
                v.position = make<float4>(-0.5f, 0.0f, -0.5f, 0.0f) + location;
                v.coordinate = make<float4>(4.0f / 32.0f, 1.0f / 32.0f, 0.0f, 1.0f);
                *pv++ = v;
                v.position = make<float4>(+0.5f, 0.0f, -0.5f, 0.0f) + location;
                v.coordinate = make<float4>(5.0f / 32.0f, 1.0f / 32.0f, 0.0f, 1.0f);
                *pv++ = v;
                v.position = make<float4>(+0.5f, 0.0f, +0.5f, 0.0f) + location;
                v.coordinate = make<float4>(5.0f / 32.0f, 0.0f / 32.0f, 0.0f, 1.0f);
                *pv++ = v;
                v.position = make<float4>(-0.5f, 0.0f, +0.5f, 0.0f) + location;
                v.coordinate = make<float4>(4.0f / 32.0f, 0.0f / 32.0f, 0.0f, 1.0f);
                *pv++ = v;
                
                *pi++ = k;
                *pi++ = k;
                *pi++ = k + 1;
                *pi++ = k + 3;
                *pi++ = k + 2;
                *pi++ = k + 2;
                k += 4;
                 */
                
                WryMesh* s = nil;
                
                if (auto r = dynamic_cast<sim::Source*>(q)) {
                    s = _mine_mesh;
                } else if (auto r = dynamic_cast<sim::Sink*>(q)) {
                    s = _furnace_mesh;
                }
                
                if (s) {
                    // now make the instance
                    MeshInstanced m;
                    m.model_transform = A;
                    m.inverse_transpose_model_transform = inverse(transpose(A));
                    m.albedo = make<float4>(1.0, 1.0, 1.0, 1.0);
                    s.instances[s.instanceCount++] = m;
                }

                
                if (auto r = dynamic_cast<sim::Source*>(q)) {
                    
                    simd_float4 coordinate = make<float4>((r->_of_this.as_int64_t() & 15) / 32.0f, 13.0f / 32.0f, 0.0f, 1.0f);
                    
                    v.position = make<float4>(-0.5f, -0.1f, -0.5f, 0.0f) + location;
                    v.coordinate = make<float4>(0.0f / 32.0f, 1.0f / 32.0f, 0.0f, 1.0f) + coordinate;
                    *pv++ = v;
                    v.position = make<float4>(+0.5f, -0.1f, -0.5f, 0.0f) + location;
                    v.coordinate = make<float4>(1.0f / 32.0f, 1.0f / 32.0f, 0.0f, 1.0f) + coordinate;
                    *pv++ = v;
                    v.position = make<float4>(+0.5f, -0.1f, +0.5f, 0.0f) + location;
                    v.coordinate = make<float4>(1.0f / 32.0f, 0.0f / 32.0f, 0.0f, 1.0f) + coordinate;
                    *pv++ = v;
                    v.position = make<float4>(-0.5f, -0.1f, +0.5f, 0.0f) + location;
                    v.coordinate = make<float4>(0.0f / 32.0f, 0.0f / 32.0f, 0.0f, 1.0f) + coordinate;
                    *pv++ = v;
                    
                    *pi++ = k;
                    *pi++ = k;
                    *pi++ = k + 1;
                    *pi++ = k + 3;
                    *pi++ = k + 2;
                    *pi++ = k + 2;
                    k += 4;
                    
                }

                
            }
            
        }
        
              
        
        for (int i = grid_bounds.a.x; i != grid_bounds.b.x; ++i) {
            for (int j = grid_bounds.a.y; j != grid_bounds.b.y; ++j) {

                simd_float4 location = make<float4>(i, j, 0.1f, 1.0f);
                simd_float4 coordinate = make<float4>(0.0f / 32.0f, 2.0f / 32.0f, 0.0f, 1.0f);
                
                {
                    wry::sim::Value q = _model->_world._value_for_coordinate.read(wry::sim::Coordinate{i, j});
                    using namespace wry::sim;
                    if (q.is_int64_t()) {
                        coordinate = make<float4>((q.as_int64_t() & 15) / 32.0f, 13.0f / 32.0f, 0.0f, 1.0f);
                    } else if (q.is_opcode()) {
                        auto p = _opcode_to_coordinate.find(q.as_opcode());
                        if (p != _opcode_to_coordinate.end()) {
                            coordinate = p->second;
                        }
                    } else {
                        coordinate = make<float4>(0.0 / 32.0f, 1.0f / 32.0f, 0.0f, 1.0f);
                    }
                }
                
                v.position = make<float4>(-0.5f, -0.5f, 0.0f, 0.0f) + location;
                v.coordinate = make<float4>(0.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                *pv++ = v;
                v.position = make<float4>(+0.5f, -0.5f, 0.0f, 0.0f) + location;
                v.coordinate = make<float4>(1.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                *pv++ = v;
                v.position = make<float4>(+0.5f, +0.5f, 0.0f, 0.0f) + location;
                v.coordinate = make<float4>(1.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                *pv++ = v;
                v.position = make<float4>(-0.5f, +0.5f, 0.0f, 0.0f) + location;
                v.coordinate = make<float4>(0.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                *pv++ = v;
                
                *pi++ = k;
                *pi++ = k;
                *pi++ = k + 1;
                *pi++ = k + 3;
                *pi++ = k + 2;
                *pi++ = k + 2;
                
                k += 4;
            }
        }
        
        {
            
            // big ground plane
            
            v.coordinate = make<float4>(3.0 / 32.0f, 4.5f / 32.0f, 0.0f, 1.0f);
            v.position = make<float4>(grid_bounds.a.x - 1, grid_bounds.a.y - 1, 0.0, 1.0f);
            *pv++ = v;
            v.position = make<float4>(grid_bounds.b.x, grid_bounds.a.y - 1, 0.0, 1.0f);
            *pv++ = v;
            v.position = make<float4>(grid_bounds.b.x, grid_bounds.b.y, 0.0, 1.0f);
            *pv++ = v;
            v.position = make<float4>(grid_bounds.a.x - 1, grid_bounds.b.y, 0.0, 1.0f);
            *pv++ = v;
            
            *pi++ = k;
            *pi++ = k;
            *pi++ = k + 1;
            *pi++ = k + 3;
            *pi++ = k + 2;
            *pi++ = k + 2;
            
            k += 4;
            
        }
        
        {
            // mouse cursor thing
            simd_float4 location = _model->_mouse4;
            location.x = round(location.x);
            location.y = round(location.y);
            location.z = 0.1f;
            simd_float4 coordinate = make<float4>(0.0f / 32.0f, 2.0f / 32.0f, 0.0f, 1.0f);
            
            v.position = make<float4>(-0.5f, -0.5f, 0.0f, 0.0f) + location;
            v.coordinate = make<float4>(0.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
            *pv++ = v;
            v.position = make<float4>(+0.5f, -0.5f, 0.0f, 0.0f) + location;
            v.coordinate = make<float4>(1.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
            *pv++ = v;
            v.position = make<float4>(+0.5f, +0.5f, 0.0f, 0.0f) + location;
            v.coordinate = make<float4>(1.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
            *pv++ = v;
            v.position = make<float4>(-0.5f, +0.5f, 0.0f, 0.0f) + location;
            v.coordinate = make<float4>(0.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
            *pv++ = v;
            
            *pi++ = k;
            *pi++ = k;
            *pi++ = k + 1;
            *pi++ = k + 3;
            *pi++ = k + 2;
            *pi++ = k + 2;
            
            k += 4;
            
            int2 xy;
            xy.x = round(_model->_mouse4.x);
            xy.y = round(_model->_mouse4.z);
            ulong z = 0;
            memcpy(&z, &xy, 8);
            //ulong value = _model->_world.get(z);
            //++value;
            //_model->_world.set(z, value);
            
        }
        
        
    }
    
    // Our relatively flat scene permits several simplifications to the
    // rendering engine.  We don't need to handle the horizon and large numbers
    // of tiny objects being obstructed by foreground geometry; everything
    // appears at a similar LOD, a similar distance, and doesn't overlap other
    // geometry much (except the local ground plane)

    // Render a shadow map for a directional light source at infinity
    //
    // We use the free parameters of the shadow map transform to make the
    // shadow rays exact for the ground plane, which will be the main shadow
    // receiver.
    
    {
        MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor new];
        descriptor.depthAttachment.loadAction = MTLLoadActionClear;
        descriptor.depthAttachment.storeAction = MTLStoreActionStore;
        descriptor.depthAttachment.clearDepth = 1.0;
        descriptor.depthAttachment.texture = _shadowMapTarget;
        id<MTLRenderCommandEncoder> render_command_encoder = [command_buffer renderCommandEncoderWithDescriptor:descriptor];
                
        [render_command_encoder setRenderPipelineState:_shadowMapRenderPipelineState];
        [render_command_encoder setDepthStencilState:_enabledDepthStencilState];

        // Tweak the depth somewhat to prevent shadow acne, seems to work
        // adequately
        [render_command_encoder setDepthBias:0.0f // +100.0f
                                  slopeScale:+1.0f // ?
                                       clamp:0.0f];
        
        // We don't render the ground plane here as it can't cast shadows; is
        // there anything else in this category to discard?

        // TODO: Think about draw order here to maximize fragment discard
        // Are 2D entities mostly just above the ground plane and thus behind
        // any 3D geometry?

        // Draw the 2D entities

        [render_command_encoder setVertexBytes:&uniforms
                                        length:sizeof(MeshUniforms)
                                       atIndex:AAPLBufferIndexUniforms];
        
        [render_command_encoder setVertexBuffer:vertices offset:0 atIndex:AAPLBufferIndexVertices];
        [render_command_encoder setVertexBuffer:_instanced_things offset:0 atIndex:AAPLBufferIndexInstanced];
                    
        [render_command_encoder setFragmentBytes:&uniforms
                                          length:sizeof(MeshUniforms)
                                         atIndex:AAPLBufferIndexUniforms];
        [render_command_encoder setFragmentTexture:_symbols atIndex:AAPLTextureIndexAlbedo];
        
        [render_command_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip
                                           indexCount:index_count
                                            indexType:MTLIndexTypeUInt32
                                          indexBuffer:indices
                                    indexBufferOffset:0];
        
        // Draw the 3D entities

        [_furnace_mesh drawWithRenderCommandEncoder:render_command_encoder commandBuffer:command_buffer];
        [_mine_mesh drawWithRenderCommandEncoder:render_command_encoder commandBuffer:command_buffer];
        [_truck_mesh drawWithRenderCommandEncoder:render_command_encoder commandBuffer:command_buffer];
        
        // TODO: add an irradiance buffer from the light
        //
        // - clear to max irradience
        //   - image projection if not from sun
        // - disable z write but not test and draw order-independent
        //   attenuation from smoke in front of solid receiver geometry
        // - other?

        [render_command_encoder endEncoding];
        
    }
    
    {
        // Deferred render pass
        
        id <MTLRenderCommandEncoder> encoder = nil;
        MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor new];

        
        {
            
            // Set up the g-buffer.  The textures used here (on Apple Silicon)
            // are "memoryless" and use the local image block storage; we
            // can only read back locally, which is excatly what we need for
            // g-buffer
                        
            descriptor.colorAttachments[AAPLColorIndexColor].clearColor = MTLClearColorMake(0, 0, 0, 1);
            descriptor.colorAttachments[AAPLColorIndexColor].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexColor].storeAction = MTLStoreActionStore;
            descriptor.colorAttachments[AAPLColorIndexColor].texture = _deferredLightColorAttachmentTexture;
            
            descriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].clearColor = MTLClearColorMake(0, 0, 0, 0);
            descriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].storeAction = MTLStoreActionDontCare;
            descriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].texture = _deferredAlbedoMetallicColorAttachmentTexture;
            
            descriptor.colorAttachments[AAPLColorIndexNormalRoughness].clearColor = MTLClearColorMake(0, 0, 0, 1);
            descriptor.colorAttachments[AAPLColorIndexNormalRoughness].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexNormalRoughness].storeAction = MTLStoreActionDontCare;
            descriptor.colorAttachments[AAPLColorIndexNormalRoughness].texture = _deferredNormalRoughnessColorAttachmentTexture;
            
            descriptor.colorAttachments[AAPLColorIndexDepth].clearColor = MTLClearColorMake(1, 1, 1, 1);
            descriptor.colorAttachments[AAPLColorIndexDepth].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexDepth].storeAction = MTLStoreActionDontCare;
            descriptor.colorAttachments[AAPLColorIndexDepth].texture = _deferredDepthColorAttachmentTexture;
            
            descriptor.depthAttachment.clearDepth = 1.0;
            descriptor.depthAttachment.loadAction = MTLLoadActionClear;
            descriptor.depthAttachment.storeAction = MTLStoreActionDontCare;
            descriptor.depthAttachment.texture = _deferredDepthAttachmentTexture;
            
            // TODO: Clearcoat and clearcoat roughness; we'll need these for
            // wet materials?
            
            encoder = [command_buffer renderCommandEncoderWithDescriptor:descriptor];
            
        }
        
        {

            {
                // image-based lighting propeties can be used to rotate and
                // blend environment maps, such as dawn-day-dusk-night
                uniforms.ibl_scale = 1.0f;
                uniforms.ibl_transform = matrix_identity_float3x3;
                // TODO: resurrect blending

            }
            
            // camera world location is...
            
            {
                [encoder setRenderPipelineState:_deferredGBufferRenderPipelineState];
                
                [encoder setDepthStencilState:_enabledDepthStencilState];
                [encoder setCullMode:MTLCullModeBack];

                bool show_jacobian, show_points, show_wireframe;
                {
                    show_jacobian = _model->_show_jacobian;
                    show_points = _model->_show_points;
                    show_wireframe = _model->_show_wireframe;

                }

                if (show_wireframe) {
                    [encoder setTriangleFillMode:MTLTriangleFillModeLines];
                }
                
                // TODO: resurrect Jacobian whiskers
                // TODO: key through the various gbuffers
                
                /*
                [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                            vertexStart:0
                            vertexCount:_mesh_count];
                 */
                /*
                [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip
                                    indexCount:_mesh_count
                                     indexType:MTLIndexTypeUInt32
                                   indexBuffer:_cube_indices
                             indexBufferOffset:0
                                 instanceCount:100];*/
                
                [encoder setVertexBytes:&uniforms
                                 length:sizeof(MeshUniforms)
                                atIndex:AAPLBufferIndexUniforms];
                
                [encoder setFragmentBytes:&uniforms
                                   length:sizeof(MeshUniforms)
                                  atIndex:AAPLBufferIndexUniforms];
                

                if (show_wireframe) {
                    [encoder setTriangleFillMode:MTLTriangleFillModeFill];
                }

                [encoder setFragmentTexture:_black atIndex:AAPLTextureIndexEmissive];
                [encoder setFragmentTexture:_symbols atIndex:AAPLTextureIndexAlbedo];
                [encoder setFragmentTexture:_black atIndex:AAPLTextureIndexMetallic];
                [encoder setFragmentTexture:_blue atIndex:AAPLTextureIndexNormal];
                [encoder setFragmentTexture:_white atIndex:AAPLTextureIndexRoughness];
                [encoder setVertexBuffer:vertices offset:0 atIndex:AAPLBufferIndexVertices];
                [encoder setVertexBuffer:_instanced_things offset:0 atIndex:AAPLBufferIndexInstanced];
                [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip
                                    indexCount:index_count
                                     indexType:MTLIndexTypeUInt32
                                   indexBuffer:indices
                             indexBufferOffset:0];
                
                [_furnace_mesh drawWithRenderCommandEncoder:encoder commandBuffer:command_buffer];
                [_mine_mesh drawWithRenderCommandEncoder:encoder commandBuffer:command_buffer];
                [_truck_mesh drawWithRenderCommandEncoder:encoder commandBuffer:command_buffer];

                /*
                if (show_points) {
                    [encoder setRenderPipelineState:_pointsPipelineState];
                    [encoder drawPrimitives:MTLPrimitiveTypePoint
                                vertexStart:0
                                vertexCount:_mesh_count];
                    [encoder setRenderPipelineState:_colorRenderPipelineState];
                }
                
                if (show_jacobian) {
                    [encoder setRenderPipelineState:_whiskerPipelineState];
                    [encoder setVertexBuffer:_whisker_buffer offset:0 atIndex:AAPLBufferIndexVertices];
                    [encoder drawPrimitives:MTLPrimitiveTypeLine
                                vertexStart:0
                                vertexCount:_whisker_count];
                    [encoder setRenderPipelineState:_colorRenderPipelineState];
                    [encoder setVertexBuffer:_cube_buffer offset:0 atIndex:AAPLBufferIndexVertices];
                }*/

                // G-buffers are now initialized
                //
                // On non-Apple Silicon/tiled, do we have to write and rebind the
                // textures, or something else?
                                
                [encoder setVertexBuffer:_screenTriangleStripVertexBuffer
                                  offset:0
                                 atIndex:AAPLBufferIndexVertices];
                [encoder setDepthStencilState:_disabledDepthStencilState];

                // Image-based lights:

                [encoder setRenderPipelineState:_deferredLightImageBasedRenderPipelineState];
                [encoder setFragmentTexture:_deferredLightImageBasedTexture atIndex:AAPLTextureIndexEnvironment];
                [encoder setFragmentTexture:_deferredLightImageBasedFresnelTexture atIndex:AAPLTextureIndexFresnel];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                            vertexStart:0
                            vertexCount:4];

                
                // Directional light (with shadow map):
                //
                // Though this is the main light source, we do it last to delay
                // the need to read from the shadow map as long as possible,
                // since unlike the G-buffer this is a nonlocalized dependency
                // on an earlier render pass.  But, does this matter?  On which
                // architectures?
                
                uniforms.radiance = simd_make_float3(1.0, 1.0, 1.0);
                [encoder setFragmentBytes:&uniforms
                                   length:sizeof(MeshUniforms)
                                  atIndex:AAPLBufferIndexUniforms];

                [encoder setRenderPipelineState:_deferredLightDirectionalShadowcastingRenderPipelineState];
                [encoder setFragmentTexture:_shadowMapTarget atIndex:AAPLTextureIndexShadow];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                            vertexStart:0
                            vertexCount:4];
                
                /*
                // Point lights:

                [encoder setRenderPipelineState:_deferredLightPointRenderPipelineState];
                uniforms.radiance = simd_make_float3(1.0, 1.0, 1.0);
                // hack rotating light
                auto A = simd_matrix_translate(simd_make_float3(M_PI*sin(_frame_count*0.016),
                                                           M_PI*sin(_frame_count*0.015),
                                                           -M_PI_2+sin(_frame_count*0.014)));
                auto B = simd_matrix_rotate(_frame_count*0.1, simd_normalize(simd_make_float3(0.4,0.3,1)));
                uniforms.light_viewprojection_transform = B*A;
                uniforms.light_position = inverse(uniforms.light_viewprojection_transform).columns[3];
                uniforms.light_position /= uniforms.light_position.w;

                [encoder setFragmentTexture:_deferredLightImageBasedTexture atIndex:AAPLTextureIndexEnvironment];
                [encoder setFragmentBytes:&uniforms
                                   length:sizeof(MeshUniforms)
                                  atIndex:AAPLBufferIndexUniforms];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                            vertexStart:0
                            vertexCount:4
                            instanceCount:1];
                 */
                 
            }
        }
        
        
        {
            [self drawOverlay:encoder];
            [command_buffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
                dispatch_semaphore_signal(self->_atlas->_semaphore);
            }];
        }
               
        {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize.x = _model->_viewport_size.x;
            io.DisplaySize.y = _model->_viewport_size.y;
            CGFloat framebufferScale = _view.window.screen.backingScaleFactor ?: NSScreen.mainScreen.backingScaleFactor;
            io.DisplayFramebufferScale = ImVec2(framebufferScale, framebufferScale);
            ImGui_ImplMetal_NewFrame(descriptor);
            ImGui_ImplOSX_NewFrame(_view);
            ImGui::NewFrame();
            static bool show_demo_window = true;
            ImGui::ShowDemoWindow(&show_demo_window);
            ImGui::Render();
            ImDrawData* draw_data = ImGui::GetDrawData();
            // id <MTLRenderCommandEncoder> renderEncoder = [command_buffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
            [encoder pushDebugGroup:@"Dear ImGui rendering"];
            ImGui_ImplMetal_RenderDrawData(draw_data, command_buffer, encoder);
            [encoder popDebugGroup];
            // [renderEncoder endEncoding];

        }
        
        [encoder endEncoding];

    }
        
    {
        // Screen-space bloom by simple Gaussian blur
        _gaussianBlur.edgeMode = MPSImageEdgeModeClamp;
        
        [_gaussianBlur encodeToCommandBuffer:command_buffer
                               sourceTexture:_deferredLightColorAttachmentTexture
                          destinationTexture:_blurredTexture];

        // TODO: Different impulse reponses, like Unreal blur, convolution

        // TODO: we may want to mix in proportion to a texture so we can
        // increase blur under the HUD
        
        _imageAdd.primaryScale = 0.875f; //0.75f; // //15.0f/16.0f;
        _imageAdd.secondaryScale = 0.125f; // 0.25f; ////1.0f/16.0f;

        [_imageAdd encodeToCommandBuffer:command_buffer
        primaryTexture:_deferredLightColorAttachmentTexture
                        secondaryTexture:_blurredTexture
                      destinationTexture:_addedTexture];
        
        
    }
    
    @autoreleasepool {
        
        // TODO: autoreleasepool holds the currentDrawable as briefly as
        // possible, does this matter though?
        
        // *** BLOCKING CALL ***
        //
        // This call will block if none of the triple? buffers are available;
        // we should only be here because the display link expects it to be
        // available
        // id<CAMetalDrawable> currentDrawable = [update drawable];
        id<CAMetalDrawable> currentDrawable = [(CAMetalLayer*)(_view.layer) nextDrawable];
        
        id<MTLBlitCommandEncoder> encoder =  [command_buffer blitCommandEncoder];
        [encoder copyFromTexture:_addedTexture toTexture:currentDrawable.texture];
        [encoder endEncoding];
        [command_buffer presentDrawable:currentDrawable];
    }
    
    [command_buffer commit];
    
    ++_frame_count;
    
    //[_captureScope endScope];

}


-(void)drawableResize:(CGSize)drawableSize
{
    
    _model->_viewport_size.x = drawableSize.width;
    _model->_viewport_size.y = drawableSize.height;
    _model->_regenerate_uniforms();
    
    // TODO: The shadow map must be recreated at the new size PLUS some
    // angle-dependent padding, power of two, etc.
        
    // The G-buffers must be recreated at the new size

    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];

    // Common attributes
    
    descriptor.textureType = MTLTextureType2D;
    descriptor.width = drawableSize.width;
    descriptor.height = drawableSize.height;
    
    // colorAttachments
    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;

    descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
    descriptor.storageMode = MTLStorageModePrivate;
    _deferredLightColorAttachmentTexture = [_device newTextureWithDescriptor:descriptor];
    _deferredLightColorAttachmentTexture.label = @"Light G-buffer";

    descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
    descriptor.storageMode = MTLStorageModeMemoryless; // <--
    _deferredAlbedoMetallicColorAttachmentTexture = [_device newTextureWithDescriptor:descriptor];
    _deferredAlbedoMetallicColorAttachmentTexture.label = @"Albedo-metallic G-buffer";
    
    descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
    _deferredNormalRoughnessColorAttachmentTexture = [_device newTextureWithDescriptor:descriptor];
    _deferredNormalRoughnessColorAttachmentTexture.label = @"Normal-roughness G-buffer";
    
    descriptor.pixelFormat = MTLPixelFormatR32Float;
    _deferredDepthColorAttachmentTexture = [_device newTextureWithDescriptor:descriptor];
    _deferredDepthColorAttachmentTexture.label = @"Depth G-buffer";
    
    // depthAttachment
    descriptor.usage = MTLTextureUsageRenderTarget;
    descriptor.pixelFormat = MTLPixelFormatDepth32Float;
    _deferredDepthAttachmentTexture = [_device newTextureWithDescriptor:descriptor];
    _deferredDepthAttachmentTexture.label = @"Depth buffer";

    descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
    _blurredTexture = [_device newTextureWithDescriptor:descriptor];
    _blurredTexture.label = @"Blur target";
    
    _addedTexture = [_device newTextureWithDescriptor:descriptor];
    _addedTexture.label = @"Addition target";
    
}

/*
- (void)metalDisplayLink:(CAMetalDisplayLink *)link needsUpdate:(CAMetalDisplayLinkUpdate *)update {
    //NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [self renderToMetalLayer:update];
}
 */

@end




#if 0

Paste bin

/*
 {
 static double t = 0;
 t += 0.01;
 int i = (int) t;
 i = i % 4;
 wry::vertex v[3];
 v[0].v = _terrain_triangles[i+0];
 v[0].color = simd_uchar4{255, 255, 255, 255 };
 v[1].v = _terrain_triangles[i+1];
 v[1].color = simd_uchar4{255, 255, 255, 255 };
 v[2].v = _terrain_triangles[i+2];
 v[2].color = simd_uchar4{255, 255, 255, 255 };
 _atlas->push_triangle(v);
 }*/
/*
 {
 wry::vertex v[6];
 v[0].color = simd_uchar4{255, 255, 255, 255 };
 v[1].color = simd_uchar4{255, 255, 255, 255 };
 v[2].color = simd_uchar4{255, 255, 255, 255 };
 v[3].color = simd_uchar4{255, 255, 255, 255 };
 v[4].color = simd_uchar4{255, 255, 255, 255 };
 v[5].color = simd_uchar4{255, 255, 255, 255 };
 
 
 for (int i = 0; i != 10; ++i) {
 for (int j = 0; j != 10; ++j) {
 int x = i*64 + j*16;
 int y = i*16 + j*64;
 v[0].v.position = simd_make_float2(x   , y   );
 v[1].v.position = simd_make_float2(x+64, y+16);
 v[2].v.position = simd_make_float2(x+16, y+64);
 v[3].v.position = simd_make_float2(x+16, y+64);
 v[4].v.position = simd_make_float2(x+64, y+16);
 v[5].v.position = simd_make_float2(x+80, y+80);
 int a = wry::hash(i + j*123) & 1;
 int b = wry::hash((i+1) + j*123) & 1;
 int c = wry::hash(i + (j+1)*123) & 1;
 int d = wry::hash((i+1) + (j+1)*123) & 1;
 a = a | (b << 1) | (c << 2);
 b = (b << 1) | (c << 0) | (d << 2);
 auto f = [&](int a, wry::vertex* v) {
 switch (a) {
 case 0:
 v[0].v.texCoord = _terrain_triangles[0].texCoord;
 v[1].v.texCoord = _terrain_triangles[1].texCoord;
 v[2].v.texCoord = _terrain_triangles[2].texCoord;
 break;
 case 1:
 v[0].v.texCoord = _terrain_triangles[3].texCoord;
 v[1].v.texCoord = _terrain_triangles[1].texCoord;
 v[2].v.texCoord = _terrain_triangles[2].texCoord;
 break;
 case 2:
 v[0].v.texCoord = _terrain_triangles[2].texCoord;
 v[1].v.texCoord = _terrain_triangles[3].texCoord;
 v[2].v.texCoord = _terrain_triangles[1].texCoord;
 break;
 case 3:
 v[0].v.texCoord = _terrain_triangles[3].texCoord;
 v[1].v.texCoord = _terrain_triangles[4].texCoord;
 v[2].v.texCoord = _terrain_triangles[2].texCoord;
 break;
 case 4:
 v[0].v.texCoord = _terrain_triangles[1].texCoord;
 v[1].v.texCoord = _terrain_triangles[2].texCoord;
 v[2].v.texCoord = _terrain_triangles[3].texCoord;
 break;
 case 5:
 v[0].v.texCoord = _terrain_triangles[4].texCoord;
 v[1].v.texCoord = _terrain_triangles[2].texCoord;
 v[2].v.texCoord = _terrain_triangles[3].texCoord;
 break;
 case 6:
 v[0].v.texCoord = _terrain_triangles[2].texCoord;
 v[1].v.texCoord = _terrain_triangles[3].texCoord;
 v[2].v.texCoord = _terrain_triangles[4].texCoord;
 break;
 case 7:
 v[0].v.texCoord = _terrain_triangles[3].texCoord;
 v[1].v.texCoord = _terrain_triangles[4].texCoord;
 v[2].v.texCoord = _terrain_triangles[5].texCoord;
 break;
 default:
 v[0].v.texCoord = simd_make_float2(0, 0);
 v[1].v.texCoord = simd_make_float2(0, 0);
 v[2].v.texCoord = simd_make_float2(0, 0);
 }
 };
 f(a, v + 0);
 f(b, v + 3);
 
 
 */
/*
 v[0].v.texCoord = _terrain_triangles[k+0].texCoord;
 v[1].v.texCoord = _terrain_triangles[k+1].texCoord;
 v[2].v.texCoord = _terrain_triangles[k+2].texCoord;
 v[3].v.texCoord = _terrain_triangles[k+2].texCoord;
 v[4].v.texCoord = _terrain_triangles[k+1].texCoord;
 v[5].v.texCoord = _terrain_triangles[k+0].texCoord;
 */
/*
 _atlas->push_triangle(v + 0);
 _atlas->push_triangle(v + 3);
 }
 }
 }
 */


/*
 char buf[256];
 snprintf(buf, 256,
 "Futura Medium Condensed\n"
 "Frame number %ld\n"
 "Zwölf Boxkämpfer jagen Viktor quer über den großen Sylter Deich",
 (unsigned long) _frameNum);
 
 // now we finally need some application state, a console, which is
 // going to be an Array<string>, of which you can edit the last string
 
 // blinking cursor
 draw_text(gl::rect<float>{0,0, 1024, 1024 }, (_frameNum & 0x40) ? "_" : " ");
 */




//{

// step one:
//

//{
// when we rescale, the pixels stay anchored to
// rhs and this looks bad; they aren't anchored at midscreen

// map pixels to screen space
/*
 simd_float4x4 A = {
 {
 {2.0f / _viewportSize.x, 0.0f, 0.0f, 0.0f},
 {0.0, -2.0f / _viewportSize.y, 0.0f, 0.0f},
 {0.0f, 0.0f, 1.0f, 0.0f},
 {-1.0f, +1.0f, 0.0f, 1.0f}
 }
 };
 
 
 // float c = sqrt(0.5f);
 // float s = c;
 float c = 1.0f / sqrt(3.0f);
 float s = sqrt(2.0f) / sqrt(3.0f);
 simd_float4x4 B = {
 {
 {1.0f, 0.0f, 0.0f, 0.0f},
 {0.0f, c, s, 0.0f},
 {0.0f, -s, c, 0.0f},
 {0.0f, 0.0f, 0.0f, 1.0f}
 }
 };
 
 // rotate to mix Y and Z
 
 float dz = 4.0f;
 // translate camera back
 simd_float4x4 C = {
 {
 {1.0f, 0.0f, 0.0f, 0.0f },
 {0.0f, 1.0f, 0.0f, 0.0f },
 {0.0f, 0.0f, 1.0f, 0.0f },
 {0.0f, 0.0f, dz, 1.0f },
 }
 };
 
 // perspective project
 simd_float4x4 D = {
 {
 {1.0f, 0.0f, 0.0f, 0.0f },
 {0.0f, 1.0f, 0.0f, 0.0f },
 {0.0f, 0.0f, 1.0f, 1.0f },
 {0.0f, 0.0f, 0.0f, 0.0f },
 }
 };
 
 // zoom
 simd_float4x4 E = {
 {
 {dz*sqrtf(2), 0.0f, 0.0f, 0.0f },
 {0.0f, dz*sqrtf(2), 0.0f, 0.0f },
 {0.0f, 0.0f, 1.0f, 0.0f },
 {0.0f, 0.0f, 0.0f, 1.0f },
 }
 };
 
 A = simd_mul(B, A);
 A = simd_mul(C, A);
 A = simd_mul(D, A);
 A = simd_mul(E, A);
 */
/*
 uniforms.position_transform = matrix_float4x4{{
 {40.0f / _viewportSize.x, 0.0f, 0.0f, 0.0f},
 {0.0f, -40.0f / _viewportSize.y, 0.0f, -40.0f / _viewportSize.x},
 {0.0f, 0.0f, 1.0f, 0.0f},
 {-20.0f, +20.0f, 0.0f, 40.0f} // (0, 0) -> top left
 // {0.0f, 0.0f} // (0, 0) -> center
 }};
 */
//uniforms.position_transform = A;

//}

//}

// [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];

// _atlas->push_sprite(_font->charmap['a'].sprite_ + gl::vec2{100,100});

// _atlas->push_sprite(gl::sprite{{{0,0},{0,0}},{{2048,2048},{1,1}}}+gl::vec2{256,256});
//{
/*
 simd_int2 origin;
 {
 auto guard = std::unique_lock{_model->_mutex};
 origin = simd_make_int2(_model->_yx.x, _model->_yx.y);
 }
 auto zz = origin;
 simd_int2 c;
 c.x = (origin.x) >> 6;
 c.y = (origin.y) >> 6;
 
 origin.x &= 0x0000003F;
 origin.y &= 0x0000003F;
 origin.x -= 32;
 origin.y -= 32;
 
 //NSLog(@"%d %d\n", origin.x, origin.y);
 
 
 simd_int2 b;
 b.x = 32 + (int) _viewportSize.x;
 b.y = 32 + (int) _viewportSize.y;
 */

//for (int y = origin.y, i = -c.y; y < b.y; y += 64, ++i) {
//  for (int x = origin.x, j = -c.x; x < b.x; x += 64, ++j) {
//    auto s = _sprites[_model->_world(simd_make_int2(i, j)).x];
/*
 printf("%f %f %f %f\n",
 s.a.position.x,
 s.a.position.y,
 s.a.position.z,
 s.a.position.w);
 */

//     _atlas->push_sprite(s + simd_make_float2(x, y));
//_atlas->push_sprite(_sprites[48] + simd_make_float2(x, y));
//}
//}

//_atlas->push_sprite(_cube_sprites[0] + simd_make_float2(512+zz.x, 512+zz.y));

// now laboriously draw cube

/*
 {
 auto o = make<float4>(512+zz.x, 512+zz.y, 0, 0);
 // simd_make_float2(512+zz.x, 512+zz.y)
 wry::vertex v[4];
 float h = -128.0f / _viewportSize.y;
 v[0].v.position = make<float4>(-32.0f, -32.0f, 0.0f, 1.0f);
 v[1].v.position = make<float4>(-32.0f, +32.0f, 0.0f, 1.0f);
 v[2].v.position = make<float4>(-32.0f, -32.0f, h, 1.0f);
 v[3].v.position = make<float4>(-32.0f, +32.0f, h, 1.0f);
 v[0].color = simd_make_uchar4(255, 255, 255, 255);
 v[1].color = simd_make_uchar4(255, 255, 255, 255);
 v[2].color = simd_make_uchar4(255, 255, 255, 255);
 v[3].color = simd_make_uchar4(255, 255, 255, 255);
 
 {
 float2 a = _cube_sprites[1].a.texCoord;
 float2 b = _cube_sprites[1].b.texCoord;
 a += 96.0f / 2048;
 b -= 96.0f / 2048;
 auto c = simd_make_float2(a.x, b.y);
 auto d = simd_make_float2(b.x, a.y);
 v[0].v.texCoord = b;
 v[1].v.texCoord = c;
 v[2].v.texCoord = d;
 v[3].v.texCoord = a;
 }
 
 
 {
 simd_float4x4 R = {{
 { 0.0f, -1.0f, 0.0f, 0.0f},
 { 1.0f, 0.0f, 0.0f, 0.0f},
 { 0.0f, 0.0f, 1.0f, 0.0f},
 { 0.0f, 0.0f, 0.0f, 1.0f},
 }};
 for (int j = 0; j != 4; ++j) {
 wry::vertex u[4];
 for (int i = 0; i != 4; ++i) {
 u[i] = v[i];
 u[i].v.position += o;
 }
 _atlas->push_quad(u);
 
 for (int i = 0; i != 4; ++i) {
 v[i].v.position = simd_mul(R, v[i].v.position);
 }
 
 }
 }
 
 // finally, top of cube
 v[0].v.position = make<float4>(-32.0f, -32.0f, h, 1.0f);
 v[1].v.position = make<float4>(-32.0f, +32.0f, h, 1.0f);
 v[2].v.position = make<float4>(+32.0f, -32.0f, h, 1.0f);
 v[3].v.position = make<float4>(+32.0f, +32.0f, h, 1.0f);
 {
 auto a = (_cube_sprites[0].a.texCoord + _cube_sprites[0].b.texCoord) / 2;
 for (int i = 0; i != 4; ++i) {
 v[i].v.position += o;
 v[i].v.texCoord = a;
 }
 }
 _atlas->push_quad(v);
 
 }
 */


/*
 simd_int2 a = {-32, -32}, b;
 a.x += (int)yx.x;
 a.y += (int)yx.y;
 b.x = a.x + (int)_viewportSize.x;
 b.y = a.y + (int)_viewportSize.y;
 
 int kk = 0;
 for (int y = a.y; y < b.y; y += 64) {
 for (int x = a.x; x < b.x; x += 64) {
 _atlas->push_sprite(_sprites[4] + gl::vec2{x, y} + yx);
 }
 }
 */
//}


/*
 NSError *error = nil;
 MTKTextureLoader *loader = [[MTKTextureLoader alloc] initWithDevice: _device];
 
 NSURL* url = [NSURL fileURLWithPath:@"/Users/antony/Downloads/dawn.png"];
 
 _big_sky = [loader newTextureWithContentsOfURL:url options:nil error:&error];
 
 NSLog(@"%lu %lu\n", (unsigned long)_big_sky.width, (unsigned long)_big_sky.height);
 
 if(error)
 {
 NSLog(@"Error creating the texture from %@: %@", url.absoluteString, error.localizedDescription);
 return nil;
 }
 */


{
    std::size_t n = 256;
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
    descriptor.textureType = MTLTextureTypeCube;
    descriptor.pixelFormat = MTLPixelFormatRGBA32Float;
    descriptor.width = n;
    descriptor.height = n;
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.resourceOptions = MTLResourceStorageModeShared;
    _environmentMapFiltered = [_device newTextureWithDescriptor:descriptor];
    _environmentMapFiltered.label = @"Environment map";
    
    
    // simple env texture
    
    
    wry::matrix<simd_float4> a(n, n);
    
    auto foo = [&](NSUInteger i) {
        [_environmentMapFiltered replaceRegion:MTLRegionMake2D(0, 0, n, n)
                                   mipmapLevel:0
                                         slice:i
                                     withBytes:a.data()
                                   bytesPerRow:a.stride() * sizeof(simd_float4)
                                 bytesPerImage:0];
    };
    
    // dark ground
    a = make<float4>(0.25, 0.125, 0.0625, 0);
    foo(3);
    // bright sky
    a = make<float4>(0.25, 0.5, 1, 0);
    foo(2);
    // ground
    a.sub(n/2, 0, n/2, n) = make<float4>(0.5, 0.25, 0.125, 0);
    // feature
    a.sub(3*n/8, n/2, n/4, n/4) = make<float4>(1, 1, 1, 0) * 0.125;
    // lamp
    a.sub(n/8, n/8, n/2, n/16) = make<float4>(1, 1, 1, 1) * 10;
    foo(0);
    a.sub(n/8, n/8, n/2, n/16) = make<float4>(0, 1, 1, 0) * 10;
    foo(1);
    a.sub(n/8, n/8, n/2, n/16) = make<float4>(1, 0, 1, 0) * 10;
    foo(4);
    a.sub(n/8, n/8, n/2, n/16) = make<float4>(1, 1, 0, 0) * 10;
    foo(5);
    
}

std::vector<wry::sprite> _sprites;

auto img = wry::from_png_and_multiply_alpha(wry::path_for_resource("assets", "png"));
// wry::draw_bounding_box(img);
for (int y = 0; y != 256; y += 64) {
    for (int x = 0; x != 2048; x += 64) {
        auto v = img.sub(y, x, 64, 64);
        wry::draw_bounding_box(v);
        wry::sprite s = _atlas->place(v, float2{32, 32});
        _sprites.push_back(s);
    }
}


// auto v = wry::mesh::prism(16);

/*
 auto p = wry::mesh2::polyhedron::icosahedron();
 wry::mesh2::triangulation q;
 p.triangulate(q);
 q.tesselate();
 q.tesselate();
 q.tesselate();
 q.tesselate();
 for (auto& x : q.vertices) {
 x.xyz = simd_normalize(x.xyz);
 }
 wry::mesh2::mesh m;
 m.position_from(q);
 m.normals_from_average(0.000001, 0.9);
 m.texcoord_from_normal();
 m.tangent_from_texcoord();
 */

/*
 wry::mesh2::mesh m; // "cogwheel horn"
 {
 auto C = simd_matrix_rotate(-0.25,
 simd_normalize(simd_make_float3(1.0, 1.0f, 1.0f)));
 auto B = simd_matrix_translate(simd_make_float3(0.0, 0.0, 0.5f));
 auto A = simd_matrix_scale(0.95f);
 auto D = simd_mul(A, simd_mul(B, C));
 wry::mesh2::triangulation q;
 auto base = wry::mesh2::polygon::regular(12);
 base.stellate(+1);
 base.truncate(0.4f);
 //base.truncate(0.03f);
 //base.triangulate(q);
 //auto p = wry::mesh2::polyhedron::frustum(base, 0.75f);
 auto p = wry::mesh2::polyhedron::extrusion(base, 50, D);
 p.apply(simd_matrix_scale(0.5));
 p.triangulate(q);
 m.position_from(q);
 m.normal_from_triangle();
 m.normals_from_average(0.000001, 0.9);
 // m.texcoord_from_position(simd_matrix_scale(0.25f));
 m.texcoord_from_normal();
 m.tangent_from_texcoord();
 }
 */

/*
 wry::mesh2::mesh m;
 {
 wry::mesh2::triangulation q;
 auto p = wry::mesh2::polyhedron::icosahedron();
 // p.apply(simd_matrix_scale(simd_make_float3(-1,1,1)));
 p.stellate(1.0f);
 p.triangulate(q);
 m.position_from(q);
 m.normal_from_triangle();
 // m.texcoord_from_position(simd_matrix_scale(0.25f));
 m.texcoord_from_normal();
 m.tangent_from_texcoord();
 }*/



-(void) capturePhoto
{
    if (![_captureSession isRunning]) {
        [_captureSession startRunning];
    }
    [_capturePhotoOutput capturePhotoWithSettings:[AVCapturePhotoSettings photoSettingsWithFormat:@{
        (__bridge NSString *)kCVPixelBufferPixelFormatTypeKey : @((int) 'BGRA')
    }] delegate:self];
}

- (void)captureOutput:(AVCapturePhotoOutput *)output
didFinishProcessingPhoto:(AVCapturePhoto *)photo
error:(NSError *)error {
    NSLog(@"%s", __PRETTY_FUNCTION__);
    if (error) {
        NSLog(@"%@", error.localizedDescription);
    }
    
    CGImageRef image = [photo CGImageRepresentation];
    assert(image);
    
    size_t width = CGImageGetWidth(image);
    size_t height = CGImageGetHeight(image);
    size_t bytesPerRow = CGImageGetBytesPerRow(image);
    CGDataProviderRef dataProvider = CGImageGetDataProvider(image);
    assert(dataProvider);
    CFDataRef data = CGDataProviderCopyData(dataProvider);
    assert(data);
    const UInt8* bytePtr = CFDataGetBytePtr(data);
    
    /*
     wry::image z(height, width);
     for (size_t i = 0; i != z.rows(); ++i) {
     for (size_t j = 0; j != z.columns(); ++j) {
     z(i, j).bgra = ((wry::pixel*)(bytePtr + i * bytesPerRow))[j];
     }
     }
     char c[256];
     snprintf(c, 256, "/Users/antony/Desktop/captures/capture%03d.png", _photoCount);
     wry::to_png(z, c);
     */
    
    MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor new];
    textureDescriptor.textureType = MTLTextureType2D;
    textureDescriptor.pixelFormat = MTLPixelFormatRGBA8Unorm_sRGB;
    textureDescriptor.width = width;
    textureDescriptor.height = height;
    textureDescriptor.storageMode = MTLStorageModeShared;
    textureDescriptor.usage = MTLTextureUsageShaderRead;
    textureDescriptor.swizzle = MTLTextureSwizzleChannelsMake(MTLTextureSwizzleBlue,
                                                              MTLTextureSwizzleGreen,
                                                              MTLTextureSwizzleRed,
                                                              MTLTextureSwizzleAlpha);
    id<MTLTexture> texture = [_device newTextureWithDescriptor:textureDescriptor];
    [texture replaceRegion:MTLRegionMake2D(0, 0, width, height)
               mipmapLevel:0
                 withBytes:bytePtr
               bytesPerRow:bytesPerRow];
    texture.label = @"Still";
    
    CFRelease(data);
    
    _photoTexture = texture; // <-- why isn't this write a race condition?
    
}


// still camera
//_capturePhotoOutput = [AVCapturePhotoOutput new];
//_capturePhotoOutput.maxPhotoQualityPrioritization = AVCapturePhotoQualityPrioritizationQuality;
//[_captureSession addOutput:_capturePhotoOutput];


@synchronized (self->_capture_results) {
    
    id<MTLTexture> texture = nil;
    if (![self->_capture_results count]) {
        
        MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor new];
        
        textureDescriptor.textureType = MTLTextureType2D;
        textureDescriptor.pixelFormat = MTLPixelFormatRGBA8Unorm_sRGB;
        textureDescriptor.width = width;
        textureDescriptor.height = height;
        textureDescriptor.storageMode = MTLStorageModeShared;
        textureDescriptor.usage = MTLTextureUsageShaderRead;
        textureDescriptor.swizzle = MTLTextureSwizzleChannelsMake(MTLTextureSwizzleBlue,
                                                                  MTLTextureSwizzleGreen,
                                                                  MTLTextureSwizzleRed,
                                                                  MTLTextureSwizzleAlpha);
        
        id<MTLTexture> texture = [self->_device newTextureWithDescriptor:textureDescriptor];
        assert(texture);
        texture.label = @"Still";
        
        [self->_capture_results addObject:texture];
        
    } else {
        texture = [self->_capture_results objectAtIndex:0];
    }
    
    [texture replaceRegion:MTLRegionMake2D(self->_sliceCount, 0, 1, height)
               mipmapLevel:0
                 withBytes:((wry::uchar*) dataCopy) + (2 * 1920)
               bytesPerRow:bytesPerRow];
    ++self->_sliceCount;
    free(dataCopy);
    //self->_last_capture = dataCopy;
    
    NSLog(@"Processed %ld", [self->_capture_results count]);

    
    // photometry
    
    // receive incoming buffers
    dispatch_queue_t _capture_video_queue;
    dispatch_queue_t _process_video_queue;
    
    AVCaptureSession* _captureSession;
    AVCaptureVideoDataOutput* _captureVideoDataOutput;
    AVCaptureDevice* _captureDevice;
    AVCaptureDeviceInput* _captureDeviceInput;
    
    NSMutableArray<id<MTLTexture>>* _capture_results;
    
    id<MTLRenderPipelineState> _trivialRenderPipelineState;
    id<MTLTexture> _trivialTexture;
    id<MTLTexture> _radienceTexture;
    wry::matrix<wry::RGBA8Unorm_sRGB> _observationSlice;
    
    int _head;
    int _tail;
    
    
    - (void)captureInit {
        
        {
            dispatch_queue_t interactive =  dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0);
            _capture_video_queue = dispatch_queue_create_with_target("wry.capture_video",
                                                                     DISPATCH_QUEUE_SERIAL,
                                                                     interactive);
            
            dispatch_queue_t background =  dispatch_get_global_queue(QOS_CLASS_BACKGROUND, 0);
            _process_video_queue = dispatch_queue_create_with_target("wry.process_video",
                                                                     DISPATCH_QUEUE_SERIAL,
                                                                     background);
        }
        
        _captureSession = [AVCaptureSession new];
        _captureSession.sessionPreset = AVCaptureSessionPreset1920x1080;
        
        _captureDevice = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
        {
            NSError* error = nil;
            _captureDeviceInput = [[AVCaptureDeviceInput alloc] initWithDevice:_captureDevice error:&error];
            if (error) {
                NSLog(@"%@", [error localizedDescription]);
                abort();
            }
        }
        [_captureSession addInput:_captureDeviceInput];
        
        _captureVideoDataOutput = [AVCaptureVideoDataOutput new];
        _captureVideoDataOutput.videoSettings = @{
            (__bridge NSString *)kCVPixelBufferPixelFormatTypeKey : @((int) kCVPixelFormatType_32BGRA)
        };
        [_captureVideoDataOutput setSampleBufferDelegate:self queue:_capture_video_queue];
        [_captureSession addOutput:_captureVideoDataOutput];
        
        
        _capture_results = [NSMutableArray new];
        
        {
            MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
            descriptor.vertexFunction = [self newFunctionWithName:@"TrivialVertexFunction"];
            descriptor.fragmentFunction = [self newFunctionWithName:@"TrivialFragmentFunction"];
            descriptor.colorAttachments[0].pixelFormat = _drawablePixelFormat;
            _trivialRenderPipelineState = [self newRenderPipelineStateWithDescriptor:descriptor];
        }
        
        {
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
            descriptor.textureType = MTLTextureType2D;
            descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
            descriptor.height = 1080;
            descriptor.width = 1;
            descriptor.storageMode = MTLStorageModeShared;
            descriptor.usage = MTLTextureUsageShaderRead;
            _radienceTexture = [_device newTextureWithDescriptor:descriptor];
            _radienceTexture.label = @"Source radience";
            _trivialTexture = _radienceTexture;
            
        }
        
        {
            _observationSlice = wry::matrix<wry::RGBA8Unorm_sRGB>(1080, 1080);
        }
        
    }
    
    -(void) captureVideoStart {
        [_captureSession startRunning];
    }
    
    -(void) captureVideoStop {
        [_captureSession stopRunning];
        
    }
    
    -(void) captureOutput:(AVCaptureOutput *)output
didDropSampleBuffer:(CMSampleBufferRef)sampleBuffer
fromConnection:(AVCaptureConnection *)connection {
    NSLog(@"Dropped a video capture sample");
}
    
    -(void) captureOutput:(AVCaptureOutput *)output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
fromConnection:(AVCaptureConnection *)connection {
    
    NSLog(@"%s", __PRETTY_FUNCTION__);
    
    int head;
    @synchronized (self) {
        if (_head >= 1080) {
            return;
        } else {
            head = _head++;
        }
    }
    
    // we are on a high-priority queue and must do minimal work
    // we can't retain the sample buffer permanently so we need to make a copy
    // we can't deep copy any of these objects, we have to extract the
    // image data
    
    CMTime presentationTimeStamp = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    
    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    assert(imageBuffer);
    CVPixelBufferRef pixelBuffer = (CVPixelBufferRef) imageBuffer;
    CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    void* baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);
    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
    size_t dataSize = CVPixelBufferGetDataSize(pixelBuffer);
    
    void* dataCopy = malloc(dataSize);
    memcpy(dataCopy, baseAddress, dataSize);
    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    
    {
        simd_uchar4 white;
        white = 255;
        simd_uchar4 black;
        black = 0;
        
        [_trivialTexture replaceRegion:MTLRegionMake2D(0, head, 1, 1)
                           mipmapLevel:0
                             withBytes:&white
                           bytesPerRow:4];
        int thick = 16;
        if (head >= thick) {
            [_trivialTexture replaceRegion:MTLRegionMake2D(0, head-thick, 1, 1)
                               mipmapLevel:0
                                 withBytes:&black
                               bytesPerRow:4];
        }
    }
    
    dispatch_async(_process_video_queue, ^{
        
        CMTimeShow(presentationTimeStamp);
        
        int tail;
        @synchronized (self) {
            tail = self->_tail;
            if (self->_tail < 1080)
                ++self->_tail;
        }
        
        for (size_t j = 0; j != 1080; ++j) {
            self->_observationSlice(j, tail) = *((wry::RGBA8Unorm_sRGB*)(((uchar*) dataCopy) + 960 * 4 + bytesPerRow * j));
        }
        
        if (tail == 1079) {
            wry::to_png(self->_observationSlice, "/Users/antony/Desktop/captures/slice.png");
            std::terminate();
        }
        
        free(dataCopy);
        
    });
    
}
    
    -(void) captureLogic {
        
        static int state = 0;
        static NSUInteger trigger = 0;
        
        if (_frame_count < trigger)
            return;
        
        switch (state) {
            case 0:
                [self captureVideoStart];
                state = 10;
                break;
            case 10:
                break;
            default:
                abort();
        }
        
        
        
        
    }

    
    // [self captureInit];
    
    if (false) {
        // load input data
        id<MTLTexture> lightTexture = [self newTextureFromResource:@"../captures/slice"
                                                            ofType:@"png"];
        
        // make mask data
        id<MTLTexture> maskTexture = nil;
        {
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
            descriptor.textureType = MTLTextureType2D;
            descriptor.pixelFormat = MTLPixelFormatRGBA32Float;
            descriptor.width = 1;
            descriptor.height = 240;
            descriptor.usage = MTLTextureUsageShaderRead;
            descriptor.storageMode = MTLStorageModeShared;
            maskTexture = [_device newTextureWithDescriptor:descriptor];
            wry::Array<simd_float4> mask(240, make<float4>(0.0f, 0.0f, 0.0f, 0.0f));
            for (int i = 0; i != 8; ++i) {
                for (int j = 0; j != 8; ++j) {
                    mask[i * 16 + (240 - 166) + j] = make<float4>(1.0f, 1.0f, 1.0f, 1.0f);
                }
            }
            [maskTexture replaceRegion:MTLRegionMake2D(0,0,1,240)
                           mipmapLevel:0
                             withBytes:mask.data()
                           bytesPerRow:16];
            assert(maskTexture);
            
        }
        
        // make result texture
        {
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
            descriptor.textureType = MTLTextureType2D;
            descriptor.pixelFormat = MTLPixelFormatRGBA32Float;
            descriptor.width = 1080;
            descriptor.height = 1080;
            descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
            descriptor.storageMode = MTLStorageModeShared;
            _trivialTexture = [_device newTextureWithDescriptor:descriptor];
            assert(_trivialTexture);
            _trivialTexture.label = @"trivial Texture";
        }
        
        // make pipeline
        id<MTLComputePipelineState> pipelineState = nil;
        {
            //MTLComputePipelineDescriptor* descriptor = [MTLComputePipelineDescriptor new];
            //descriptor.computeFunction = [self newFunctionWithName:@"DepthProcessing"];
            //descriptor.label = @"DepthProcessing";
            NSError* error = nil;
            pipelineState = [_device newComputePipelineStateWithFunction:[self newFunctionWithName:@"DepthProcessing"]
                                                                   error:&error];
            assert(!error);
            assert(pipelineState);
        }
        
        id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        [encoder setComputePipelineState:pipelineState];
        [encoder setTexture:_trivialTexture atIndex:0];
        [encoder setTexture:lightTexture atIndex:1];
        [encoder setTexture:maskTexture atIndex:2];
        
        NSUInteger w = pipelineState.threadExecutionWidth;
        NSUInteger h = pipelineState.maxTotalThreadsPerThreadgroup / w;
        MTLSize threadsPerThreadgroup = MTLSizeMake(w, h, 1);
        
        MTLSize threadsPerGrid = MTLSizeMake(_trivialTexture.width, _trivialTexture.height, 1);
        
        [encoder dispatchThreadgroups:threadsPerGrid threadsPerThreadgroup:threadsPerThreadgroup];
        [encoder endEncoding];
        [commandBuffer commit];
        
        
    }
    
    
    - (void)renderToMetalLayer2:(nonnull CAMetalLayer*)metalLayer
    {
        //[self captureLogic];
        
        id<MTLCommandBuffer> command_buffer = [_commandQueue commandBuffer];
        id<CAMetalDrawable> currentDrawable = [metalLayer nextDrawable];
        id <MTLRenderCommandEncoder> encoder = nil;
        {
            MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor new];
            descriptor.colorAttachments[AAPLColorIndexColor].clearColor = MTLClearColorMake(1, 0, 1, 0);
            descriptor.colorAttachments[AAPLColorIndexColor].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexColor].storeAction = MTLStoreActionStore;
            descriptor.colorAttachments[AAPLColorIndexColor].texture = currentDrawable.texture;
            encoder = [command_buffer renderCommandEncoderWithDescriptor:descriptor];
        }
        
        
        [encoder setRenderPipelineState:_trivialRenderPipelineState];
        [encoder setVertexBuffer:_screenTriangleStripVertexBuffer offset:0 atIndex:AAPLBufferIndexVertices];
        if (_trivialTexture) {
            [encoder setFragmentTexture:_trivialTexture atIndex:AAPLTextureIndexColor];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        }
        
        [encoder endEncoding];
        
        [command_buffer presentDrawable:currentDrawable];
        [command_buffer commit];
        
        ++_frame_count;
        
    }

    
    {
        _tireMesh.instances[ 0].model_transform = simd_matrix_translate(simd_make_float3( -1.0f,  -1.0f, -0.5f));
        _tireMesh.instances[ 1].model_transform = simd_matrix_translate(simd_make_float3( -1.0f,  -1.0f, +0.5f));
        _tireMesh.instances[ 2].model_transform = simd_matrix_translate(simd_make_float3(  0.0f,  -1.0f, -0.5f));
        _tireMesh.instances[ 3].model_transform = simd_matrix_translate(simd_make_float3(  0.0f,  -1.0f, +0.5f));
        _tireMesh.instances[ 4].model_transform = simd_matrix_translate(simd_make_float3( +1.0f,  -1.0f, -0.5f));
        _tireMesh.instances[ 5].model_transform = simd_matrix_translate(simd_make_float3( +1.0f,  -1.0f, +0.5f));
        
        float2 center_of_turn = make_float2(0.5f, 2.0 / sin(_frame_count * 0.01));
        
        for (NSUInteger i = 0; i != _tireMesh.instanceCount; ++i) {
            float4x4 A = _tireMesh.instances[i].model_transform;
            float2 b = simd_normalize(A.columns[3].xz - center_of_turn);
            float4x4 B = simd_matrix(make<float4>(  b.y ,  0.0f, -b.x,  0.0f  ),
                                     make<float4>(  0.0f,  1.0f,0,0),
                                     make<float4>(b.x,0,b.y,0),
                                     make<float4>(0,0,0,1));
            float4x4 C = A * B;
            float4x4 D = simd_transpose(simd_inverse(C));
            _tireMesh.instances[i].model_transform = C;
            _tireMesh.instances[i].inverse_transpose_model_transform = D;
        }
        
        {
            auto A = simd_matrix_scale(0.002f);
            auto B = simd_matrix_rotate(-M_PI_F/2, simd_make_float3(1,0,0));
            auto C = simd_matrix_rotate(_frame_count * 0.01, simd_make_float3(0,1,0));
            _chassisMesh.instances[0].model_transform = A * C * B;
            _chassisMesh.instances[0].inverse_transpose_model_transform
            = simd_transpose(simd_inverse(C * B));
            // = matrix_identity_float4x4;
            
        }
    }
    
    WryMesh* _groundMesh;
    WryMesh* _tireMesh;
    WryMesh* _chassisMesh;
    
    {
        {
            
            
            
            using namespace simd;
            
            // low-poly tire with superquadric cross-section
            
            wry::mesh::mesh m;
            // m.add_face_disk(8);
            // m.add_edges_polygon(4);
            /*
             m.add_edges_superquadric(8);
             m.extrude(12, vector4(0.0f, M_PI / 6, 0.0f, 0.0f));
             m.edges.clear();
             m.transform_with_function([](float4 position, float4 coordinate) {
             float4x4 A = simd_matrix_translate(vector3(-2.0f, 0.0f, +coordinate.y));
             float4x4 B = simd_matrix_rotate(-coordinate.y, vector3(0.0f, 1.0f, 0.0f));
             //float4x4 C = simd_matrix_translate(vector3(0.0f, -10.0f, 0.0f));
             float4x4 D = simd_matrix_scale(0.5f);
             //position = D * (C * (B * (A * position)));
             // position = D * B * A * position;
             position = D * B * A * position;
             return position;
             });
             m.reparameterize_with_matrix(simd_matrix_scale(vector3(2.0f, 8.0f, 1.0f)));
             */
            m.add_quads_box(float4{-8,-16,-8,1}, float4{8,0,8,1});
            m.colocate_similar_vertices();
            m.combine_duplicate_vertices();
            m.triangulate();
            m.strip();
            m.reindex_for_strip();
            m.MeshVertexify();
            
            _groundMesh = [[WryMesh alloc] initWithDevice:_device];
            _groundMesh.vertexBuffer = newBufferWithArray(m.vertices);
            _groundMesh.indexBuffer = newBufferWithArray(m.hack_triangle_strip);
            
            _groundMesh.emissiveTexture = [self newTextureFromResource:@"black"
                                                                ofType:@"png"];
            //_cube_colors = [self newTextureFromResource:@"rustediron2_basecolor" ofType:@"png"];
            //_cube_colors = [self newTextureFromResource:@"albedo" ofType:@"png"];
            _groundMesh.albedoTexture = [self newTextureFromResource:@"wavy-sand_albedo" ofType:@"png"];
            _groundMesh.metallicTexture= [self newTextureFromResource:@"wavy-sand_metallic"
                                                               ofType:@"png"];
            _groundMesh.normalTexture = [self newTextureFromResource:@"wavy-sand_normal-ogl"
                                                              ofType:@"png"
                                                     withPixelFormat:MTLPixelFormatRGBA8Unorm];
            _groundMesh.roughnessTexture = [self newTextureFromResource:@"wavy-sand_roughness"
                                                                 ofType:@"png"];
            _groundMesh.instanceCount = 1;
            _groundMesh.instances[0].albedo = 1.0f;
            _groundMesh.instances[0].model_transform = matrix_identity_float4x4;
            _groundMesh.instances[0].inverse_transpose_model_transform = matrix_identity_float4x4;
            
            
            m = wry::mesh::mesh();
            m.add_edges_superquadric(8);
            m.extrude(12, vector4(0.0f, M_PI_F / 6.0f, 0.0f, 0.0f));
            m.edges.clear();
            m.transform_with_function([](float4 position, float4 coordinate) {
                float4x4 A = simd_matrix_translate(vector3(-2.0f, 0.0f, +coordinate.y));
                float4x4 B = simd_matrix_rotate(-coordinate.y, vector3(0.0f, 1.0f, 0.0f));
                float4x4 C = simd_matrix_rotate(M_PI/2, vector3(1.0f, 0.0f, 0.0f));
                float4x4 D = simd_matrix_translate(vector3(0.0f, 3.0f, 2.0f));
                float4x4 E = simd_matrix_scale(0.125f);
                //float4x4 C = simd_matrix_translate(vector3(0.0f, -10.0f, 0.0f));
                //float4x4 D = simd_matrix_scale(0.5f);
                //position = D * (C * (B * (A * position)));
                // position = D * B * A * position;
                position = E * D * C * B * A * position;
                return position;
            });
            
            //m.reparameterize_with_matrix(simd_matrix_scale(vector3(2.0f, 8.0f, 1.0f)));
            m.colocate_similar_vertices();
            m.combine_duplicate_vertices();
            m.triangulate();
            m.copy_under_transform(simd_matrix_rotate(M_PI, simd_make_float3(0,1,0)));
            m.strip();
            m.reindex_for_strip();
            m.MeshVertexify();
            
            _tireMesh = [[WryMesh alloc] initWithDevice:_device];
            _tireMesh.vertexBuffer = newBufferWithArray(m.vertices);
            _tireMesh.indexBuffer = newBufferWithArray(m.hack_triangle_strip);
            
            _tireMesh.emissiveTexture = [self newTextureFromResource:@"black"
                                                              ofType:@"png"];
            _tireMesh.albedoTexture = [self newTextureFromResource:@"black" ofType:@"png"];
            _tireMesh.metallicTexture= [self newTextureFromResource:@"white"
                                                             ofType:@"png"];
            _tireMesh.normalTexture = [self newTextureFromResource:@"blue"
                                                            ofType:@"png"
                                                   withPixelFormat:MTLPixelFormatRGBA8Unorm];
            _tireMesh.roughnessTexture = [self newTextureFromResource:@"darkgray"
                                                               ofType:@"png"];
            _tireMesh.instanceCount = 6;
            
            /*
             m = wry::mesh::mesh();
             m.add_quads_box(float4{-1.5f,0.875f,-0.875f,1.0f}, float4{1.5f,1.0f,-0.8125f,1.0f});
             m.add_quads_box(float4{-1.5f,0.875f,0.8125f,1.0f}, float4{1.5f,1.0f,0.875f,1.0f});
             m.add_quads_box(float4{-1.0625f,0.875f,-0.75f,1.0f}, float4{-0.9375,1.0f,0.75f,1.0f});
             m.add_quads_box(float4{-0.0625f,0.875f,-0.75f,1.0f}, float4{+0.0625,1.0f,0.75f,1.0f});
             m.add_quads_box(float4{+0.9375,0.875f,-0.75f,1.0f}, float4{+1.0625f,1.0f,0.75f,1.0f});
             m.colocate_similar_vertices();
             m.repair_texturing(4.0f);
             m.combine_duplicate_vertices();
             m.triangulate();
             m.strip();
             m.reindex_for_strip();
             m.MeshVertexify();
             */
            
            m = from_obj("/Users/antony/Desktop/assets/16747_Mining_Truck_v1.obj");
            m.MeshVertexify();
            
            
            _chassisMesh = [[WryMesh alloc] initWithDevice:_device];
            _chassisMesh.vertexBuffer = newBufferWithArray(m.vertices);
            _chassisMesh.indexBuffer = newBufferWithArray(m.hack_triangle_strip);
            
            _chassisMesh.emissiveTexture = [self newTextureFromResource:@"black"
                                                                 ofType:@"png"];
            _chassisMesh.albedoTexture = [self newTextureFromResource:@"white" ofType:@"png"];
            _chassisMesh.metallicTexture= [self newTextureFromResource:@"black"
                                                                ofType:@"png"];
            _chassisMesh.normalTexture = [self newTextureFromResource:@"blue"
                                                               ofType:@"png"
                                                      withPixelFormat:MTLPixelFormatRGBA8Unorm];
            _chassisMesh.roughnessTexture = [self newTextureFromResource:@"white"
                                                                  ofType:@"png"];
            _chassisMesh.instanceCount = 1;
            auto A = simd_matrix_scale(0.002f);
            auto B = simd_matrix_rotate(-M_PI_F/2, simd_make_float3(1,0,0));
            _chassisMesh.instances[0].model_transform = A * B;
            _chassisMesh.instances[0].inverse_transpose_model_transform
            = simd_transpose(simd_inverse(B));
            // = matrix_identity_float4x4;
            
            
        }
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
        
        
    }
    
    [_chassisMesh drawWithRenderCommandEncoder:render_command_encoder commandBuffer:command_buffer];
    [_tireMesh drawWithRenderCommandEncoder:render_command_encoder commandBuffer:command_buffer];

    [_chassisMesh drawWithRenderCommandEncoder:encoder commandBuffer:command_buffer];
    [_tireMesh drawWithRenderCommandEncoder:encoder commandBuffer:command_buffer];
    [_groundMesh drawWithRenderCommandEncoder:encoder commandBuffer:command_buffer];

    {
        // ultra-paranoid
    }
    
    
    /*
     id<CAMetalDrawable> currentDrawable = nil;
     {
     // wry::timer t("nextDrawable");
     //auto a = mach_absolute_time();
     auto a = std::chrono::steady_clock::now();
     currentDrawable = [metalLayer nextDrawable];
     auto b = std::chrono::steady_clock::now();
     static std::chrono::steady_clock::time_point t0, t1;
     auto c = std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
     auto d = std::chrono::duration_cast<std::chrono::microseconds>(b - t0).count();
     auto e = std::chrono::duration_cast<std::chrono::microseconds>(a - t0).count();
     //b.time_since_epoch()
     
     //if (c > 999) {
     // printf("! %lld\n", c);
     //printf(" wake-to-wake  %lld\n", d);
     //printf(" wake-to-sleep %lld\n", e);
     //}
     t0 = b;
     t1 = a;
     
     }
     
     {
     id<MTLBlitCommandEncoder> encoder =  [command_buffer blitCommandEncoder];
     [encoder copyFromTexture:_addedTexture toTexture:currentDrawable.texture];
     [encoder endEncoding];
     }
     
     [command_buffer presentDrawable:currentDrawable];
     currentDrawable = nil;
     [command_buffer commit];
     */
    
    // capture
    
    id<MTLCaptureScope> _captureScope;
    
    
    {
        MTLCaptureManager* captureManager = [MTLCaptureManager sharedCaptureManager];
        _captureScope = [captureManager newCaptureScopeWithCommandQueue:_commandQueue];
        [_captureScope setLabel:@"Custom"];
        [captureManager setDefaultCaptureScope:_captureScope];
    }
    
    
#endif
