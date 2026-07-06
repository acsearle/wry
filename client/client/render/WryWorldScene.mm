//
//  WryWorldScene.mm
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
#include "WryRenderContext.h"
#include "WryWorldScene.h"

#include "SpriteAtlas.hpp"
#include "csv.hpp"
#include "debug.hpp"
#include "font.hpp"
#include "json.hpp"
#include "mesh.hpp"
#include "palette.hpp"
#include "platform.hpp"
#include "sdf.hpp"
#include "term.hpp"
#include "text.hpp"
#include "Wavefront.hpp"
#include "world.hpp"

#include "save.hpp"
#include "server.hpp"

@implementation WryWorldScene
{
    
    // link to rest of program

    // The world's state (MVC model): world pipeline, session view/input state,
    // overlays, and the per-frame logic (update / input).  This scene is a thin
    // Metal renderer over it -- _model->_world_to_render is the world the
    // renderer draws, advanced by _model->update().
    std::shared_ptr<wry::WorldState> _model;

    
    
    // Shared device + 2D-services context: device, command queue, shader
    // library, depth-stencil states, full-screen quad, sprite atlas, font,
    // overlay pipeline, and the pipeline / texture factory helpers.  See
    // WryRenderContext.h.  The renderer builds its world-specific resources
    // against this and reads its services every frame.
    WryRenderContext* _ctx;

    // view-only state
        
    size_t _frame_count;
            
    // (device / commandQueue / library / depth-stencil states / screen quad
    //  now live on _ctx)
    
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
    id<MTLTexture> _deferredAlbedoColorAttachmentTexture;
    id<MTLTexture> _deferredNormalColorAttachmentTexture;
    id<MTLTexture> _deferredMaterialColorAttachmentTexture;
    id<MTLTexture> _deferredDepthColorAttachmentTexture;
    id<MTLTexture> _deferredDepthAttachmentTexture;
    
    id<MTLTexture> _deferredLightImageBasedTexture;
    id<MTLTexture> _deferredLightImageBasedFresnelTexture;
        
    // conventional compositing for overlay
    //   (_ctx.overlayRenderPipelineState / _ctx.atlas / _ctx.font / _ctx.font2 now live on _ctx)

    // Bezier font
    
    id<MTLRenderPipelineState> _bezierRenderPipelineState;
    //std::vector<otf::GlyphData> _otf_glyph_data;
    //std::vector<otf::QuadraticBezier> _otf_quadratic_bezier;
                    
    
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
    // Default visualization for every MATTER code until per-code
    // appearances (meshes / tints / decals) exist.
    WryMesh* _container_mesh;
    
    // The opcode palette's data and selection now live on
    // _model->_palette_overlay; the renderer reads it for painting only.

    NSCursor* _cursor;
    
}


