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

#include "WryMesh.h"
#include "WryRenderer.h"

#include "atlas.hpp"
#include "debug.hpp"
#include "font.hpp"
#include "mesh.hpp"
#include "platform.hpp"
#include "obj.hpp"
#include "json.hpp"

@implementation WryRenderer
{
    
    // link to rest of program
    
    std::shared_ptr<wry::model> _model;
    
    MTLPixelFormat _drawablePixelFormat;

    // view-only state
        
    size_t _frame_count;
    simd::float2 _viewport_size;
            
    id<MTLBuffer> _screenTriangleStripVertexBuffer;
    id<MTLCommandQueue> _commandQueue;
    id<MTLDepthStencilState> _enabledDepthStencilState;
    id<MTLDepthStencilState> _disabledDepthStencilState;
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
    
    wry::table<ulong, simd_float4> _opcode_to_coordinate;

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
    wry::matrix<wry::RGBA8Unorm_sRGB> image = wry::from_png(wry::path_for_resource([name UTF8String], [ext UTF8String]));
    wry::multiply_alpha_inplace(image);
    
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
    descriptor.textureType = MTLTextureType2D;
    descriptor.pixelFormat = pixelFormat;
    descriptor.width = image.get_major();
    descriptor.height = image.get_minor();
    descriptor.mipmapLevelCount = std::countr_zero(descriptor.width | descriptor.height);
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageShaderRead;
    
    id<MTLTexture> texture = [_device newTextureWithDescriptor:descriptor];
    [texture replaceRegion:MTLRegionMake2D(0, 0, image.get_major(), image.get_minor())
                    mipmapLevel:0
                      withBytes:image.data()
                    bytesPerRow:image.bytes_per_row()];
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
    render_pipeline_descriptor.vertexFunction = [self newFunctionWithName:@"SplitSumVertex"];
    render_pipeline_descriptor.fragmentFunction = [self newFunctionWithName:@"SplitSumFragment"];
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
            MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
            descriptor.label = @"Shadow map pipeline";
            descriptor.vertexFunction = [self newFunctionWithName:@"DeferredGBufferVertexShader"];
            descriptor.fragmentFunction = [self newFunctionWithName:@"DeferredGBufferShadowFragmentShader"];
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

            descriptor.vertexFunction = [self newFunctionWithName:@"DeferredGBufferVertexShader"];
            descriptor.fragmentFunction = [self newFunctionWithName:@"DeferredGBufferFragmentShader"];
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
        
            descriptor.vertexFunction = [self newFunctionWithName:@"meshLightingVertex"];
            descriptor.fragmentFunction = [self newFunctionWithName:@"meshLightingFragment"];
            descriptor.label = @"Deferred image-based light";
            _deferredLightImageBasedRenderPipelineState = [self newRenderPipelineStateWithDescriptor:descriptor];

            descriptor.vertexFunction = [self newFunctionWithName:@"meshLightingVertex"];
            descriptor.fragmentFunction = [self newFunctionWithName:@"meshPointLightFragment"];
            descriptor.label = @"Deferred shadowcasting directional light";
            _deferredLightDirectionalShadowcastingRenderPipelineState = [self newRenderPipelineStateWithDescriptor:descriptor];

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
            _darkgray = [self newTextureFromResource:@"darkgray" ofType:@"png"];
            
            
            MeshInstanced i;
            i.model_transform = simd_matrix_rotate(-M_PI_2, simd_make_float3(-1.0f, 0.0f, 0.0f));
            i.inverse_transpose_model_transform = simd_inverse(simd_transpose(i.model_transform));
            i.albedo = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);
            _instanced_things = [_device newBufferWithBytes:&i length:sizeof(i) options:MTLStorageModeShared];
            
            // auto b = json::from_string<json::value>(wry::string_from_file("/Users/antony/Desktop/assets/opcodes.json"));

            table<wry::string, ulong> _name_to_opcode;

            if (auto x = json::from_file<array<string>>("/Users/antony/Desktop/assets/opcodes.json");
                x.is_ok()) {
                ulong i = 0;
                for (const string& y : x._ok.value)
                    _name_to_opcode[y] = i++;
                
                json::serializer s;
                serialize(x._ok.value.as_view(), s);
                printf("%s\n", s.s.c_str());
            }
            
            if (auto x = json::from_file<array<array<string>>>("/Users/antony/Desktop/assets/assets.json");
                x.is_ok()) {
                ulong i = 0;
                for (const array<string>& y : x._ok.value) {
                    ulong j = 0;
                    for (const string& z : y) {
                        simd_float4 coordinate = simd_make_float4(j / 32.0f, i / 32.0f, 0.0f, 1.0f);
                        auto p = _name_to_opcode.find(z);
                        if (p != _name_to_opcode.end())
                            _opcode_to_coordinate[p->second] = coordinate;
                        ++j;
                    }
                    ++i;
                }
            }            

        }
        
    }
    
    NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);

    return self;
}


