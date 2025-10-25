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

#include <sqlite3.h>

#include "WryMesh.h"
#include "WryRenderer.h"

#include "SpriteAtlas.hpp"
#include "csv.hpp"
#include "debug.hpp"
#include "font.hpp"
#include "json.hpp"
#include "mesh.hpp"
#include "palette.hpp"
#include "platform.hpp"
#include "sdf.hpp"
#include "text.hpp"
#include "Wavefront.hpp"
#include "world.hpp"

#include "save.hpp"

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
        
    wry::SpriteAtlas* _atlas;
    wry::Font* _font;
                    
    
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
    
    wry::Palette<wry::Value> _controls;

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
            
            _atlas = new wry::SpriteAtlas(2048, device);
            _font = new wry::Font(build_font(*_atlas));
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
            
            f(_furnace_mesh, "furnace.obj");
            f(_mine_mesh, "mine.obj");
            f(_truck_mesh, "truck2.obj");
        }
                    
        {
            
            Table<wry::String, i64> _name_to_opcode;
            Table<i64, wry::String> _opcode_to_name;

//            try {
//                auto x = json::from_file<ContiguousDeque<String>>("opcodes.json");
//                ulong i = 0;
//                for (const String& y : x)
//                    _name_to_opcode[y] = i++;
//                
//                //json::serializer s;
//                //serialize(x.as_view(), s);
//                //printf("%.*s\n", (int) s.s.chars.size(), (char*) s.s.chars.data());
//            } catch (...) {
//                
//            }
            
            for (auto&& [k, v] : wry::OPCODE_NAMES) {
                // printf("\"%s\" <-> %lld\n", v, k);
                _opcode_to_name.emplace(k, v);
                auto h = _name_to_opcode._inner._hasher.get_hash(v + 7);
                // printf("hash \"%s\" -> %llu\n", v+7, h);
                _name_to_opcode.emplace(v + 7, k);
            }
            
            for (auto&& [k, v] : wry::OPCODE_NAMES) {
                // printf("\"%s\" <-> %lld\n", v, k);
                const String& s = _opcode_to_name[k];
                // printf("    %lld -> %.*s\n", k, (int) s.chars.size(), (char const*) s.chars.data());
                int64_t i = _name_to_opcode[v + 7];
                // printf("    %s -> %lld\n", v + 7, i);
            }

                
            try {
                auto x = json::from_file<ContiguousDeque<ContiguousDeque<String>>>("assets.json");
                ulong i = 0;
                for (const ContiguousDeque<String>& y : x) {
                    ulong j = 0;
                    for (const String& z : y) {
                        // printf("loading image for %.*s\n", (int) z.chars.size(), (const char*) z.chars.data());
                        simd_float4 coordinate = make<float4>(j / 32.0f, i / 32.0f, 0.0f, 1.0f);
                        // auto h = _name_to_opcode._inner._hasher.get_hash(z);
                        // printf("hash \"%.*s\" -> %llu\n", (int) z.chars.size(), (const char*) z.chars.data(), h);
                        auto p = _name_to_opcode.find(z);
                        // printf("p choice\n");
                        if (p == _name_to_opcode.end()) {
                            // printf("p is equal to end\n");
                            // printf("No opcode found for \"%.*s\"\n", (int) z.chars.size(), (const char*) z.chars.data());
                            // auto mq = _name_to_opcode[z];
                            // printf("Forced lookup is %lld\n", mq);
                        }
                        else /* if (p != _name_to_opcode.end()) */ {
                            // printf("p is not equal to end\n");
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
            _controls._payload = wry::matrix<wry::Value>(nn, 2);
            
            i64 j = 0;
            for (i64 i = 0; i != _name_to_opcode.size(); ++i) {
                if (_opcode_to_coordinate.contains(i)) {
                    
                    _controls._payload[j % nn, j / nn] = value_make_opcode((int)i);
                    ++j;
                                        
                } else {
                    auto s = _opcode_to_name[i].chars.as_view();
                    if (s.size())
                        printf("Sprite not found for %.*s\n", (int) s.size(), (char*) s.data());
                }
            }
            
            // printf("%lld\n", j);
            
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
        
        wry::ContiguousDeque<wry::SpriteVertex> v;
        
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
                // auto& the_tile = _model->_world->_value_for_coordinate[xy];
                // the_tile = _model->_holding_value;
                // _model->_world->_value_for_coordinate.write(xy, _model->_holding_value);
                
                // these notifications happen logically between steps and are
                // excused from transactions (hopefully)
                
                // the_tile.notify_occupant(&_model->_world);
                // notify_by_world_coordinate(_model->_world, xy);
                
                {
                    Player::Action a;
                    a.tag = Player::Action::WRITE_VALUE_FOR_COORDINATE;
                    a.coordinate = xy;
                    a.value = _model->_holding_value;
                    _model->_local_player->_queue.push_back(std::move(a));
                }
                
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
                // auto& the_tile = _model->_world->_value_for_coordinate[xy];
                // the_tile = k;
                //_model->_world->_value_for_coordinate.write(xy, k);
                // the_tile.notify_occupant(&_model->_world);
                // notify_by_world_coordinate(_model->_world, xy);
            }
        }
        
        
        for (difference_type j = 0; j != m.major(); ++j) {
            for (difference_type i = 0; i != m.minor(); ++i) {
                Value a = m[i, j];
                if (a.is_opcode()) {
                    
                    SpriteVertex c;
                    
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
                // TODO: actual window size
                z = wry::drawOverlay_draw_text(_font, _atlas, {_font->height / 2, y, 1920, 1080}, p->second, color);
                // TODO: detect multiline (draw backwards? shift? prep the size?  linebreak?)
                // There are a lot of ways to skin a line...
                
                // A spite atlas might be associated with multiple fonts
                
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
            z = wry::drawOverlay_draw_text(_font, _atlas, {_font->height / 2, y, 1920, 1080}, *p, color);
            if (first) {
                wry::drawOverlay_draw_text(_font, _atlas, wry::rect<float>{z.x, z.y, 1920, 1080 }, (_frame_count & 0x40) ? "_" : " ", color);
                first = false;
            }
        }
    }
    _atlas->commit(encoder);
}

-(void)resetCursor {
    [_cursor set];
}

- (void)render
{

    using namespace ::simd;
    using namespace ::wry;

    // Service the garbage collector
    _model->shade_roots();
    wry::mutator_handshake();

    // Advance the world state
    wry::epoch::pin_this_thread();
    World* old_world = std::exchange(_model->_world, _model->_world->step());
    // Write barrier
    garbage_collected_shade(old_world);
    wry::epoch::unpin_this_thread();


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

        auto tnow = world_get_time(_model->_world);
        auto&& entities = _model->_world->_entity_for_entity_id;
        
        NSUInteger quad_count = /*entities->data.size()*/ 10 * 4 + 1000 + 2;
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

#if 1
        
        // kludge out the contents of the entity mapping
        std::vector<Entity const*> ptrs;
        entities.inner.parallel_for_each([&ptrs](auto&& k, auto&& v) {
            ptrs.push_back(v.first);
        });
        // TODO:
        // Make a new entity-coordinate table that doesn't imply exclusive
        // ownership
        // Lookup by region
                
        // for (const auto& [qi, q] : entities->data) {
            // Entity* q = entities[qi];
        for (Entity const* q : ptrs) {
            
            if (auto p = dynamic_cast<const wry::Machine*>(q)) { // ugh
                
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
                for (int i = (int) p->_stack.size(); i--;) {
                    location.z += 0.5;
                    wry::Value value = p->_stack[i];
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
                
            } else if (auto p = dynamic_cast<const LocalizedEntity*>(q)){
                
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
                
                if (auto r = dynamic_cast<const Source*>(q)) {
                    s = _mine_mesh;
                } else if (auto r = dynamic_cast<const Sink*>(q)) {
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

                
                if (auto r = dynamic_cast<const Source*>(q)) {
                    
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
#endif

        for (int i = grid_bounds.a.x; i != grid_bounds.b.x; ++i) {
            for (int j = grid_bounds.a.y; j != grid_bounds.b.y; ++j) {

                simd_float4 location = make<float4>(i, j, 0.1f, 1.0f);
                simd_float4 coordinate = make<float4>(0.0f / 32.0f, 2.0f / 32.0f, 0.0f, 1.0f);
                
                {
                    //wry::Value q = _model->_world->_value_for_coordinate.read(wry::Coordinate{i, j});
                    wry::Value q = {};
                    (void) _model->_world->_value_for_coordinate.try_get(wry::Coordinate{i, j}, q);
                    // printf("(%d, %d)=%llx -> (%d) %llx\n", i, j, wry::Coordinate{i, j}.data(), q._data);
                    if (q.is_int64_t()) {
                        coordinate = make<float4>((q.as_int64_t() & 15) / 32.0f, 13.0f / 32.0f, 0.0f, 1.0f);
                    } else if (q.is_opcode()) {
                        //printf("q is opcode\n");
                        auto p = _opcode_to_coordinate.find(q.as_opcode());
                        if (p != _opcode_to_coordinate.end()) {
                            coordinate = p->second;
                        } else {
                            printf("q is opcode but was not found, %d\n", q.as_opcode());
                        }
                    } else {
                        //printf("q is mystery\n");
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
            //ulong value = _model->_world->get(z);
            //++value;
            //_model->_world->set(z, value);
            
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

@end