-(void) dealloc {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

// The Metal device, command queue, shader library, depth-stencil states,
// full-screen quad, sprite atlas / font, overlay pipeline, and the pipeline
// and texture factory helpers (newFunctionWithName:, newRenderPipelineState...,
// newTextureFromResource:) now live on WryRenderContext.  The renderer reaches
// all of them through _ctx.

-(id<MTLTexture>)prefilteredEnvironmentMapFromResource:(NSString*)name ofType:ext {
    
    using namespace simd;
    
    id<MTLTexture> input = [_ctx newTextureFromResource:name ofType:ext];
    
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
        output = [_ctx.device newTextureWithDescriptor:descriptor];
        output.label = name;
    }
    
    id<MTLRenderPipelineState> pipeline = nil;
    {
        MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.label = @"CubeFilter3";
        descriptor.vertexFunction = [_ctx newFunctionWithName:@"CubeFilterVertex"];
        descriptor.fragmentFunction = [_ctx newFunctionWithName:@"CubeFilterAccumulate3"];
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
        descriptor.colorAttachments[0].blendingEnabled = YES;
        descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
        descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
        descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
        descriptor.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
        pipeline = [_ctx newRenderPipelineStateWithDescriptor:descriptor];
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
    
    id<MTLCommandBuffer> commandBuffer = [_ctx.commandQueue commandBuffer];
    
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
            target = [_ctx.device newTextureWithDescriptor:descriptor];
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
            [encoder setVertexBuffer:_ctx.screenTriangleStripVertexBuffer
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
    _deferredLightImageBasedFresnelTexture = [_ctx.device newTextureWithDescriptor:texture_descriptor];
    _deferredLightImageBasedFresnelTexture.label = @"Fresnel integral look-up-table";
    
    id<MTLRenderPipelineState> render_pipeline_state = nil;
    MTLRenderPipelineDescriptor* render_pipeline_descriptor = [MTLRenderPipelineDescriptor new];
    render_pipeline_descriptor.vertexFunction = [_ctx newFunctionWithName:@"split_sum_vertex_function"];
    render_pipeline_descriptor.fragmentFunction = [_ctx newFunctionWithName:@"split_sum_fragment_function"];
    render_pipeline_descriptor.colorAttachments[0].pixelFormat = _deferredLightImageBasedFresnelTexture.pixelFormat;
    render_pipeline_descriptor.label = @"SplitSum";
    render_pipeline_state = [_ctx newRenderPipelineStateWithDescriptor:render_pipeline_descriptor];
        
    MTLRenderPassDescriptor* render_pass_descriptor = [MTLRenderPassDescriptor new];
    render_pass_descriptor.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    render_pass_descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    render_pass_descriptor.colorAttachments[0].texture = _deferredLightImageBasedFresnelTexture;
    
    id<MTLCommandBuffer> command_buffer = [_ctx.commandQueue commandBuffer];
    
    id<MTLRenderCommandEncoder> command_encoder = [command_buffer renderCommandEncoderWithDescriptor:render_pass_descriptor];
    [command_encoder setRenderPipelineState:render_pipeline_state];
    [command_encoder setVertexBuffer:_ctx.screenTriangleStripVertexBuffer offset:0 atIndex:AAPLBufferIndexVertices];
    [command_encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [command_encoder endEncoding];
    
    [command_buffer commit];
    
}

-(nonnull instancetype)initWithContext:(nonnull WryRenderContext*)context
                                 model:(std::shared_ptr<wry::WorldState>)model_
{

    using namespace ::wry;
    using namespace ::simd;
        
    if ((self = [super init])) {
        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
                
        auto newBufferWithArray = [&](auto& a) {
            void* bytes = a.data();
            size_t length = a.size() * sizeof(typename std::decay_t<decltype(a)>::value_type);
            id<MTLBuffer> buffer = [_ctx.device newBufferWithBytes:bytes length:length options:MTLStorageModeShared];
            assert(buffer);
            return buffer;
        };
        
        _model = model_;
        // The host owns the shared device + 2D-services context; we borrow it
        // and build our world-specific resources against it below.
        _ctx = context;
                        
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
            _shadowMapTarget = [_ctx.device newTextureWithDescriptor:descriptor];
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
            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::shadow_vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::shadow_fragment_function"];
            descriptor.vertexBuffers[AAPLBufferIndexVertices].mutability = MTLMutabilityImmutable;
            descriptor.vertexBuffers[AAPLBufferIndexUniforms].mutability = MTLMutabilityImmutable;
            descriptor.depthAttachmentPixelFormat = _shadowMapTarget.pixelFormat;
            _shadowMapRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];
        }
        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);

        // Prefilter the environment map to approximate spectral lobes
        _deferredLightImageBasedTexture = [self prefilteredEnvironmentMapFromResource:@"day" ofType:@"png"];

        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
        
        
        {
            MTLRenderPipelineDescriptor *descriptor = [[MTLRenderPipelineDescriptor alloc] init];
            
            // Render meshes to GBuffer

            descriptor.colorAttachments[AAPLColorIndexColor].pixelFormat = _ctx.drawablePixelFormat;
            descriptor.colorAttachments[AAPLColorIndexAlbedo].pixelFormat = MTLPixelFormatRGBA16Float;
            descriptor.colorAttachments[AAPLColorIndexNormal].pixelFormat = MTLPixelFormatRGBA16Float;
            descriptor.colorAttachments[AAPLColorIndexMaterial].pixelFormat = MTLPixelFormatRGBA8Unorm;
            descriptor.colorAttachments[AAPLColorIndexDepth].pixelFormat = MTLPixelFormatR32Float;
            descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::fragment_function"];
            descriptor.label = @"Deferred G-buffer";
            _deferredGBufferRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];
            
            descriptor.vertexFunction = [_ctx newFunctionWithName:@"whiskerVertexShader"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"whiskerFragmentShader"];
            descriptor.label = @"Deferred lines (DEBUG)";
            _deferredLinesRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];

            descriptor.vertexFunction = [_ctx newFunctionWithName:@"pointsVertexShader"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"pointsFragmentShader"];
            descriptor.label = @"Deferred points (DEBUG)";
            _deferredPointsRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];

            // Full-screen deferred lighting passes
            descriptor.colorAttachments[AAPLColorIndexColor].blendingEnabled = YES;
            descriptor.colorAttachments[AAPLColorIndexColor].rgbBlendOperation = MTLBlendOperationAdd;
            descriptor.colorAttachments[AAPLColorIndexColor].alphaBlendOperation = MTLBlendOperationAdd;
            descriptor.colorAttachments[AAPLColorIndexColor].sourceRGBBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[AAPLColorIndexColor].sourceAlphaBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[AAPLColorIndexColor].destinationRGBBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[AAPLColorIndexColor].destinationAlphaBlendFactor = MTLBlendFactorOne;
        
            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::lighting_vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::image_based_lighting_fragment_function"];
            descriptor.label = @"Deferred image-based light";
            _deferredLightImageBasedRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];

            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::lighting_vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::directional_lighting_fragment_function"];
            descriptor.label = @"Deferred shadowcasting directional light";
            _deferredLightDirectionalShadowcastingRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];

            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::lighting_vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::point_lighting_fragment_function"];
            descriptor.label = @"Deferred point light";
            _deferredLightPointRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];

        }
        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);

                
        // The overlay compositing pipeline and the sprite atlas / font now
        // live on _ctx (built in WryRenderContext's init).
        
        {
            // Create a pipeline state descriptor to create a compiled pipeline state object
            MTLMeshRenderPipelineDescriptor *renderPipelineDescriptor = [[MTLMeshRenderPipelineDescriptor alloc] init];
            
            renderPipelineDescriptor.label = @"BezierRenderPipeline";

            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].pixelFormat = _ctx.drawablePixelFormat;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].blendingEnabled = YES;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].rgbBlendOperation = MTLBlendOperationAdd;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].alphaBlendOperation = MTLBlendOperationAdd;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].sourceRGBBlendFactor = MTLBlendFactorOne;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].sourceAlphaBlendFactor = MTLBlendFactorOne;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            
            renderPipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

            renderPipelineDescriptor.fragmentBuffers[0].mutability = MTLMutabilityImmutable;
            renderPipelineDescriptor.fragmentFunction  =  [_ctx newFunctionWithName:@"otf::bezierFragmentFunction"];


            // ?
            // renderPipelineDescriptor.maxTotalThreadgroupsPerMeshGrid = AAPLMaxTotalThreadsPerMeshThreadgroup;
            // renderPipelineDescriptor.maxTotalThreadsPerMeshThreadgroup
            // renderPipelineDescriptor.maxTotalThreadsPerObjectThreadgroup
            
            renderPipelineDescriptor.meshBuffers[0].mutability = MTLMutabilityImmutable;
            renderPipelineDescriptor.meshFunction =  [_ctx newFunctionWithName:@"otf::bezierMeshFunction"];
            // renderPipelineDescriptor.meshThreadgroupSizeIsMultipleOfThreadExecutionWidth = YES;
            
            // TODO: Profiling is not available on mesh shaders.  We should also
            // provide an alternative implementation of the mesh shader using a
            // conventional vector shader and more CPU processing.

            // renderPipelineDescriptor.objectBuffers[0].mutability = MTLMutabilityImmutable;
            // renderPipelineDescriptor.objectFunction =  [_ctx newFunctionWithName:@"stroked::bezierObjectFunction"];
            // renderPipelineDescriptor.objectThreadgroupSizeIsMultipleOfThreadExecutionWidth = YES;
            
            // renderPipelineDescriptor.payloadMemoryLength = sizeof(BezierPayload);
            // renderPipelineDescriptor.rasterSampleCount;
            
            renderPipelineDescriptor.shaderValidation = MTLShaderValidationEnabled;
            

            _bezierRenderPipelineState = [_ctx newRenderPipelineStateWithMeshDescriptor:renderPipelineDescriptor];

            NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
            
            
        }

        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
        
        _gaussianBlur = [[MPSImageGaussianBlur alloc] initWithDevice:_ctx.device sigma:32.0];
        _imageAdd = [[MPSImageAdd alloc] initWithDevice:_ctx.device];
        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);

        {
            _symbols = [_ctx newTextureFromResource:@"assets" ofType:@"png"];
            _hackmesh = [[WryMesh alloc] initWithDevice:_ctx.device];
            _black = [_ctx newTextureFromResource:@"black" ofType:@"png"];
            _white = [_ctx newTextureFromResource:@"white" ofType:@"png"];
            _blue = [_ctx newTextureFromResource:@"blue"
                                          ofType:@"png"
                                 withPixelFormat:MTLPixelFormatRGBA8Unorm];
            _darkgray = [_ctx newTextureFromResource:@"gray_sRGB" ofType:@"png"];
            //_darkgray = [_ctx newTextureFromResource:@"darkgray" ofType:@"png"];
            _orange = [_ctx newTextureFromResource:@"orange" ofType:@"png"];

            
            MeshInstanced i;
            i.model_transform = simd_matrix_rotate(-M_PI_2, simd_make_float3(-1.0f, 0.0f, 0.0f));
            i.inverse_transpose_model_transform = simd_inverse(simd_transpose(i.model_transform));
            i.albedo = make<float4>(1.0f, 1.0f, 1.0f, 1.0f);
            _instanced_things = [_ctx.device newBufferWithBytes:&i length:sizeof(i) options:MTLStorageModeShared];
            
        }
        
        {
            auto f = [&](WryMesh *__strong& p, std::filesystem::path v) {
                auto m = wry::from_obj(v);
                m.MeshVertexify();
                
                p = [[WryMesh alloc] initWithDevice:_ctx.device];
                p.vertexBuffer = newBufferWithArray(m.hack_MeshVertex);
                p.indexBuffer = newBufferWithArray(m.hack_triangle_strip);
                
                p.emissiveTexture = _black;
                p.albedoTexture = _white; // [_ctx newTextureFromResource:@"PaintedMetal009_1K-PNG_Color" ofType:@"png"];
                p.metallicTexture = _white; // [_ctx newTextureFromResource:@"PaintedMetal009_1K-PNG_Metalness" ofType:@"png"];
                p.normalTexture = _blue; // [_ctx newTextureFromResource:@"PaintedMetal009_1K-PNG_NormalGL" ofType:@"png" withPixelFormat:MTLPixelFormatRGBA8Unorm];
                p.roughnessTexture = _darkgray; // [_ctx newTextureFromResource:@"PaintedMetal009_1K-PNG_Roughness" ofType:@"png"];;
                p.occlusionTexture = _white;
                p.instanceCount = 0;
            };

            f(_furnace_mesh, "furnace.obj");
            f(_mine_mesh, "mine.obj");
            f(_truck_mesh, "truck2.obj");

            // Baked-detail workflow: the render mesh is the 12-triangle box
            // proxy; the corrugation and door geometry live in the normal and
            // occlusion maps baked from the high-poly source.  Normal and
            // occlusion are non-color data, so they load linear (not sRGB).
            // The test-pattern albedo makes UV orientation and scale problems
            // visible; swap for a real paint texture when one exists.
            f(_container_mesh, "Container20_Proxy.obj");
            _container_mesh.albedoTexture = [_ctx newMipmappedTextureFromResource:@"testpattern"
                                                                           ofType:@"png"
                                                                  withPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB];
            _container_mesh.normalTexture = [_ctx newMipmappedTextureFromResource:@"Container20_Normal"
                                                                           ofType:@"png"
                                                                  withPixelFormat:MTLPixelFormatRGBA8Unorm];
            _container_mesh.occlusionTexture = [_ctx newMipmappedTextureFromResource:@"Container20_AO"
                                                                              ofType:@"png"
                                                                     withPixelFormat:MTLPixelFormatRGBA8Unorm];
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
                // auto h = _name_to_opcode._inner._hasher.get_hash(v + 7);
                // printf("hash \"%s\" -> %llu\n", v+7, h);
                _name_to_opcode.emplace(v + 7, k);
            }
            