- (void) drawOverlay:(id<MTLRenderCommandEncoder>)encoder {
    
    [encoder setRenderPipelineState:_overlayRenderPipelineState];
    MyUniforms uniforms;
    uniforms.position_transform = matrix_float4x4{{
        {2.0f / _viewport_size.x, 0.0f, 0.0f},
        {0.0f, -2.0f / _viewport_size.y, 0.0f, 0.0f},
        { 0.0f, 0.0f, 1.0f, 0.0f },
        {-1.0f, +1.0f, 0.0f, 1.0f},
    }};
    [encoder setVertexBytes:&uniforms
                     length:sizeof(uniforms)
                    atIndex:AAPLBufferIndexUniforms ];
    
    auto draw_text = [=](wry::rect<float> x, wry::string_view v, wry::RGBA8Unorm_sRGB color) {
        
        auto valign = (_font->height + _font->ascender + _font->descender) / 2; // note descender is negative
        
        auto xy = x.a;
        xy.y += valign;
        while (v) {
            auto c = *v; ++v;
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
        simd_float2 z;
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
        simd_float2 z;
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


// - (void)renderToMetalLayer:(nonnull CAMetalLayer*)metalLayer
- (void)renderToMetalLayer:(nonnull CAMetalDisplayLinkUpdate*)update
{
    // NSLog(@"%s\n", __PRETTY_FUNCTION__);

    using namespace ::simd;
    using namespace ::wry;

    //[_captureScope beginScope];
    
    
    _model->_world.step();
    
    // printf("%lx\n", _model->_world._waiting_on_time.begin()->second->_location);
    
    
    
    id<MTLCommandBuffer> command_buffer = [_commandQueue commandBuffer];
    

    {
        MeshInstanced i;
        i.model_transform = simd_matrix_rotate(-M_PI_2, simd_make_float3(-1.0f, 0.0f, 0.0f));
        i.model_transform.columns[3].x += _model->_looking_at.x / 1024.0f;
        i.model_transform.columns[3].z -= _model->_looking_at.y / 1024.0f;
        i.inverse_transpose_model_transform = simd_inverse(simd_transpose(i.model_transform));
        i.albedo = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);
        // _instanced_things = [_device newBufferWithBytes:&i length:sizeof(i) options:MTLStorageModeShared];
        memcpy([_instanced_things contents], &i, sizeof(i));
    }
    
    MeshUniforms uniforms = {};
    
    id<MTLBuffer> vertices = nil;
    id<MTLBuffer> indices = nil;
    NSUInteger index_count = 0;
    // raid model for data
    {
        auto tnow = _model->_world._tick;
        const auto& machines = _model->_world._waiting_on_time;
        
        NSUInteger quad_count = 10 * 10 + machines.size() * 10;
        NSUInteger vertex_count = quad_count * 4;
        index_count = quad_count * 6;
        vertices = [_device newBufferWithLength:vertex_count * sizeof(MeshVertex) options:MTLStorageModeShared];
        indices = [_device newBufferWithLength:index_count * sizeof(uint) options:MTLStorageModeShared];
        
        MeshVertex* pv = (MeshVertex*) vertices.contents;
        uint* pi = (uint*) indices.contents;
        MeshVertex v;
        v.tangent = simd_make_float4(-1.0f, 0.0f, 0.0f, 0.0f);
        v.bitangent = simd_make_float4(0.0f, 1.0f, 0.0f, 0.0f);
        v.normal = simd_make_float4(0.0f, 0.0f, -1.0f, 0.0f);
        uint k = 0;
        for (auto [t, p] : machines) {
            simd_int2 xy = 0;
            memcpy(&xy, &p->_location, 8);
            simd_float4 location = simd_make_float4(xy.x, xy.y, 0.0f, 01.0f);
            simd_float4 heading = {};
            auto h = p->_heading & 3;
            switch(h & 3) {
                case 0:
                    heading = simd_make_float4(0.0f, 1.0f, 0.0f, 0.0f);
                    break;
                case 1:
                    heading = simd_make_float4(1.0f, 0.0f, 0.0f, 0.0f);
                    break;
                case 2:
                    heading = simd_make_float4(0.0f, -1.0f, 0.0f, 0.0f);
                    break;
                case 3:
                    heading = simd_make_float4(-1.0f, 0.0f, 0.0f, 0.0f);
                    break;
            }
            location = location - heading * (t - tnow) / 64.0f;
            
            v.position = simd_make_float4(-0.5f, -0.5f, 0.0f, 0.0f) + location;
            v.coordinate = simd_make_float4(11.0f / 32.0f, 3.0f / 32.0f, 0.0f, 1.0f);
            *pv++ = v;
            v.position = simd_make_float4(+0.5f, -0.5f, 0.0f, 0.0f) + location;
            v.coordinate = simd_make_float4(12.0f / 32.0f, 3.0f / 32.0f, 0.0f, 1.0f);
            *pv++ = v;
            v.position = simd_make_float4(+0.5f, +0.5f, 0.0f, 0.0f) + location;
            v.coordinate = simd_make_float4(12.0f / 32.0f, 2.0f / 32.0f, 0.0f, 1.0f);
            *pv++ = v;
            v.position = simd_make_float4(-0.5f, +0.5f, 0.0f, 0.0f) + location;
            v.coordinate = simd_make_float4(11.0f / 32.0f, 2.0f / 32.0f, 0.0f, 1.0f);
            *pv++ = v;
            
            while (h--) {
                wry::rotate_args_left(pv[-4].coordinate,
                                      pv[-3].coordinate,
                                      pv[-2].coordinate,
                                      pv[-1].coordinate
                                      );
                
            }
            
            *pi++ = k;
            *pi++ = k;
            *pi++ = k + 1;
            *pi++ = k + 3;
            *pi++ = k + 2;
            *pi++ = k + 2;
            
            k += 4;
            
            for (int i = 0; i != p->_stack.size(); ++i) {
                location.z -= 0.5;
                Value value = p->_stack[i];
                simd_float4 coordinate = simd_make_float4((value.data & 15) / 32.0f, 13.0f / 32.0f, 0.0f, 1.0f);
                v.position = simd_make_float4(-0.5f, -0.5f, 0.0f, 0.0f) + location;
                v.coordinate = simd_make_float4(0.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                *pv++ = v;
                v.position = simd_make_float4(+0.5f, -0.5f, 0.0f, 0.0f) + location;
                v.coordinate = simd_make_float4(1.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                *pv++ = v;
                v.position = simd_make_float4(+0.5f, +0.5f, 0.0f, 0.0f) + location;
                v.coordinate = simd_make_float4(1.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                *pv++ = v;
                v.position = simd_make_float4(-0.5f, +0.5f, 0.0f, 0.0f) + location;
                v.coordinate = simd_make_float4(0.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
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
        
        for (int i = -5; i != 5; ++i) {
            for (int j = -5; j != 5; ++j) {

                simd_float4 location = simd_make_float4(i, j, 0.5f, 1.0f);
                simd_float4 coordinate = simd_make_float4(0.0f / 32.0f, 2.0f / 32.0f, 0.0f, 1.0f);
                
                {
                    i64 value = _model->_world.get({i, j}).data;
                    if (value) {
                        auto p = _opcode_to_coordinate.find(value);
                        if (p != _opcode_to_coordinate.end()) {
                            coordinate = p->second;
                        }
                    }
                }
                
                
                v.position = simd_make_float4(-0.5f, -0.5f, 0.0f, 0.0f) + location;
                v.coordinate = simd_make_float4(0.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                *pv++ = v;
                v.position = simd_make_float4(+0.5f, -0.5f, 0.0f, 0.0f) + location;
                v.coordinate = simd_make_float4(1.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                *pv++ = v;
                v.position = simd_make_float4(+0.5f, +0.5f, 0.0f, 0.0f) + location;
                v.coordinate = simd_make_float4(1.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                *pv++ = v;
                v.position = simd_make_float4(-0.5f, +0.5f, 0.0f, 0.0f) + location;
                v.coordinate = simd_make_float4(0.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
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
            
            v.coordinate = simd_make_float4(3.0 / 32.0f, 4.5f / 32.0f, 0.0f, 1.0f);
            v.position = simd_make_float4(-5.0f, -5.0f, 0.52, 1.0f);
            *pv++ = v;
            v.position = simd_make_float4(+5.0f, -5.0f, 0.52, 1.0f);
            *pv++ = v;
            v.position = simd_make_float4(+5.0f, +5.0f, 0.52, 1.0f);
            *pv++ = v;
            v.position = simd_make_float4(-5.0f, +5.0f, 0.52, 1.0f);
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
            simd_float4 location = _model->_mouse4.xzyw;
            //location.x = round(location.x);
            //location.y = round(location.y);
            simd_float4 coordinate = simd_make_float4(3.0f / 32.0f, 0.0f / 32.0f, 0.0f, 1.0f);
            
            v.position = simd_make_float4(-0.5f, -0.5f, 0.0f, 0.0f) + location;
            v.coordinate = simd_make_float4(0.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
            *pv++ = v;
            v.position = simd_make_float4(+0.5f, -0.5f, 0.0f, 0.0f) + location;
            v.coordinate = simd_make_float4(1.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
            *pv++ = v;
            v.position = simd_make_float4(+0.5f, +0.5f, 0.0f, 0.0f) + location;
            v.coordinate = simd_make_float4(1.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
            *pv++ = v;
            v.position = simd_make_float4(-0.5f, +0.5f, 0.0f, 0.0f) + location;
            v.coordinate = simd_make_float4(0.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
            *pv++ = v;
            
            *pi++ = k;
            *pi++ = k;
            *pi++ = k + 1;
            *pi++ = k + 3;
            *pi++ = k + 2;
            *pi++ = k + 2;
            
            k += 4;
            
            simd_int2 xy;
            xy.x = round(_model->_mouse4.x);
            xy.y = round(_model->_mouse4.z);
            ulong z = 0;
            memcpy(&z, &xy, 8);
            //ulong value = _model->_world.get(z);
            //++value;
            //_model->_world.set(z, value);
            
        }
        
        
    }

   
       
    // Render shadow map
    
    {
        MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor new];
        descriptor.depthAttachment.loadAction = MTLLoadActionClear;
        descriptor.depthAttachment.storeAction = MTLStoreActionStore;
        descriptor.depthAttachment.clearDepth = 1.0;
        descriptor.depthAttachment.texture = _shadowMapTarget;
        id<MTLRenderCommandEncoder> render_command_encoder = [command_buffer renderCommandEncoderWithDescriptor:descriptor];
        
        // shadow map parameters
        
        float light_radius = 8.0;
        simd_float3 light_direction = simd_normalize(simd_make_float3(2, 3, 1));
        
        simd_quatf q = simd_quaternion(light_direction, simd_make_float3(0, 0, -1));
        float4x4 A = simd_matrix4x4(q);
        float4x4 B = simd_matrix_scale(simd_make_float3(1.0f, 1.0f, 0.5f) / light_radius);
        float4x4 C = simd_matrix_translate(simd_make_float3(0.0f, 0.0f, 0.5f));
        
        uniforms.viewprojection_transform = C * B * A;
        uniforms.light_direction = -light_direction;
        uniforms.radiance = 2.0f; //sqrt(simd_saturate(cos(phaseOfDay)));
        
        [render_command_encoder setRenderPipelineState:_shadowMapRenderPipelineState];
        //[render_command_encoder setCullMode:MTLCullModeFront];
        [render_command_encoder setDepthStencilState:_enabledDepthStencilState];
        

        [render_command_encoder setVertexBytes:&uniforms
                                        length:sizeof(MeshUniforms)
                                       atIndex:AAPLBufferIndexUniforms];
        
        [render_command_encoder setFragmentBytes:&uniforms
                                          length:sizeof(MeshUniforms)
                                         atIndex:AAPLBufferIndexUniforms];
        [render_command_encoder setVertexBuffer:vertices offset:0 atIndex:AAPLBufferIndexVertices];
        [render_command_encoder setVertexBuffer:_instanced_things offset:0 atIndex:AAPLBufferIndexInstanced];
        
        
        [render_command_encoder setDepthBias:0.0f // +100.0f
                                  slopeScale:+1.0f // ?
                                       clamp:0.0f];
        
        
        [render_command_encoder setFragmentBytes:&uniforms
                           length:sizeof(MeshUniforms)
                          atIndex:AAPLBufferIndexUniforms];
        [render_command_encoder setFragmentTexture:_symbols atIndex:AAPLTextureIndexAlbedo];
        
        [render_command_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip
                                           indexCount:index_count
                                            indexType:MTLIndexTypeUInt32
                                          indexBuffer:indices
                                    indexBufferOffset:0];
        
        [render_command_encoder endEncoding];
        
    }
    
    {
        // Deferred render pass
        
        id <MTLRenderCommandEncoder> encoder = nil;
        
        {
            
            MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor new];
            
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
            
            encoder = [command_buffer renderCommandEncoderWithDescriptor:descriptor];
            
        }
        
        {

            {
                // save shadow map transform
                float4x4 A = matrix_ndc_to_tc_float4x4 *
                uniforms.viewprojection_transform;
                uniforms.light_viewprojection_transform = A;
            }

            
            {
                // rotate eye location to -Z
                float4 origin = simd_make_float4(0.0f, 8.0, -4.0f, 1.0f);
                quatf q = simd_quaternion(simd_normalize(origin.xyz),
                                          simd_make_float3(0.0, 0.0, -1.0));
                float4x4 A = simd_matrix4x4(q);
                float4x4 B = simd_matrix_translate(simd_make_float3(0.0f, 0.0f, simd_length(origin.xyz)));
                float aspect_ratio =  _viewport_size.x / _viewport_size.y;
                float4x4 C = simd_matrix_scale(simd_make_float3(2.0f, 2.0f * aspect_ratio, 1.0f));
                float4x4 V = C * B * A;
                float4x4 iV = inverse(V);
                float4x4 P = matrix_perspective_float4x4;
                float4x4 VP = P * V;
                float4x4 iVP = inverse(VP);
                                                
                uniforms.origin = origin;
                uniforms.view_transform = V;
                uniforms.inverse_view_transform = iV;
                uniforms.viewprojection_transform = VP;
                uniforms.inverse_viewprojection_transform = iVP;

  
                //     float4 position = float4(in.direction * depth, 0) + uniforms.origin;

                {
                    simd_float4 direction = uniforms.inverse_view_transform
                        * simd_make_float4(_model->_mouse.x, _model->_mouse.y, 1.0f, 0.0f);
                    // we want to intersect with y == 0 plane
                    //NSLog(@"%f %f %f\n", direction.x, direction.y, direction.z);
                    float depth = - uniforms.origin.y / direction.y;
                    simd_float3 position = direction.xyz * depth + uniforms.origin.xyz;
                    _model->_mouse4 = simd_make_float4(position, 1.0f);
                    _model->_mouse4.x -= _model->_looking_at.x / 1024.0f;
                    _model->_mouse4.z += _model->_looking_at.y / 1024.0f;

                    NSLog(@"%f %f %f\n", _model->_mouse4.x, _model->_mouse4.y, _model->_mouse4.z);

                    
                    
                }

                
                
            }
            
            {
                uniforms.ibl_scale = 1.0f;
                uniforms.ibl_transform = matrix_identity_float3x3;

            }
            
            
            
            
            // camera world location is...
            
            {
                [encoder setRenderPipelineState:_deferredGBufferRenderPipelineState];
                
                [encoder setDepthStencilState:_enabledDepthStencilState];
                [encoder setCullMode:MTLCullModeBack];

                bool show_jacobian, show_points, show_wireframe;
                {
                    auto guard = std::unique_lock(_model->_mutex);
                    show_jacobian = _model->_show_jacobian;
                    show_points = _model->_show_points;
                    show_wireframe = _model->_show_wireframe;

                }

                if (show_wireframe) {
                    [encoder setTriangleFillMode:MTLTriangleFillModeLines];
                }
                
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

                // Direction light (with shadow map)
                
                [encoder setRenderPipelineState:_deferredLightDirectionalShadowcastingRenderPipelineState];
                [encoder setFragmentTexture:_shadowMapTarget atIndex:AAPLTextureIndexShadow];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                            vertexStart:0
                            vertexCount:4];
            }
        }
        
        
        {
            auto guard = std::unique_lock(_model->_mutex);
            [self drawOverlay:encoder];
            [command_buffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
                dispatch_semaphore_signal(self->_atlas->_semaphore);
            }];
        }

        [encoder endEncoding];
            
    }
    
    // now blur
    
    {
        _gaussianBlur.edgeMode = MPSImageEdgeModeClamp;
        
        [_gaussianBlur encodeToCommandBuffer:command_buffer
                               sourceTexture:_deferredLightColorAttachmentTexture
                          destinationTexture:_blurredTexture];

        _imageAdd.primaryScale = 15.0f/16.0f;
        _imageAdd.secondaryScale = 1.0f/16.0f;

        [_imageAdd encodeToCommandBuffer:command_buffer
        primaryTexture:_deferredLightColorAttachmentTexture
                        secondaryTexture:_blurredTexture
                      destinationTexture:_addedTexture];
        
        
    }
    
   
    
    @autoreleasepool {
        id<CAMetalDrawable> currentDrawable = [update drawable];
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
    
    _viewport_size.x = drawableSize.width;
    _viewport_size.y = drawableSize.height;
        
    // The G-buffers must be recreated at the new size

    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];

    // Common attributes
    
    descriptor.textureType = MTLTextureType2D;
    descriptor.width = _viewport_size.x;
    descriptor.height = _viewport_size.y;
    
    // colorAttachments
    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;

    descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
    descriptor.storageMode = MTLStorageModePrivate;
    _deferredLightColorAttachmentTexture = [_device newTextureWithDescriptor:descriptor];
    _deferredLightColorAttachmentTexture.label = @"Light G-buffer";

    descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
    descriptor.storageMode = MTLStorageModeMemoryless;
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

- (void)metalDisplayLink:(CAMetalDisplayLink *)link needsUpdate:(CAMetalDisplayLinkUpdate *)update {
    //NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [self renderToMetalLayer:update];
}

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
 // going to be an array<string>, of which you can edit the last string
 
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
 auto o = simd_make_float4(512+zz.x, 512+zz.y, 0, 0);
 // simd_make_float2(512+zz.x, 512+zz.y)
 wry::vertex v[4];
 float h = -128.0f / _viewportSize.y;
 v[0].v.position = simd_make_float4(-32.0f, -32.0f, 0.0f, 1.0f);
 v[1].v.position = simd_make_float4(-32.0f, +32.0f, 0.0f, 1.0f);
 v[2].v.position = simd_make_float4(-32.0f, -32.0f, h, 1.0f);
 v[3].v.position = simd_make_float4(-32.0f, +32.0f, h, 1.0f);
 v[0].color = simd_make_uchar4(255, 255, 255, 255);
 v[1].color = simd_make_uchar4(255, 255, 255, 255);
 v[2].color = simd_make_uchar4(255, 255, 255, 255);
 v[3].color = simd_make_uchar4(255, 255, 255, 255);
 
 {
 simd_float2 a = _cube_sprites[1].a.texCoord;
 simd_float2 b = _cube_sprites[1].b.texCoord;
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
 v[0].v.position = simd_make_float4(-32.0f, -32.0f, h, 1.0f);
 v[1].v.position = simd_make_float4(-32.0f, +32.0f, h, 1.0f);
 v[2].v.position = simd_make_float4(+32.0f, -32.0f, h, 1.0f);
 v[3].v.position = simd_make_float4(+32.0f, +32.0f, h, 1.0f);
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
    a = simd_make_float4(0.25, 0.125, 0.0625, 0);
    foo(3);
    // bright sky
    a = simd_make_float4(0.25, 0.5, 1, 0);
    foo(2);
    // ground
    a.sub(n/2, 0, n/2, n) = simd_make_float4(0.5, 0.25, 0.125, 0);
    // feature
    a.sub(3*n/8, n/2, n/4, n/4) = simd_make_float4(1, 1, 1, 0) * 0.125;
    // lamp
    a.sub(n/8, n/8, n/2, n/16) = simd_make_float4(1, 1, 1, 1) * 10;
    foo(0);
    a.sub(n/8, n/8, n/2, n/16) = simd_make_float4(0, 1, 1, 0) * 10;
    foo(1);
    a.sub(n/8, n/8, n/2, n/16) = simd_make_float4(1, 0, 1, 0) * 10;
    foo(4);
    a.sub(n/8, n/8, n/2, n/16) = simd_make_float4(1, 1, 0, 0) * 10;
    foo(5);
    
}

std::vector<wry::sprite> _sprites;

auto img = wry::from_png_and_multiply_alpha(wry::path_for_resource("assets", "png"));
// wry::draw_bounding_box(img);
for (int y = 0; y != 256; y += 64) {
    for (int x = 0; x != 2048; x += 64) {
        auto v = img.sub(y, x, 64, 64);
        wry::draw_bounding_box(v);
        wry::sprite s = _atlas->place(v, simd_float2{32, 32});
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
            wry::array<simd_float4> mask(240, simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f));
            for (int i = 0; i != 8; ++i) {
                for (int j = 0; j != 8; ++j) {
                    mask[i * 16 + (240 - 166) + j] = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);
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
            float4x4 B = simd_matrix(simd_make_float4(  b.y ,  0.0f, -b.x,  0.0f  ),
                                     simd_make_float4(  0.0f,  1.0f,0,0),
                                     simd_make_float4(b.x,0,b.y,0),
                                     simd_make_float4(0,0,0,1));
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