//            for (auto&& [k, v] : wry::OPCODE_NAMES) {
//                // printf("\"%s\" <-> %lld\n", v, k);
//                const String& s = _opcode_to_name[k];
//                // printf("    %lld -> %.*s\n", k, (int) s.chars.size(), (char const*) s.chars.data());
//                int64_t i = _name_to_opcode[v + 7];
//                // printf("    %s -> %lld\n", v + 7, i);
//            }

                
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
                                if (charcode == U'_') {
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

            {
                // Initialize the palette's grid and transform on the
                // PaletteOverlay; the overlay owns this data from here on.
                auto& controls = _model->_palette_overlay.controls();
                size_type nn = 24;
                controls._payload = wry::matrix<wry::Term>(nn, 2);

                i64 j = 0;
                for (i64 i = 0; i != _name_to_opcode.size(); ++i) {
                    if (_opcode_to_coordinate.contains(i)) {
                        controls._payload[j % nn, j / nn] = term_make_opcode((int)i);
                        ++j;
                    } else {
                        auto s = _opcode_to_name[i].chars.as_view();
                        if (s.size())
                            printf("Sprite not found for %.*s\n",
                                   (int) s.size(), (char*) s.data());
                    }
                }

                controls._transform
                =
                simd_matrix_scale(1.0f / 16.0f)
                * simd_matrix_translate(simd_make_float3(nn*-0.5f, -2.0f, 0.0f));
            }
            
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
                _ctx.atlas->place(nn);
                
            }
            
        }
        
    }
        
    NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);

    return self;
}

- (void) drawBezier:(id<MTLRenderCommandEncoder>)encoder {

    using namespace simd;
    using namespace wry;
    
    [encoder setRenderPipelineState:_bezierRenderPipelineState];
        
    // These can be constants
    std::vector<otf::CubicBezier> curves = _ctx.font2.cubic_bezier;
    //curves.push_back({{0.0f, 0.0f},{0.5f, 0.0f},{0.5f, 0.5f},});
    //curves.push_back({{0.6f, 0.5f},{0.5f, 0.0f},{0.0f, -0.1f},});
    id<MTLBuffer> buf_curves = [_ctx.device newBufferWithBytes:curves.data()
                                             length:curves.size()*sizeof(otf::CubicBezier)
                                            options:MTLStorageModeShared];
    
    std::vector<otf::GlyphData> gi = _ctx.font2.glyph_data;
    // gi.push_back({{-0.2, -0.2}, {0.7, 0.6}, 0, 2});
    id<MTLBuffer> buf_gi = [_ctx.device newBufferWithBytes:gi.data()
                                             length:gi.size()*sizeof(otf::GlyphData)
                                            options:MTLStorageModeShared];

    // This is per draw
    std::vector<otf::PlacedGlyph> characters;
    //static unsigned int oof = 0;
    // characters.push_back({{0.0, 0.0}, 33});
    //++oof; if (oof >= gi.size()) oof = 0;
    auto str = "Sphinx of black quartz, judge my rasterizer.";
    simd_float2 pos = {-4.0, -1.0};
    // pos += _model->_looking_at / 1024.0f * float2{1.0f, -1.0f};
    for (auto a = str; *a; ++a) {
        // auto q = _ctx.font->charmap.find(*a);
        auto q = _ctx.font2.charmap.find(*a);
        characters.push_back({pos, q->second.glyph_index});
        //pos.x += q->second.advance * 0.01;
        pos.x += q->second.advance;
    }
    
    id<MTLBuffer> buf_ch = [_ctx.device newBufferWithBytes:characters.data()
                                               length:characters.size()*sizeof(otf::PlacedGlyph)
                                              options:MTLStorageModeShared];
    
    otf::BezierUniforms un;
    
    float aspect_ratio = _model->_gui.viewport_size.x / _model->_gui.viewport_size.y;
    float scale_size = 48.0 * (4.0/3.0) * 2.0 * 2.0 / _model->_gui.viewport_size.x;
    un.transformation = matrix_float4x4{{
        { 1.0f * scale_size, 0.5f * scale_size * aspect_ratio, 0.0f, 0.1f },
        {-0.5f * scale_size, 1.0f * scale_size * aspect_ratio, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.5f, 1.0f },
    }};
    
    // un.transformation = _model->_uniforms.viewprojection_transform;
    
    
    un.inverse_transformation = inverse(un.transformation);
    un.pixel_size = 1.0 / _model->_gui.viewport_size;

    [encoder setMeshBuffer:buf_gi offset:0 atIndex:0];
    [encoder setMeshBuffer:buf_ch offset:0 atIndex:1];
    
    [encoder setFragmentBuffer:buf_curves offset:0 atIndex:0];
    [encoder setMeshBytes:&un length:sizeof(un) atIndex:3];
    [encoder setFragmentBytes:&un length:sizeof(un) atIndex:3];

    [encoder drawMeshThreadgroups:MTLSizeMake(characters.size(),1,1)
      threadsPerObjectThreadgroup:MTLSizeMake(1,1,1)
        threadsPerMeshThreadgroup:MTLSizeMake(1,1,1)];
    
    /*

    id<MTLBuffer> vb = [_ctx.device newBufferWithLength:v.size_in_bytes() options:MTLStorageModeShared];
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
    
    uniforms.position_transform = matrix_float4x4{{
        {2.0f / _model->_gui.viewport_size.x, 0.0f, 0.0f},
        {0.0f, -2.0f / _model->_gui.viewport_size.y, 0.0f, 0.0f},
        { 0.0f, 0.0f, 1.0f, 0.0f },
        {-1.0f, +1.0f, 0.0f, 1.0f},
    }};
    [encoder setVertexBytes:&uniforms
                     length:sizeof(uniforms)
                    atIndex:AAPLBufferIndexUniforms ];
    
    assert(_buffers[0].length >= _vertices.size() * sizeof(SpriteVertex));
    std::memcpy(_buffers[0].contents,
                _vertices.data(),
                _vertices.size() * sizeof(SpriteVertex));
    [renderEncoder setVertexBuffer:_buffers[0]
                            offset:0
                           atIndex:AAPLBufferIndexVertices];
    [renderEncoder setFragmentTexture:_texture
                              atIndex:AAPLTextureIndexColor];
    [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                      vertexStart:0
                      vertexCount:_vertices.size()];
    _vertices.clear();
    std::rotate(_buffers, _buffers + 1, _buffers + 4);
     */
    
}

- (void) drawOverlay:(id<MTLRenderCommandEncoder>)encoder {

    using namespace simd;
    using namespace wry;

    MyUniforms uniforms;

    // If the palette overlay selected a new opcode this frame, swap the
    // platform cursor to its icon.  The overlay flagged this from its
    // on_event during pump; we consume the flag here.
    if (_model->_palette_overlay.cursor_needs_refresh()) {
        auto coordinate = _opcode_to_coordinate[term_as_opcode(_model->_holding_value)];
        matrix<RGBA8Unorm_sRGB> tile(64, 64);
        MTLRegion region = MTLRegionMake2D(coordinate.x * _symbols.width,
                                           coordinate.y * _symbols.height,
                                           64, 64);
        [_symbols getBytes:tile.data()
               bytesPerRow:tile.stride_bytes()
                fromRegion:region
               mipmapLevel:0];
        unsigned char* p[1];
        p[0] = (unsigned char*) tile.data();
        NSBitmapImageRep* a = [[NSBitmapImageRep alloc]
                              initWithBitmapDataPlanes:p
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
        NSCursor* c = [[NSCursor alloc] initWithImage:b
                                              hotSpot:NSMakePoint(0.0f, 0.0f)];
        [c set];
        _cursor = c;
        _model->_palette_overlay.clear_cursor_refresh();
    }

    [encoder setRenderPipelineState:_ctx.overlayRenderPipelineState];

    // ----- Palette paint pass (projective transform; reads PaletteOverlay
    // state for hover/selected highlights).
    {
        auto& palette = _model->_palette_overlay;
        auto& m = palette.controls()._payload;

        uniforms.position_transform = simd_mul(matrix_float4x4{{
            {1.0f, 0.0f, 0.0f},
            {0.0f, -_model->_gui.viewport_size.x / _model->_gui.viewport_size.y, 0.0f, 0.0f},
            { 0.0f, 0.0f, 1.0f, 0.0f },
            {0.0f, -1.0f, 0.0f, 1.0f},
        }}, palette.controls()._transform);

        wry::ContiguousDeque<wry::SpriteVertex> v;

        for (difference_type j = 0; j != m.major(); ++j) {
            for (difference_type i = 0; i != m.minor(); ++i) {
                Term a = m[i, j];
                if (a.is_opcode()) {

                    SpriteVertex c;

                    simd_float4 position = make<float4>(i, j, 0, 1);
                    float2 texCoord;

                    c.color = RGBA8Unorm_sRGB(0.0f, 0.0f, 0.0f, 0.875f);
                    texCoord = simd_make_float2(9, 9) / 32.0f;

                    if ((palette.hover_i() == (int)i) && (palette.hover_j() == (int)j)) {
                        c.color.g.write(0.5f);
                    }

                    if ((palette.selected_i() == (int)i) && (palette.selected_j() == (int)j)) {
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

                    texCoord = _opcode_to_coordinate[term_as_opcode(a)].xy;
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

        id<MTLBuffer> vb = [_ctx.device newBufferWithLength:v.size_in_bytes()
                                                options:MTLStorageModeShared];
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

    // (World input -> commands moved out of the render path into
    // -submitLocalCommands, driven from -update.)

    // ----- Screen-space overlays.  Set the screen-space transform on the
    // encoder; the sprite atlas accumulates and commits below.
    uniforms.position_transform = matrix_float4x4{{
        {2.0f / _model->_gui.viewport_size.x, 0.0f, 0.0f},
        {0.0f, -2.0f / _model->_gui.viewport_size.y, 0.0f, 0.0f},
        { 0.0f, 0.0f, 1.0f, 0.0f },
        {-1.0f, +1.0f, 0.0f, 1.0f},
    }};
    [encoder setVertexBytes:&uniforms
                     length:sizeof(uniforms)
                    atIndex:AAPLBufferIndexUniforms ];

    // Paint the world's scene overlays -- palette (no-op paint for now) plus the
    // in-game menu / save list when pushed -- then the app-tier overlays
    // (floating log, console) on top.  The console is a global drop-down, so it
    // sits above everything.
    {
        wry::gui::Painter painter;
        painter.atlas = _ctx.atlas;
        painter.font = _ctx.font;
        painter.viewport_size_px = _model->_gui.viewport_size;
        painter.frame_count = (uint64_t)_frame_count;
        painter.white_sprite = _ctx.atlas->_white;
        // Default clip: the full viewport.  Widgets push tighter clips
        // around their own contents via Painter::push_clip / pop_clip.
        painter.clip = wry::rect<float>{
            0.0f, 0.0f,
            _model->_gui.viewport_size.x, _model->_gui.viewport_size.y,
        };
        _model->_stack.paint(painter);
        _model->_gui.overlays.paint(painter);
    }

    _ctx.atlas->commit((__bridge void*)encoder);
}

-(void)resetCursor {
    [_cursor set];
}

// WryScene: the world scene runs until the app quits; no transitions yet.
- (id<WryScene>)nextScene {
    return nil;
}


// Per-frame logic lives on WorldState now (this scene is a thin Metal renderer
// over it); -update and -handleEventsWithViewSize: just forward.
-(void)update:(double)dtSeconds {
    _model->update(dtSeconds);
}

- (void)handleEventsWithViewSize:(CGSize)viewSizePoints {
    _model->handle_events(simd_make_float2((float)viewSizePoints.width,
                                           (float)viewSizePoints.height));
}

// Encode this frame's passes into the host-supplied command buffer and return
// the texture the host should present (it blits this into the late-acquired
// drawable).  The host owns the command buffer's lifecycle -- it creates it,
// presents, and commits -- so every scene can share one drawable / present
// path.  Returning the texture (rather than taking a target up front) lets the
// host acquire nextDrawable as late as possible, preserving the old behavior.
- (id<MTLTexture>)encodeIntoCommandBuffer:(id<MTLCommandBuffer>)command_buffer
{

    using namespace ::simd;
    using namespace ::wry;

    // -update advanced the displayed world (and serviced the GC) immediately
    // before this -encode, on the same thread and frame; we only read it here.
    Root<World*>& new_world = _model->_world_to_render;
    assert(new_world);
    
    // Construct camera transforms
    MeshUniforms uniforms = _model->_uniforms;
    
    // Construct ground plane transforms
    auto lookat_transform =  matrix_identity_float4x4;
    lookat_transform.columns[3].x += _model->_looking_at.x / 1024.0f;
    lookat_transform.columns[3].y -= _model->_looking_at.y / 1024.0f;

    // The container proxy is authored at real scale (6.058 x 2.438 x 2.591
    // meters, Z-up, base at z = 0, long axis along x); this scale seats it
    // within one grid cell.
    constexpr float container_mesh_scale = 0.15f;

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
        
        // (_model->_mouse4 -- the cursor's ground-plane projection -- is now
        // computed in -submitLocalCommands during -update, not here.)
        
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
    _container_mesh.instanceCount = 0;
    // raid model for data

    {

        auto tnow = world_get_time(new_world._ptr);
        auto&& entities = new_world->_entity_for_entity_id;
        
        // kludge out the contents of the entity mapping
        std::vector<Entity const*> ptrs;
        entities.kv.for_each([&ptrs](auto&& k, auto&& v) {
            ptrs.push_back(v);
        });

        // Accumulate the textured quads into growable buffers and upload them
        // once below.  The buffers grow to fit whatever the loops emit, so no
        // number of entities can overflow them the way the old fixed-size
        // allocation did.
        std::vector<MeshVertex> vbuf;
        std::vector<uint> ibuf;
        // Reserve a rough lower bound (one quad per grid cell and per entity,
        // plus the ground plane and cursor) to avoid most reallocations; this
        // is only a hint -- the vectors still grow past it as needed.
        size_t quad_hint = 2
            + (size_t)(grid_bounds.b.x - grid_bounds.a.x) * (size_t)(grid_bounds.b.y - grid_bounds.a.y)
            + ptrs.size();
        vbuf.reserve(quad_hint * 4);
        ibuf.reserve(quad_hint * 6);

        MeshVertex v;
        v.tangent = make<float4>(1.0f, 0.0f, 0.0f, 0.0f);
        v.bitangent = make<float4>(0.0f, 1.0f, 0.0f, 0.0f);
        v.normal = make<float4>(0.0f, 0.0f, 1.0f, 0.0f);
        uint k = 0;

#if 1
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
                    [_truck_mesh addInstance:m];
                }
                
                // now make the stack
                location.z += 0.8;
                for (int i = (int) wry::PersistentStack<wry::Term>::size(p->_stack); i--;) {
                    location.z += 0.5;
                    wry::Term value = wry::PersistentStack<wry::Term>::at(p->_stack, i);
                    if (value.is_matter()) {
                        // held matter rides as a mesh at this stack slot,
                        // heading-aligned with the machine
                        auto A = simd_matrix_translate(location) * lookat_transform;
                        A = A * simd_matrix_rotate(atan2(dx1.x, dx1.y), make<float3>(0.0, 0.0, -1.0));
                        A = A * simd_matrix_scale(container_mesh_scale);
                        MeshInstanced m;
                        m.model_transform = A;
                        m.inverse_transpose_model_transform = inverse(transpose(A));
                        m.albedo = make<float4>(1.0, 1.0, 1.0, 1.0);
                        [_container_mesh addInstance:m];
                        continue;
                    }
                    simd_float4 coordinate;
                    if (value.is_opcode()) {
                        coordinate = _opcode_to_coordinate[value.as_opcode()];
                    } else {
                        // number, as hex
                        coordinate = make<float4>((value.as_int64_t() & 15) / 32.0f, 13.0f / 32.0f, 0.0f, 1.0f);
                    }
                    v.position = make<float4>(-0.5f, -0.5f, 0.0f, 0.0f) + location;
                    v.coordinate = make<float4>(0.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                    vbuf.push_back(v);
                    v.position = make<float4>(+0.5f, -0.5f, 0.0f, 0.0f) + location;
                    v.coordinate = make<float4>(1.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                    vbuf.push_back(v);
                    v.position = make<float4>(+0.5f, +0.5f, 0.0f, 0.0f) + location;
                    v.coordinate = make<float4>(1.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                    vbuf.push_back(v);
                    v.position = make<float4>(-0.5f, +0.5f, 0.0f, 0.0f) + location;
                    v.coordinate = make<float4>(0.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                    vbuf.push_back(v);
                    ibuf.push_back(k);
                    ibuf.push_back(k);
                    ibuf.push_back(k + 1);
                    ibuf.push_back(k + 3);
                    ibuf.push_back(k + 2);
                    ibuf.push_back(k + 2);
                    k += 4;
                }
                
            } else if (auto p = dynamic_cast<const LocalizedEntity*>(q)){
                
                simd_float4 location = make<float4>(p->_location.x, p->_location.y + 1.0, 0.0, 1.0f);
                auto A = simd_matrix_translate(location) * lookat_transform * simd_matrix_scale(0.5f);

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
                    [s addInstance:m];
                }

                
                if (auto r = dynamic_cast<const Source*>(q)) {
                    
                    simd_float4 coordinate = make<float4>((r->_of_this.as_int64_t() & 15) / 32.0f, 13.0f / 32.0f, 0.0f, 1.0f);
                    
                    v.position = make<float4>(-0.5f, -0.1f, -0.5f, 0.0f) + location;
                    v.coordinate = make<float4>(0.0f / 32.0f, 1.0f / 32.0f, 0.0f, 1.0f) + coordinate;
                    vbuf.push_back(v);
                    v.position = make<float4>(+0.5f, -0.1f, -0.5f, 0.0f) + location;
                    v.coordinate = make<float4>(1.0f / 32.0f, 1.0f / 32.0f, 0.0f, 1.0f) + coordinate;
                    vbuf.push_back(v);
                    v.position = make<float4>(+0.5f, -0.1f, +0.5f, 0.0f) + location;
                    v.coordinate = make<float4>(1.0f / 32.0f, 0.0f / 32.0f, 0.0f, 1.0f) + coordinate;
                    vbuf.push_back(v);
                    v.position = make<float4>(-0.5f, -0.1f, +0.5f, 0.0f) + location;
                    v.coordinate = make<float4>(0.0f / 32.0f, 0.0f / 32.0f, 0.0f, 1.0f) + coordinate;
                    vbuf.push_back(v);
                    
                    ibuf.push_back(k);
                    ibuf.push_back(k);
                    ibuf.push_back(k + 1);
                    ibuf.push_back(k + 3);
                    ibuf.push_back(k + 2);
                    ibuf.push_back(k + 2);
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
                    //wry::Term q = new_world->_term_for_coordinate.read(wry::Coordinate{i, j});
                    wry::Term q = {};
                    (void) new_world->_term_for_coordinate.try_get(wry::Coordinate{i, j}, q);
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
                    } else if (q.is_matter()) {
                        // matter renders as a mesh sitting on an ordinary
                        // empty tile
                        coordinate = make<float4>(0.0 / 32.0f, 1.0f / 32.0f, 0.0f, 1.0f);
                        auto A = simd_matrix_translate(location) * lookat_transform;
                        A = A * simd_matrix_scale(container_mesh_scale);
                        MeshInstanced m;
                        m.model_transform = A;
                        m.inverse_transpose_model_transform = inverse(transpose(A));
                        m.albedo = make<float4>(1.0, 1.0, 1.0, 1.0);
                        [_container_mesh addInstance:m];
                    } else {
                        //printf("q is mystery\n");
                        coordinate = make<float4>(0.0 / 32.0f, 1.0f / 32.0f, 0.0f, 1.0f);
                    }
                }
                
                v.position = make<float4>(-0.5f, -0.5f, 0.0f, 0.0f) + location;
                v.coordinate = make<float4>(0.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                vbuf.push_back(v);
                v.position = make<float4>(+0.5f, -0.5f, 0.0f, 0.0f) + location;
                v.coordinate = make<float4>(1.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                vbuf.push_back(v);
                v.position = make<float4>(+0.5f, +0.5f, 0.0f, 0.0f) + location;
                v.coordinate = make<float4>(1.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                vbuf.push_back(v);
                v.position = make<float4>(-0.5f, +0.5f, 0.0f, 0.0f) + location;
                v.coordinate = make<float4>(0.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
                vbuf.push_back(v);
                
                ibuf.push_back(k);
                ibuf.push_back(k);
                ibuf.push_back(k + 1);
                ibuf.push_back(k + 3);
                ibuf.push_back(k + 2);
                ibuf.push_back(k + 2);
                
                k += 4;
            }
        }

        {
            
            // big ground plane
            
            v.coordinate = make<float4>(3.0 / 32.0f, 4.5f / 32.0f, 0.0f, 1.0f);
            v.position = make<float4>(grid_bounds.a.x - 1, grid_bounds.a.y - 1, 0.0, 1.0f);
            vbuf.push_back(v);
            v.position = make<float4>(grid_bounds.b.x, grid_bounds.a.y - 1, 0.0, 1.0f);
            vbuf.push_back(v);
            v.position = make<float4>(grid_bounds.b.x, grid_bounds.b.y, 0.0, 1.0f);
            vbuf.push_back(v);
            v.position = make<float4>(grid_bounds.a.x - 1, grid_bounds.b.y, 0.0, 1.0f);
            vbuf.push_back(v);
            
            ibuf.push_back(k);
            ibuf.push_back(k);
            ibuf.push_back(k + 1);
            ibuf.push_back(k + 3);
            ibuf.push_back(k + 2);
            ibuf.push_back(k + 2);
            
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
            vbuf.push_back(v);
            v.position = make<float4>(+0.5f, -0.5f, 0.0f, 0.0f) + location;
            v.coordinate = make<float4>(1.0f / 32.0f, 1.0f / 32.0f, 0.0f, 0.0f) + coordinate;
            vbuf.push_back(v);
            v.position = make<float4>(+0.5f, +0.5f, 0.0f, 0.0f) + location;
            v.coordinate = make<float4>(1.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
            vbuf.push_back(v);
            v.position = make<float4>(-0.5f, +0.5f, 0.0f, 0.0f) + location;
            v.coordinate = make<float4>(0.0f / 32.0f, 0.0f / 32.0f, 0.0f, 0.0f) + coordinate;
            vbuf.push_back(v);
            
            ibuf.push_back(k);
            ibuf.push_back(k);
            ibuf.push_back(k + 1);
            ibuf.push_back(k + 3);
            ibuf.push_back(k + 2);
            ibuf.push_back(k + 2);
            
            k += 4;
            
            int2 xy;
            xy.x = round(_model->_mouse4.x);
            xy.y = round(_model->_mouse4.z);
            ulong z = 0;
            memcpy(&z, &xy, 8);
            //ulong value = new_world->get(z);
            //++value;
            //new_world->set(z, value);

        }

        // Upload exactly what we accumulated.
        index_count = ibuf.size();
        vertices = [_ctx.device newBufferWithLength:vbuf.size() * sizeof(MeshVertex) options:MTLStorageModeShared];
        indices = [_ctx.device newBufferWithLength:ibuf.size() * sizeof(uint) options:MTLStorageModeShared];
        memcpy(vertices.contents, vbuf.data(), vbuf.size() * sizeof(MeshVertex));
        memcpy(indices.contents, ibuf.data(), ibuf.size() * sizeof(uint));

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
        [render_command_encoder setDepthStencilState:_ctx.enabledDepthStencilState];

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
        [_container_mesh drawWithRenderCommandEncoder:render_command_encoder commandBuffer:command_buffer];
        
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
            
            descriptor.colorAttachments[AAPLColorIndexAlbedo].clearColor = MTLClearColorMake(0, 0, 0, 0);
            descriptor.colorAttachments[AAPLColorIndexAlbedo].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexAlbedo].storeAction = MTLStoreActionDontCare;
            descriptor.colorAttachments[AAPLColorIndexAlbedo].texture = _deferredAlbedoColorAttachmentTexture;

            descriptor.colorAttachments[AAPLColorIndexNormal].clearColor = MTLClearColorMake(0, 0, 0, 0);
            descriptor.colorAttachments[AAPLColorIndexNormal].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexNormal].storeAction = MTLStoreActionDontCare;
            descriptor.colorAttachments[AAPLColorIndexNormal].texture = _deferredNormalColorAttachmentTexture;

            // clear to (occlusion 1, roughness 1, metallic 0), matching the
            // background surface properties the old layout cleared to
            descriptor.colorAttachments[AAPLColorIndexMaterial].clearColor = MTLClearColorMake(1, 1, 0, 0);
            descriptor.colorAttachments[AAPLColorIndexMaterial].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexMaterial].storeAction = MTLStoreActionDontCare;
            descriptor.colorAttachments[AAPLColorIndexMaterial].texture = _deferredMaterialColorAttachmentTexture;

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
                
                [encoder setDepthStencilState:_ctx.enabledDepthStencilState];
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
                [_container_mesh drawWithRenderCommandEncoder:encoder commandBuffer:command_buffer];

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
                                
                [encoder setVertexBuffer:_ctx.screenTriangleStripVertexBuffer
                                  offset:0
                                 atIndex:AAPLBufferIndexVertices];
                [encoder setDepthStencilState:_ctx.disabledDepthStencilState];

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
            // [self drawBezier:encoder];
            //   The Sphinx pangram was useful while the OTF outline path
            //   wasn't yet feeding the atlas; now that build_font (font.mm)
            //   does the rasterisation through the same otf::Handle, the
            //   live mesh-shader pass is just visual noise covering up the
            //   GUI.  Method body is preserved as the reference for how to
            //   pack otf::GlyphData / otf::PlacedGlyph / CubicBezier into
            //   mesh and fragment buffers and dispatch a draw -- re-enable
            //   this line if you want to bring it back.
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
    
    ++_frame_count;

    //[_captureScope endScope];

    // Hand the host the texture to show.  It blits this into the drawable
    // (acquired as late as possible) and presents + commits the command buffer.
    // Bloom is rendered into _addedTexture but, as before, we present the raw
    // light texture; the _addedTexture path stays for when bloom is re-enabled.
    return _deferredLightColorAttachmentTexture;

}


-(void)drawableResize:(CGSize)drawableSize
{
    
    _model->_gui.viewport_size.x = drawableSize.width;
    _model->_gui.viewport_size.y = drawableSize.height;
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
    _deferredLightColorAttachmentTexture = [_ctx.device newTextureWithDescriptor:descriptor];
    _deferredLightColorAttachmentTexture.label = @"Light G-buffer";

    descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
    descriptor.storageMode = MTLStorageModeMemoryless; // <--
    _deferredAlbedoColorAttachmentTexture = [_ctx.device newTextureWithDescriptor:descriptor];
    _deferredAlbedoColorAttachmentTexture.label = @"Albedo G-buffer";

    descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
    _deferredNormalColorAttachmentTexture = [_ctx.device newTextureWithDescriptor:descriptor];
    _deferredNormalColorAttachmentTexture.label = @"Normal G-buffer";

    descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
    _deferredMaterialColorAttachmentTexture = [_ctx.device newTextureWithDescriptor:descriptor];
    _deferredMaterialColorAttachmentTexture.label = @"Material G-buffer";
    
    descriptor.pixelFormat = MTLPixelFormatR32Float;
    _deferredDepthColorAttachmentTexture = [_ctx.device newTextureWithDescriptor:descriptor];
    _deferredDepthColorAttachmentTexture.label = @"Depth G-buffer";
    
    // depthAttachment
    descriptor.usage = MTLTextureUsageRenderTarget;
    descriptor.pixelFormat = MTLPixelFormatDepth32Float;
    _deferredDepthAttachmentTexture = [_ctx.device newTextureWithDescriptor:descriptor];
    _deferredDepthAttachmentTexture.label = @"Depth buffer";

    descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
    _blurredTexture = [_ctx.device newTextureWithDescriptor:descriptor];
    _blurredTexture.label = @"Blur target";
    
    _addedTexture = [_ctx.device newTextureWithDescriptor:descriptor];
    _addedTexture.label = @"Addition target";
    
}

@end
