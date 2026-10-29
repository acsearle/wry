//
//  WryWorldScene.mm
//  client
//
//  Created by Antony Searle on 1/7/2023.
//

// Shared Metal header first for no contamination
#include "ShaderTypes.h"

#include <algorithm>
#include <chrono>
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
#include "gui_widget.hpp"
#include "json.hpp"
#include "mesh.hpp"
#include "palette.hpp"
#include "platform.hpp"
#include "sdf.hpp"
#include "symbol_sdf.hpp"
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
    id<MTLTexture> _deferredLightIrradianceTexture;
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

    // The symbol field beside the atlas (symbol_sdf.hpp), its per-cell
    // stroke widths, and the slab pipelines that march it in the G-buffer
    // and shadow passes
    id<MTLTexture> _symbolField;
    std::vector<float> _symbolStrokeWidth;
    id<MTLRenderPipelineState> _symbolGBufferRenderPipelineState;
    id<MTLRenderPipelineState> _symbolShadowRenderPipelineState;
    id<MTLRenderPipelineState> _symbolDecalRenderPipelineState;
    
    id<MTLTexture> _black;
    id<MTLTexture> _white;
    id<MTLTexture> _blue;
    id<MTLTexture> _darkgray;
    id<MTLTexture> _orange;

    // Terrain material atlases (assets/terrain/): 4 material rows indexed
    // by wry::Terrain x 4 variant columns of 256 px PBR tiles, with a
    // baked near-black border per cell that doubles as the bleed gutter.
    // ORM packs occlusion/roughness/metallic into r/g/b (the glTF channel
    // convention the shader reads), so the one texture serves all three
    // bindings.
    id<MTLTexture> _terrainAlbedo;
    id<MTLTexture> _terrainNormal;
    id<MTLTexture> _terrainORM;

    // Double-buffered world-map texture (one texel per tile).  The
    // background builder hands finished pixel buffers to the main thread,
    // which uploads into the back texture and flips; the front texture was
    // last written a whole build (~1 s) earlier, far outside any frame
    // still in flight on the GPU.
    id<MTLTexture> _mapTexture[2];
    int _mapFrontIndex;

    wry::Table<ulong, simd_float4> _opcode_to_coordinate;

    // Non-opcode glyph coordinates captured from assets.json (whose grid
    // is aligned 1:1 with the atlas art, SymbolSDF::kCells cells a side):
    // the digit row origin ('0', with '1'..'F' contiguous to its right),
    // the boolean glyphs, the empty-cell dot and the cursor's border.
    simd_float4 _digit_zero_coordinate;
    simd_float4 _true_coordinate;
    simd_float4 _false_coordinate;
    simd_float4 _cdot_coordinate;
    simd_float4 _border_coordinate;
    
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

// The sky's irradiance map (CubeFilterIrradiance): a small cube, one
// cosine-weighted hemisphere mean per texel, for the diffuse term of the
// image-based light.  The GGX-prefiltered map above serves specular only;
// its roughest level is a whole-sphere average, not a hemisphere's.
-(id<MTLTexture>)irradianceMapFromResource:(NSString*)name ofType:ext {

    using namespace simd;

    id<MTLTexture> input = [_ctx newTextureFromResource:name ofType:ext];

    id<MTLTexture> output = nil;
    {
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
        descriptor.textureType = MTLTextureTypeCube;
        descriptor.width = 32;
        descriptor.height = descriptor.width;
        descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
        descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        descriptor.resourceOptions = MTLResourceStorageModePrivate;
        output = [_ctx.device newTextureWithDescriptor:descriptor];
        output.label = [NSString stringWithFormat:@"%@ irradiance", name];
    }

    id<MTLRenderPipelineState> pipeline = nil;
    {
        MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.label = @"CubeFilterIrradiance";
        descriptor.vertexFunction = [_ctx newFunctionWithName:@"CubeFilterVertex"];
        descriptor.fragmentFunction = [_ctx newFunctionWithName:@"CubeFilterIrradiance"];
        descriptor.colorAttachments[0].pixelFormat = output.pixelFormat;
        descriptor.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
        pipeline = [_ctx newRenderPipelineStateWithDescriptor:descriptor];
    }

    // The same six face projections as the prefilter
    CubeFilterUniforms uniforms;
    {
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
        uniforms.alpha2 = 0.0f;
    }

    MTLRenderPassDescriptor* renderPass = [MTLRenderPassDescriptor new];
    renderPass.colorAttachments[0].texture = output;
    renderPass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    renderPass.colorAttachments[0].storeAction = MTLStoreActionStore;
    renderPass.renderTargetArrayLength = 6;

    id<MTLCommandBuffer> commandBuffer = [_ctx.commandQueue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPass];
    [encoder setRenderPipelineState:pipeline];
    [encoder setVertexBuffer:_ctx.screenTriangleStripVertexBuffer
                      offset:0
                     atIndex:AAPLBufferIndexVertices];
    [encoder setVertexBytes:&uniforms
                     length:sizeof(CubeFilterUniforms)
                    atIndex:AAPLBufferIndexUniforms];
    [encoder setFragmentTexture:input
                        atIndex:AAPLTextureIndexColor];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                vertexStart:0
                vertexCount:4
              instanceCount:6];
    [encoder endEncoding];
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

        // Prepare shadow map resources.  The shadow map texture itself is
        // allocated (and reallocated) by -drawableResize:, which the host
        // guarantees to call before our first frame; see
        // -resizeShadowMapForDrawableSize: for the sizing policy.
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
            descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            _shadowMapRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];

            descriptor.label = @"Symbol slab shadow pipeline";
            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::symbol_shadow_vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::symbol_shadow_fragment_function"];
            _symbolShadowRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];
        }
        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);

        // Prefilter the environment map to approximate spectral lobes
        _deferredLightImageBasedTexture = [self prefilteredEnvironmentMapFromResource:@"day" ofType:@"png"];
        _deferredLightIrradianceTexture = [self irradianceMapFromResource:@"day" ofType:@"png"];

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

            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::symbol_vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::symbol_fragment_function"];
            descriptor.label = @"Symbol slab G-buffer";
            _symbolGBufferRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];

            // The decal multiplies the material's occlusion channel in
            // place and touches nothing else
            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::symbol_decal_vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::symbol_decal_fragment_function"];
            descriptor.label = @"Symbol decal (ambient occlusion)";
            descriptor.colorAttachments[AAPLColorIndexColor].writeMask = MTLColorWriteMaskNone;
            descriptor.colorAttachments[AAPLColorIndexAlbedo].writeMask = MTLColorWriteMaskNone;
            descriptor.colorAttachments[AAPLColorIndexNormal].writeMask = MTLColorWriteMaskNone;
            descriptor.colorAttachments[AAPLColorIndexMaterial].writeMask = MTLColorWriteMaskRed;
            descriptor.colorAttachments[AAPLColorIndexDepth].writeMask = MTLColorWriteMaskNone;
            _symbolDecalRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];
            descriptor.colorAttachments[AAPLColorIndexColor].writeMask = MTLColorWriteMaskAll;
            descriptor.colorAttachments[AAPLColorIndexAlbedo].writeMask = MTLColorWriteMaskAll;
            descriptor.colorAttachments[AAPLColorIndexNormal].writeMask = MTLColorWriteMaskAll;
            descriptor.colorAttachments[AAPLColorIndexMaterial].writeMask = MTLColorWriteMaskAll;
            descriptor.colorAttachments[AAPLColorIndexDepth].writeMask = MTLColorWriteMaskAll;

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
            // Mip levels allocated now, generated below once the opcode
            // labels have been stamped into level 0.
            _symbols = [_ctx newTextureWithMipStorageFromResource:@"assets" ofType:@"png"];
            {
                // The field beside the atlas (symbol_sdf.hpp): from its
                // cache, or built from the atlas's silhouette at half the
                // atlas resolution and cached
                auto t0 = std::chrono::steady_clock::now();
                wry::SymbolSDF sdf = wry::symbol_sdf_for_atlas(wry::path_for_resource("assets", "png"));
                const int cell = sdf.cell;
                MTLTextureDescriptor* descriptor =
                    [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                       width:sdf.image.major()
                                                                      height:sdf.image.minor()
                                                                   mipmapped:NO];
                descriptor.storageMode = MTLStorageModeShared;
                descriptor.usage = MTLTextureUsageShaderRead;
                _symbolField = [_ctx.device newTextureWithDescriptor:descriptor];
                [_symbolField replaceRegion:MTLRegionMake2D(0, 0, sdf.image.major(), sdf.image.minor())
                                mipmapLevel:0
                                  withBytes:sdf.image.data()
                                bytesPerRow:sdf.image.stride_bytes()];
                _symbolField.label = @"Symbol field";
                _symbolStrokeWidth = std::move(sdf.stroke_width);
                auto t1 = std::chrono::steady_clock::now();
                std::vector<float> widths;
                for (float w : _symbolStrokeWidth)
                    if (w > 0.0f)
                        widths.push_back(w);
                std::sort(widths.begin(), widths.end());
                NSLog(@"symbol field: %d px cells, ready in %.0f ms; %zu symbols, stroke widths %.1f / %.1f / %.1f texels (min / median / max)",
                      cell, std::chrono::duration<double, std::milli>(t1 - t0).count(),
                      widths.size(), widths.empty() ? 0.0f : widths.front(),
                      widths.empty() ? 0.0f : widths[widths.size() / 2],
                      widths.empty() ? 0.0f : widths.back());
            }
            _hackmesh = [[WryMesh alloc] init];
            _black = [_ctx newTextureFromResource:@"black" ofType:@"png"];
            _white = [_ctx newTextureFromResource:@"white" ofType:@"png"];
            _blue = [_ctx newTextureFromResource:@"blue"
                                          ofType:@"png"
                                 withPixelFormat:MTLPixelFormatRGBA8Unorm];
            _darkgray = [_ctx newTextureFromResource:@"gray_sRGB" ofType:@"png"];
            //_darkgray = [_ctx newTextureFromResource:@"darkgray" ofType:@"png"];
            _orange = [_ctx newTextureFromResource:@"orange" ofType:@"png"];

        }

        {
            // Terrain material atlases (built by assets/terrain/
            // build_terrain_textures.py): immutable, so they take the
            // mipmapped path; the trilinear sampler puts the mips to use.
            // Albedo is color (sRGB); the normal and ORM maps are data,
            // loaded linear.
//            _terrainAlbedo = [_ctx newMipmappedTextureFromResource:@"terrain/Terrain_Albedo"
            _terrainAlbedo = [_ctx newMipmappedTextureFromResource:@"white"
                                                            ofType:@"png"
                                                   withPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB];
//            _terrainNormal = [_ctx newMipmappedTextureFromResource:@"terrain/Terrain_NormalGL"
            _terrainNormal = [_ctx newMipmappedTextureFromResource:@"blue"
                                                            ofType:@"png"
                                                   withPixelFormat:MTLPixelFormatRGBA8Unorm];
//            _terrainORM = [_ctx newMipmappedTextureFromResource:@"terrain/Terrain_ORM"
            _terrainORM = [_ctx newMipmappedTextureFromResource:@"orange"
                                                            ofType:@"png"
                                                   withPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB];
        }

        {
            // World-map double buffer, zero-filled so the map is fully
            // transparent until the first background build lands.
            MTLTextureDescriptor* descriptor =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB
                                                                   width:wry::WorldMap::EXTENT
                                                                  height:wry::WorldMap::EXTENT
                                                               mipmapped:NO];
            descriptor.usage = MTLTextureUsageShaderRead;
            std::vector<uint8_t> zero((size_t)wry::WorldMap::EXTENT * wry::WorldMap::EXTENT * 4, 0);
            for (int i = 0; i != 2; ++i) {
                _mapTexture[i] = [_ctx.device newTextureWithDescriptor:descriptor];
                _mapTexture[i].label = [NSString stringWithFormat:@"world map %d", i];
                [_mapTexture[i] replaceRegion:MTLRegionMake2D(0, 0, wry::WorldMap::EXTENT, wry::WorldMap::EXTENT)
                                  mipmapLevel:0
                                    withBytes:zero.data()
                                  bytesPerRow:(NSUInteger)wry::WorldMap::EXTENT * 4];
            }
            _mapFrontIndex = 0;
        }
        
        {
            auto f = [&](WryMesh *__strong& p, std::filesystem::path v) {
                auto m = wry::from_obj(v);
                m.MeshVertexify();
                
                p = [[WryMesh alloc] init];
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

                
            // Defaults matching the shipped atlas layout, overwritten by
            // the by-name capture below; a json missing the names
            // degrades to these rather than rendering garbage
            constexpr float kCells = (float)wry::SymbolSDF::kCells;
            _digit_zero_coordinate = make<float4>(0.0f / kCells, 10.0f / kCells, 0.0f, 1.0f);
            _true_coordinate       = make<float4>(0.0f / kCells, 11.0f / kCells, 0.0f, 1.0f);
            _false_coordinate      = make<float4>(1.0f / kCells, 11.0f / kCells, 0.0f, 1.0f);
            _cdot_coordinate       = make<float4>(0.0f / kCells,  2.0f / kCells, 0.0f, 1.0f);
            _border_coordinate     = make<float4>(0.0f / kCells,  3.0f / kCells, 0.0f, 1.0f);

            try {
                auto x = json::from_file<ContiguousDeque<ContiguousDeque<String>>>("assets.json");
                ulong i = 0;
                for (const ContiguousDeque<String>& y : x) {
                    ulong j = 0;
                    for (const String& z : y) {
                        // printf("loading image for %.*s\n", (int) z.chars.size(), (const char*) z.chars.data());
                        simd_float4 coordinate = make<float4>(j / kCells, i / kCells, 0.0f, 1.0f);
                        // auto h = _name_to_opcode._inner._hasher.get_hash(z);
                        // printf("hash \"%.*s\" -> %llu\n", (int) z.chars.size(), (const char*) z.chars.data(), h);
                        auto p = _name_to_opcode.find(z);
                        // printf("p choice\n");
                        if (p == _name_to_opcode.end()) {
                            // Not an opcode: capture the handful of named
                            // non-opcode glyphs the renderer draws directly
                            std::string_view sv((const char*) z.chars.data(),
                                                z.chars.size());
                            if (sv == "0")
                                _digit_zero_coordinate = coordinate;
                            else if (sv == "true")
                                _true_coordinate = coordinate;
                            else if (sv == "false")
                                _false_coordinate = coordinate;
                            else if (sv == "cdot")
                                _cdot_coordinate = coordinate;
                            else if (sv == "border2")
                                _border_coordinate = coordinate;
                            // Lowercase opcode-art names: wired into the
                            // opcode coordinate table like any glyph, but
                            // routed through this branch so the art is
                            // NOT overstamped with label text the way the
                            // uppercase opcode path stamps its cells
                            else if (sv == "valve_north_south")
                                _opcode_to_coordinate[OPCODE_VALVE_NORTH_SOUTH] = coordinate;
                            else if (sv == "valve_east_west")
                                _opcode_to_coordinate[OPCODE_VALVE_EAST_WEST] = coordinate;
                        }
                        else /* if (p != _name_to_opcode.end()) */ {
                            // printf("p is not equal to end\n");
                            _opcode_to_coordinate[p->second] = coordinate;
                            
                            
                            // One atlas cell.  The atlas is a kCells x
                            // kCells grid at whatever resolution it ships
                            // in (assets.png is 4096 px, so 256 px cells);
                            // the label glyphs are stamped at their
                            // native pixel size regardless.
                            const NSUInteger cell = _symbols.width / wry::SymbolSDF::kCells;
                            matrix<RGBA8Unorm_sRGB> tile(cell, cell);
                            MTLRegion region = MTLRegionMake2D(j*cell, i*cell, cell, cell);
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
                                if (cursor.x + advance.x >= (float) cell) {
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

            // Level 0 is final (label stamping above is its last edit), so
            // fill the mip chain.  Blocking; nothing has sampled it yet.
            [_ctx generateMipmapsForTexture:_symbols];

            {
                // Initialize the palette's grid and transform on the
                // PaletteOverlay; the overlay owns this data from here on.
                auto& controls = _model->_palette_overlay.controls();
                size_type nn = 24;
                controls._payload = wry::matrix<wry::Term>(nn, 2);

                i64 j = 0;
                for (i64 i = 0; i != _name_to_opcode.size(); ++i) {
                    // One glyph per reorientation orbit: the R / SHIFT-R /
                    // H / V keys reach the rest of each cycle.
                    if (!wry::opcode_is_orbit_representative((wry::OPCODE)i))
                        continue;
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
    // pos += _model->_focus_fraction * float2{-1.0f, -1.0f};
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
    if (_model->_palette_overlay.cursor_needs_refresh() && !_model->is_holding()) {
        // The hand was dropped (Escape): back to the platform arrow.
        _cursor = [NSCursor arrowCursor];
        [_cursor set];
        _model->_palette_overlay.clear_cursor_refresh();
    }
    if (_model->_palette_overlay.cursor_needs_refresh()) {
        // The hand can hold any non-matter term (pipette); the cursor
        // shows the glyph the ground would.
        wry::Term held = _model->_holding_value;
        simd_float4 coordinate;
        if (held.is_opcode())
            coordinate = _opcode_to_coordinate[held.as_opcode()];
        else if (wry::term_is_boolean(held))
            coordinate = wry::term_as_boolean(held) ? _true_coordinate : _false_coordinate;
        else if (held.is_inty())
            coordinate = _digit_zero_coordinate
                + make<float4>((held.as_int() & 15) / (float)wry::SymbolSDF::kCells, 0.0f, 0.0f, 0.0f);
        else
            coordinate = _cdot_coordinate;
        // The glyph's frame, the central half of its atlas cell (the rest
        // is the field's margin), cut out at its native resolution; the
        // NSImage below is still 32 points, so a higher-resolution atlas
        // just makes a sharper cursor.
        const NSUInteger cell = _symbols.width / wry::SymbolSDF::kCells / 2;
        matrix<RGBA8Unorm_sRGB> tile(cell, cell);
        MTLRegion region = MTLRegionMake2D(coordinate.x * _symbols.width + cell / 2,
                                           coordinate.y * _symbols.height + cell / 2,
                                           cell, cell);
        [_symbols getBytes:tile.data()
               bytesPerRow:tile.stride_bytes()
                fromRegion:region
               mipmapLevel:0];
        unsigned char* p[1];
        p[0] = (unsigned char*) tile.data();
        NSBitmapImageRep* a = [[NSBitmapImageRep alloc]
                              initWithBitmapDataPlanes:p
                                            pixelsWide:(NSInteger) cell
                                            pixelsHigh:(NSInteger) cell
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
    // The deferred pass left back-face culling on; GUI quads are authored
    // in screen space with no winding discipline, so turn it off.
    [encoder setCullMode:MTLCullModeNone];

    // ----- Palette paint pass (reads PaletteOverlay state for hover /
    // selected highlights).  Hidden in map mode.
    //
    // The palette's cells are laid out by its projective _transform, which
    // the hit test also uses; map the cell corners through it to drawable
    // pixels, then draw in screen space with the GUI quad pipeline: a Panel-
    // style plate docked to the bottom edge (it extends off-screen, so only
    // its top corners show and its shadow reads as that of a plate running
    // on below the edge), and on it a rounded square per opcode, one gap
    // apart, each under its glyph from _symbols.  One instanced draw with
    // _symbols bound; the untextured quads ignore it.
    if (!_model->_show_map) {
        auto& palette = _model->_palette_overlay;
        auto& m = palette.controls()._payload;
        const float vw = _model->_gui.viewport_size.x;
        const float vh = _model->_gui.viewport_size.y;

        // Same composition as PaletteOverlay::on_event's hit test.
        const simd_float4x4 palette_to_ndc = simd_mul(matrix_float4x4{{
            {1.0f, 0.0f, 0.0f},
            {0.0f, -vw / vh, 0.0f, 0.0f},
            { 0.0f, 0.0f, 1.0f, 0.0f },
            {0.0f, -1.0f, 0.0f, 1.0f},
        }}, palette.controls()._transform);
        auto to_px = [&](float i, float j) {
            const simd_float4 q = simd_mul(palette_to_ndc, simd_make_float4(i, j, 0.0f, 1.0f));
            const float2 ndc = q.xy / q.w;
            return simd_make_float2((ndc.x + 1.0f) * 0.5f * vw,
                                    (1.0f - ndc.y) * 0.5f * vh);
        };
        auto px_rect = [&](float i0, float j0, float i1, float j1) {
            const float2 a = to_px(i0, j0);
            const float2 b = to_px(i1, j1);
            return wry::rect<float>{
                std::min(a.x, b.x), std::min(a.y, b.y),
                std::max(a.x, b.x), std::max(a.y, b.y),
            };
        };

        using wry::gui::Button;
        using wry::gui::Painter;
        using wry::gui::Panel;
        const float gap = Panel::gap(_ctx.font);
        const float cell_radius = Button::default_corner_radius(_ctx.font);
        const float panel_radius = cell_radius + gap;
        const float half_gap = 0.5f * gap;
        auto radii = [](float r) { return simd_make_float4(r, r, r, r); };

        std::vector<WryQuadInstance> quads;
        auto push = [&](std::optional<WryQuadInstance> q) {
            if (q) quads.push_back(*q);
        };

        // The plate: the grid's bounds, outset so the outermost squares
        // sit one gap in from its edge, run on below the viewport by more
        // than its corner radius and shadow, so its bottom never shows.
        {
            wry::rect<float> plate = px_rect(0.0f, 0.0f,
                                             (float)m.minor(), (float)m.major());
            plate.a -= half_gap;
            plate.b += half_gap;
            plate.b.y = std::max(plate.b.y, vh + panel_radius
                                 + 3.0f * Painter::kShadowSigma
                                 + Painter::kShadowOffset.y);
            push(Painter::blurred_round_rect_quad(
                plate + Painter::kShadowOffset, radii(panel_radius),
                Painter::kShadowSigma, RGBA8Unorm_sRGB(0.0f, 0.0f, 0.0f, 1.0f)));
            const float k = Panel::kPanelGray;
            push(Painter::blurred_round_rect_quad(
                plate, radii(panel_radius), Painter::kEdgeSigma,
                RGBA8Unorm_sRGB(k, k, k, 1.0f)));
        }

        for (difference_type j = 0; j != m.major(); ++j) {
            for (difference_type i = 0; i != m.minor(); ++i) {
                Term a = m[i, j];
                if (!a.is_opcode())
                    continue;
                const wry::rect<float> cell = px_rect((float)i, (float)j,
                                                      (float)(i + 1), (float)(j + 1));

                const bool hover = (palette.hover_i() == (int)i)
                    && (palette.hover_j() == (int)j);
                const bool selected = (palette.selected_i() == (int)i)
                    && (palette.selected_j() == (int)j);
                wry::rect<float> square = cell;
                square.a += half_gap;
                square.b -= half_gap;
                push(Painter::blurred_round_rect_quad(
                    square, radii(cell_radius), Painter::kEdgeSigma,
                    Button::background_color(selected, hover, false)));

                // The glyph's frame (the central half of its atlas cell;
                // the rest is the field's margin) over the whole button,
                // minified through _symbols' mip chain.
                const float2 uv = _opcode_to_coordinate[term_as_opcode(a)].xy;
                const float cell_uv = 1.0f / (float)wry::SymbolSDF::kCells;
                WryQuadInstance g = wry::SpriteAtlas::make_quad(
                    simd_make_float4(cell.a.x, cell.a.y, cell.b.x, cell.b.y),
                    RGBA8Unorm_sRGB(1.0f, 1.0f, 1.0f, 1.0f));
                g.uv = simd_make_float4(uv + 0.25f * cell_uv, uv + 0.75f * cell_uv);
                g.flags = WryQuadFlagTextured | WryQuadFlagFiltered;
                quads.push_back(g);
            }
        }

        uniforms.position_transform = matrix_float4x4{{
            {2.0f / vw, 0.0f, 0.0f},
            {0.0f, -2.0f / vh, 0.0f, 0.0f},
            { 0.0f, 0.0f, 1.0f, 0.0f },
            {-1.0f, +1.0f, 0.0f, 1.0f},
        }};
        WryBufferSlice qb = [_ctx.arena copyBytes:quads.data()
                                           length:quads.size() * sizeof(WryQuadInstance)];

        [encoder setRenderPipelineState:_ctx.overlayQuadRenderPipelineState];
        [encoder setVertexBytes:&uniforms
                         length:sizeof(uniforms)
                        atIndex:AAPLBufferIndexUniforms ];
        [encoder setVertexBuffer:qb.buffer
                          offset:qb.offset
                         atIndex:AAPLBufferIndexVertices];
        [encoder setFragmentTexture:_symbols
                            atIndex:AAPLTextureIndexColor];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                    vertexStart:0
                    vertexCount:4
                  instanceCount:quads.size()];
        // The minimap below draws SpriteVertex quads.
        [encoder setRenderPipelineState:_ctx.overlayRenderPipelineState];
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

    // ----- Minimap (hidden while the full map is displayed): the world
    // map in the top-right corner, windowed to one map-extent of world
    // centered on the camera focus -- the same content the full map shows,
    // miniaturized and screen-aligned.  A translucent plate marks the
    // instrument's box; the map texels draw over it, clipped to the mapped
    // world rect (so panning past the edge shows plate, not smeared or
    // wrapped texels).  Drawn before the atlas commit, so all sprite-atlas
    // GUI (log, console, menus) stays on top.
    if (!_model->_show_map) {
        using wry::WorldMap;

        const float SIZE = 512.0f;    // drawable px; 2 px per map texel
        const float MARGIN = 24.0f;
        float2 vp = _model->_gui.viewport_size;
        const float bx0 = vp.x - MARGIN - SIZE;   // box, screen px, y-down
        const float by0 = MARGIN;

        // Window (tile units, render space), centered on the camera focus.
        const float cx = _model->_focus_fraction.x;
        const float cy = _model->_focus_fraction.y;
        const float W = (float)WorldMap::EXTENT;
        const float wx0 = cx - W * 0.5f;
        const float wy0 = cy - W * 0.5f;

        // Mapped world rect (tile (x, y) spans x +/- 0.5), render space.
        const float2 map_origin = _model->render_position(wry::Coordinate{WorldMap::X0, WorldMap::Y0});
        const float mx0 = map_origin.x - 0.5f;
        const float my0 = map_origin.y - 0.5f;
        const float mx1 = mx0 + W;
        const float my1 = my0 + W;

        // Window/world intersection, in world units.
        const float ix0 = std::max(wx0, mx0);
        const float ix1 = std::min(wx0 + W, mx1);
        const float iy0 = std::max(wy0, my0);
        const float iy1 = std::min(wy0 + W, my1);

        // World -> screen (north = world +y at the top of the box) and
        // world -> map uv.
        auto sx = [=](float x) { return bx0 + (x - wx0) * (SIZE / W); };
        auto sy = [=](float y) { return by0 + (wy0 + W - y) * (SIZE / W); };
        auto tu = [=](float x) { return (x - mx0) / W; };
        auto tv = [=](float y) { return (y - my0) / W; };

        auto emit_quad = [&](float px0, float py0, float px1, float py1,
                             float u0, float v0, float u1, float v1,
                             RGBA8Unorm_sRGB color, id<MTLTexture> texture) {
            SpriteVertex q[6];
            for (auto& c : q) c.color = color;
            q[0].v.position = make<float4>(px0, py0, 0.0f, 1.0f);
            q[0].v.texCoord = simd_make_float2(u0, v0);
            q[1].v.position = make<float4>(px1, py0, 0.0f, 1.0f);
            q[1].v.texCoord = simd_make_float2(u1, v0);
            q[2].v.position = make<float4>(px1, py1, 0.0f, 1.0f);
            q[2].v.texCoord = simd_make_float2(u1, v1);
            q[3] = q[0];
            q[4] = q[2];
            q[5].v.position = make<float4>(px0, py1, 0.0f, 1.0f);
            q[5].v.texCoord = simd_make_float2(u0, v1);
            [encoder setVertexBytes:q length:sizeof(q) atIndex:AAPLBufferIndexVertices];
            [encoder setFragmentTexture:texture atIndex:AAPLTextureIndexColor];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:6];
        };

        // Plate: the whole box, white texture tinted translucent black.
        emit_quad(bx0, by0, bx0 + SIZE, by0 + SIZE,
                  0.0f, 0.0f, 1.0f, 1.0f,
                  RGBA8Unorm_sRGB(0.0f, 0.0f, 0.0f, 0.5f), _white);

        // Map texels over the intersection (empty when panned far away).
        if ((ix0 < ix1) && (iy0 < iy1)) {
            emit_quad(sx(ix0), sy(iy1), sx(ix1), sy(iy0),
                      tu(ix0), tv(iy1), tu(ix1), tv(iy0),
                      RGBA8Unorm_sRGB(1.0f, 1.0f, 1.0f, 1.0f),
                      _mapTexture[_mapFrontIndex]);
        }
    }

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
        // Default clip: the full viewport.  Widgets push tighter clips
        // around their own contents via Painter::push_clip / pop_clip.
        painter.clip = wry::rect<float>{
            0.0f, 0.0f,
            _model->_gui.viewport_size.x, _model->_gui.viewport_size.y,
        };
        _model->_stack.paint(painter);
        _model->_gui.overlays.paint(painter);
    }

    _ctx.atlas->commit((__bridge void*)encoder,
                       (__bridge void*)_ctx.overlayQuadRenderPipelineState);
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

    // Adopt a freshly built world map, if the background builder finished
    // one: upload into the back texture of the double buffer and flip.
    if (wry::WorldMap* built = _model->_map_handoff->take_finished()) {
        int back = _mapFrontIndex ^ 1;
        [_mapTexture[back] replaceRegion:MTLRegionMake2D(0, 0, wry::WorldMap::EXTENT, wry::WorldMap::EXTENT)
                             mipmapLevel:0
                               withBytes:built->rgba.data()
                             bytesPerRow:(NSUInteger)wry::WorldMap::EXTENT * 4];
        _mapFrontIndex = back;
        delete built;
    }

    const bool show_map = _model->_show_map;

    // Camera transforms.  Everything below is positioned in RENDER SPACE:
    // world space with the camera's focus tile at the origin, so positions
    // stay small floats at any world coordinate; the focus tile is added
    // back only to look up world content (WorldState::world_tile /
    // render_position), and the sub-tile pan is in the view transform.
    MeshUniforms uniforms = _model->_uniforms;
    const wry::Coordinate focus_tile = _model->_focus_tile;
    // A world tile from its render-space offset (the coordinates wrap)
    auto tile_at = [focus_tile](int i, int j) {
        return wry::Coordinate{(wry::i32)((wry::u32)focus_tile.x + (wry::u32)i),
                               (wry::i32)((wry::u32)focus_tile.y + (wry::u32)j)};
    };

    // The container proxy is authored at real scale (6.058 x 2.438 x 2.591
    // meters, Z-up, base at z = 0, long axis along x); this scale seats it
    // within one grid cell.
    constexpr float container_mesh_scale = 0.15f;

    // Bound with setVertexBytes at each pass below.  A persistent shared
    // MTLBuffer here is read at GPU *execution* time, so the next frame's
    // CPU overwrite races any frame still in flight; setVertexBytes copies
    // into the command buffer at encode time.
    MeshInstanced mesh_instanced_things = {};
    mesh_instanced_things.model_transform = matrix_identity_float4x4;
    mesh_instanced_things.inverse_transpose_model_transform = simd_inverse(simd_transpose(mesh_instanced_things.model_transform));
    mesh_instanced_things.albedo = make<float4>(1.0f, 1.0f, 1.0f, 1.0f);
    
    // Project things onto ground plane
    
    // TODO: more generally
    // - bound mesh
    // - transform bounding volumes
    // - bounding volume intersect frustum
    // - transform to frustum planes
    // - ray-intersect mesh

    // The ground cells in view, as render-space offsets from the focus tile
    rect<i32> grid_bounds;
    {
        simd_float4x4 A = uniforms.viewprojection_transform;
        
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
    
    
    
    
    
    
    // Per-frame geometry, in transient arena slices.  The symbols are slab
    // instances (WrySymbolInstance), expanded to cuboids by their vertex
    // function.
    WryBufferSlice symbol_instances = {};
    NSUInteger symbol_count = 0;
    // The slabs that shade what lies under them (WrySymbolInstance again,
    // with a decal reach)
    WryBufferSlice decal_instances = {};
    NSUInteger decal_count = 0;
    // Terrain tiles get their own buffers: same quad pipeline, but drawn
    // with the terrain palette (not the sprite atlas) bound as albedo.
    WryBufferSlice terrain_vertices = {};
    WryBufferSlice terrain_indices = {};
    NSUInteger terrain_index_count = 0;
    // Map mode: the world-map quad, in place of all of the above.
    WryBufferSlice map_vertices = {};
    WryBufferSlice map_indices = {};
    NSUInteger map_index_count = 0;
    MeshInstanced map_instanced = mesh_instanced_things;
    _furnace_mesh.instanceCount = 0;
    _mine_mesh.instanceCount = 0;
    _truck_mesh.instanceCount = 0;
    _container_mesh.instanceCount = 0;
    // raid model for data

    if (show_map) {

        // Map mode: a single quad covering the mapped world rect, textured
        // by the current map at one texel per tile, through the same lit
        // G-buffer path as the world -- none of the per-tile / per-entity
        // content is built or drawn.  Tile (x, y) spans x +/- 0.5, so the
        // rect runs [X0 - 0.5, X0 + EXTENT - 0.5) per axis and uv 0..1
        // lands texel centers on tile centers.
        //
        // The zoom (x64 canonically; pinch varies _map_zoom) is a model
        // transform: scale the quad by 1/zoom about the camera focus,
        // leaving the camera itself alone, so the view direction and
        // lighting are the world's own.
        map_instanced.model_transform = _model->map_model_transform(_model->_map_zoom);
        map_instanced.inverse_transpose_model_transform
            = simd_inverse(simd_transpose(map_instanced.model_transform));

        const float2 map_origin = _model->render_position(wry::Coordinate{wry::WorldMap::X0, wry::WorldMap::Y0});
        const float mx0 = map_origin.x - 0.5f;
        const float my0 = map_origin.y - 0.5f;
        const float mx1 = mx0 + (float)wry::WorldMap::EXTENT;
        const float my1 = my0 + (float)wry::WorldMap::EXTENT;

        MeshVertex v;
        v.tangent = make<float4>(1.0f, 0.0f, 0.0f, 0.0f);
        v.bitangent = make<float4>(0.0f, 1.0f, 0.0f, 0.0f);
        v.normal = make<float4>(0.0f, 0.0f, 1.0f, 0.0f);

        MeshVertex quad[4];
        v.position = make<float4>(mx0, my0, 0.0f, 1.0f);
        v.coordinate = make<float4>(0.0f, 0.0f, 0.0f, 1.0f);
        quad[0] = v;
        v.position = make<float4>(mx1, my0, 0.0f, 1.0f);
        v.coordinate = make<float4>(1.0f, 0.0f, 0.0f, 1.0f);
        quad[1] = v;
        v.position = make<float4>(mx1, my1, 0.0f, 1.0f);
        v.coordinate = make<float4>(1.0f, 1.0f, 0.0f, 1.0f);
        quad[2] = v;
        v.position = make<float4>(mx0, my1, 0.0f, 1.0f);
        v.coordinate = make<float4>(0.0f, 1.0f, 0.0f, 1.0f);
        quad[3] = v;
        const uint quad_indices[6] = {0, 0, 1, 3, 2, 2};

        map_vertices = [_ctx.arena copyBytes:quad length:sizeof(quad)];
        map_indices = [_ctx.arena copyBytes:quad_indices length:sizeof(quad_indices)];
        map_index_count = 6;

    } else {

        auto tnow = world_get_time(new_world._ptr);
        auto&& entities = new_world->_entity_for_entity_id;

        // Visible entities by Morton-range descent over the location
        // multimap (the occupancy/location split's authoritative
        // Coordinate -> Set<EntityID>): O(visited blocks + hits), not
        // O(all entities).  The query rect is the tile loop's grid_bounds
        // (in closed form), so an entity is emitted iff it is located at
        // a drawn cell.  This surfaces the non-occupying statics (Source,
        // Sink, Spawner) that the occupancy map hid.  An entity located
        // at several cells (a travelling machine holds both endpoints)
        // surfaces once per cell, hence sort+unique.  The Player has no
        // location and is not drawn by this loop.
        std::vector<EntityID> visible_ids;
        visit_in_region(new_world->_located_for_coordinate,
                        tile_at(grid_bounds.a.x, grid_bounds.a.y),
                        tile_at(grid_bounds.b.x - 1, grid_bounds.b.y - 1),
                        [&visible_ids](Coordinate, const EntityIDSet& located) {
                            located.for_each([&visible_ids](EntityID id) {
                                if (id) visible_ids.push_back(id);
                            });
                        });
        std::sort(visible_ids.begin(), visible_ids.end());
        visible_ids.erase(std::unique(visible_ids.begin(), visible_ids.end()),
                          visible_ids.end());
        std::vector<Entity const*> ptrs;
        ptrs.reserve(visible_ids.size());
        for (EntityID id : visible_ids) {
            Entity const* e = nullptr;
            if (entities.try_get(id, e) && e)
                ptrs.push_back(e);
        }

        // Accumulate the symbol slabs into a growable buffer and upload it
        // once below.  Reserve a rough lower bound (one per grid cell and
        // per entity, plus the cursor); this is only a hint.
        std::vector<WrySymbolInstance> symbols;
        std::vector<WrySymbolInstance> decals;
        size_t symbol_hint = 2
            + (size_t)(grid_bounds.b.x - grid_bounds.a.x) * (size_t)(grid_bounds.b.y - grid_bounds.a.y)
            + ptrs.size();
        symbols.reserve(symbol_hint);
        decals.reserve(symbol_hint);

        // A symbol slab centred at `location`, showing the atlas cell at
        // `coordinate` (its top-left uv), its face spanned by the world
        // unit vectors `across` (image right) and `up` (image up): the
        // glyph's frame, the central half of its cell, is one world unit
        // square, and the cuboid covers just that frame while the field
        // (and the decal) reach out to the cell's edge; as thick as the
        // cell's stroke width, with top and bottom fillets of a quarter
        // of it (symbol_sdf.hpp).
        // A slab lying flat near the ground also casts its ambient
        // occlusion on what is under it, as a decal (Shaders.metal,
        // symbol_decal_fragment_function): its reach is where the
        // section's far-field tail (h + t/2) t / (2 d^2) falls below
        // two percent.  Higher slabs (the stacks) are skipped: their
        // occlusion is a faint blur the stroke model no longer describes.
        const float texels_per_cell = (float)(_symbols.width / wry::SymbolSDF::kCells);
        const float texels_per_unit = texels_per_cell * 0.5f;   // the glyph's frame
        auto emit_symbol = [&](simd_float4 location, simd_float4 coordinate,
                               simd_float3 across, simd_float3 up) {
            const int cx = std::clamp((int)std::lround(coordinate.x * wry::SymbolSDF::kCells), 0, wry::SymbolSDF::kCells - 1);
            const int cy = std::clamp((int)std::lround(coordinate.y * wry::SymbolSDF::kCells), 0, wry::SymbolSDF::kCells - 1);
            // Capped: a solid tile's stroke is the whole cell; 20 texels
            // keeps a ground symbol's underside (centred at z = 0.1, 12.8
            // texels up) above the ground, where its decal can land
            const float width = std::max(_symbolStrokeWidth[(size_t)cy * wry::SymbolSDF::kCells + (size_t)cx], 2.0f);
            const float stroke = std::min(width, 20.0f);
            // Slab space is source texels; the frame is one world unit
            const float texel = 1.0f / texels_per_unit;
            simd_float3 normal = simd_cross(across, up);
            simd_float4x4 model = simd_matrix(simd_make_float4(across * texel, 0.0f),
                                              simd_make_float4(up * texel, 0.0f),
                                              simd_make_float4(normal * texel, 0.0f),
                                              simd_make_float4(location.xyz, 1.0f));
            WrySymbolInstance s = {};
            s.model_transform = model;
            s.inverse_model_transform = simd_inverse(model);
            s.cell = make<float4>(coordinate.x, coordinate.y, 1.0f / wry::SymbolSDF::kCells, 1.0f / (float)_symbols.width);
            s.half_extent = make<float4>(texels_per_unit * 0.5f, texels_per_unit * 0.5f, stroke * 0.5f, texels_per_cell * 0.5f);
            s.top_radius = stroke * 0.25f;
            s.bottom_radius = stroke * 0.25f;
            s.stroke_width = width;
            s.decal_reach = 0.0f;
            if (normal.z > 0.99f) {
                // Flat: the underside's height above the ground, in texels
                const float h = (location.z - stroke * 0.5f * texel) * texels_per_unit;
                if (h <= 2.0f * width) {
                    const float tail = (std::max(h, 0.25f) + stroke * 0.5f) * stroke;
                    s.decal_reach = std::min(std::sqrt(tail / (2.0f * 0.02f)), wry::SymbolSDF::kFarRange);
                    decals.push_back(s);
                }
            }
            symbols.push_back(s);
        };

        MeshVertex v;
        v.tangent = make<float4>(1.0f, 0.0f, 0.0f, 0.0f);
        v.bitangent = make<float4>(0.0f, 1.0f, 0.0f, 0.0f);
        v.normal = make<float4>(0.0f, 0.0f, 1.0f, 0.0f);

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
                auto x0 = _model->render_position(p->_old_location);
                auto x1 = _model->render_position(p->_new_location);
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
                
                
                auto A = simd_matrix_translate(location);
                
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
                        auto A = simd_matrix_translate(location);
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
                    } else if (wry::term_is_boolean(value)) {
                        coordinate = wry::term_as_boolean(value)
                            ? _true_coordinate : _false_coordinate;
                    } else if (value.is_inty()) {
                        // number, as hex
                        coordinate = _digit_zero_coordinate
                            + make<float4>((value.as_int() & 15) / (float)wry::SymbolSDF::kCells, 0.0f, 0.0f, 0.0f);
                    } else {
                        // not renderable yet (heap integer, string, ...):
                        // the same neutral dot the ground uses, rather than
                        // a garbage hex digit cut from the bit pattern
                        coordinate = _cdot_coordinate;
                    }
                    emit_symbol(location, coordinate,
                                make<float3>(1.0f, 0.0f, 0.0f), make<float3>(0.0f, 1.0f, 0.0f));
                }
                
            } else if (auto p = dynamic_cast<const LocalizedEntity*>(q)){

                simd_float4 location = make<float4>(_model->render_position(p->_location) + make<float2>(0.0f, 1.0f), 0.0f, 1.0f);
                auto A = simd_matrix_translate(location) * simd_matrix_scale(0.5f);

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
                    
                    simd_float4 coordinate;
                    if (wry::term_is_boolean(r->_of_this)) {
                        coordinate = wry::term_as_boolean(r->_of_this)
                            ? _true_coordinate : _false_coordinate;
                    } else {
                        coordinate = _digit_zero_coordinate
                            + make<float4>(((r->_of_this.is_inty() ? r->_of_this.as_int() : 0) & 15) / (float)wry::SymbolSDF::kCells, 0.0f, 0.0f, 0.0f);
                    }
                    
                    // Upright on the source's front face: image right is
                    // world x, image up is world z
                    emit_symbol(location + make<float4>(0.0f, -0.1f, 0.0f, 0.0f), coordinate,
                                make<float3>(1.0f, 0.0f, 0.0f), make<float3>(0.0f, 0.0f, 1.0f));

                }

                
            }
            
        }
#endif

        for (int i = grid_bounds.a.x; i != grid_bounds.b.x; ++i) {
            for (int j = grid_bounds.a.y; j != grid_bounds.b.y; ++j) {

                simd_float4 location = make<float4>(i, j, 0.1f, 1.0f);
                simd_float4 coordinate = _border_coordinate;
                bool not_empty = true;

                {
                    wry::Term q = {};
                    not_empty = new_world->_term_for_coordinate.try_get(tile_at(i, j), q);
                    // printf("(%d, %d)=%llx -> (%d) %llx\n", i, j, wry::Coordinate{i, j}.data(), q._data);
                    if (!not_empty)
                        continue;
                    // A committed null write erases the cell, so a stored
                    // null only survives from older worlds; either way it
                    // is the empty cell, not something to mark with a dot.
                    if (wry::term_is_null(q))
                        continue;
                    if (wry::term_is_boolean(q)) {
                        coordinate = wry::term_as_boolean(q)
                            ? _true_coordinate : _false_coordinate;
                    } else if (q.is_inty()) {
                        // number, as hex
                        coordinate = _digit_zero_coordinate
                            + make<float4>((q.as_int() & 15) / (float)wry::SymbolSDF::kCells, 0.0f, 0.0f, 0.0f);
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
                        coordinate = _cdot_coordinate;
                        auto A = simd_matrix_translate(location);
                        A = A * simd_matrix_scale(container_mesh_scale);
                        MeshInstanced m;
                        m.model_transform = A;
                        m.inverse_transpose_model_transform = inverse(transpose(A));
                        m.albedo = make<float4>(1.0, 1.0, 1.0, 1.0);
                        [_container_mesh addInstance:m];
                    } else {
                        //printf("q is mystery\n");
                        coordinate = _cdot_coordinate;
                    }
                }
                
                emit_symbol(location, coordinate,
                            make<float3>(1.0f, 0.0f, 0.0f), make<float3>(0.0f, 1.0f, 0.0f));
            }
        }

        {

            // Terrain: one material-atlas-textured quad per generated tile
            // in view, at z = 0 under the tile/entity layer.  Sourced by
            // Morton-range descent over the terrain map, like the entity
            // query above; cells that have no terrain yet (beyond the
            // generated region) draw nothing and show the clear color.
            std::vector<MeshVertex> terrain_vbuf;
            std::vector<uint> terrain_ibuf;
            uint terrain_k = 0;
            visit_in_region(new_world->_terrain_for_coordinate,
                            tile_at(grid_bounds.a.x, grid_bounds.a.y),
                            tile_at(grid_bounds.b.x - 1, grid_bounds.b.y - 1),
                            [&](Coordinate xy, wry::Terrain t) {
                if ((t < 0) || (t >= wry::TERRAIN_KIND_COUNT))
                    t = wry::TERRAIN_KIND_COUNT - 1;
                // Atlas cell: material row from the terrain kind, variant
                // column from the coordinate hash.  Corner order follows
                // the sprite quads (world +y = image up = smaller v),
                // which also orients the GL-convention normal map's +Y
                // with the +y bitangent.  The cells' baked border is the
                // bleed gutter.
                float u0 = (float)wry::terrain_variant(xy) * 0.25f;
                float v0 = (float)t * 0.25f;
                simd_float4 location = make<float4>(_model->render_position(xy), 0.0f, 1.0f);
                v.position = make<float4>(-0.5f, -0.5f, 0.0f, 0.0f) + location;
                v.coordinate = make<float4>(u0, v0 + 0.25f, 0.0f, 1.0f);
                terrain_vbuf.push_back(v);
                v.position = make<float4>(+0.5f, -0.5f, 0.0f, 0.0f) + location;
                v.coordinate = make<float4>(u0 + 0.25f, v0 + 0.25f, 0.0f, 1.0f);
                terrain_vbuf.push_back(v);
                v.position = make<float4>(+0.5f, +0.5f, 0.0f, 0.0f) + location;
                v.coordinate = make<float4>(u0 + 0.25f, v0, 0.0f, 1.0f);
                terrain_vbuf.push_back(v);
                v.position = make<float4>(-0.5f, +0.5f, 0.0f, 0.0f) + location;
                v.coordinate = make<float4>(u0, v0, 0.0f, 1.0f);
                terrain_vbuf.push_back(v);
                terrain_ibuf.push_back(terrain_k);
                terrain_ibuf.push_back(terrain_k);
                terrain_ibuf.push_back(terrain_k + 1);
                terrain_ibuf.push_back(terrain_k + 3);
                terrain_ibuf.push_back(terrain_k + 2);
                terrain_ibuf.push_back(terrain_k + 2);
                terrain_k += 4;
            });
            terrain_index_count = terrain_ibuf.size();
            if (terrain_index_count) {
                terrain_vertices = [_ctx.arena copyBytes:terrain_vbuf.data()
                                                  length:terrain_vbuf.size() * sizeof(MeshVertex)];
                terrain_indices = [_ctx.arena copyBytes:terrain_ibuf.data()
                                                 length:terrain_ibuf.size() * sizeof(uint)];
            }

        }

        {
            // The cursor's tile (render space, like _mouse4)
            simd_float4 location = _model->_mouse4;
            location.x = round(location.x);
            location.y = round(location.y);
            location.z = 0.1f;
            simd_float4 coordinate = _border_coordinate;
            emit_symbol(location, coordinate,
                        make<float3>(1.0f, 0.0f, 0.0f), make<float3>(0.0f, 1.0f, 0.0f));
        }

        // Upload exactly what we accumulated.
        symbol_count = symbols.size();
        if (symbol_count)
            symbol_instances = [_ctx.arena copyBytes:symbols.data()
                                              length:symbols.size() * sizeof(WrySymbolInstance)];
        decal_count = decals.size();
        if (decal_count)
            decal_instances = [_ctx.arena copyBytes:decals.data()
                                             length:decals.size() * sizeof(WrySymbolInstance)];

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
        
        // We don't render the terrain tiles here as geometry in the z = 0
        // ground plane can't cast shadows; is there anything else in this
        // category to discard?

        // TODO: Think about draw order here to maximize fragment discard
        // Are 2D entities mostly just above the ground plane and thus behind
        // any 3D geometry?

        [render_command_encoder setVertexBytes:&uniforms
                                        length:sizeof(MeshUniforms)
                                       atIndex:AAPLBufferIndexUniforms];
        [render_command_encoder setFragmentBytes:&uniforms
                                          length:sizeof(MeshUniforms)
                                         atIndex:AAPLBufferIndexUniforms];

        // The symbol slabs (none in map mode: the map quad lies in the
        // ground plane and cannot cast); their fragments march the slab
        // along the light's rays and write the hit's depth

        if (!show_map && symbol_count) {
            [render_command_encoder setRenderPipelineState:_symbolShadowRenderPipelineState];
            [render_command_encoder setCullMode:MTLCullModeBack];
            [render_command_encoder setVertexBuffer:symbol_instances.buffer
                                             offset:symbol_instances.offset
                                            atIndex:AAPLBufferIndexInstanced];
            [render_command_encoder setFragmentBuffer:symbol_instances.buffer
                                               offset:symbol_instances.offset
                                              atIndex:AAPLBufferIndexInstanced];
            [render_command_encoder setFragmentTexture:_symbolField atIndex:AAPLTextureIndexNormal];
            [render_command_encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                       vertexStart:0
                                       vertexCount:36
                                     instanceCount:symbol_count];
            [render_command_encoder setCullMode:MTLCullModeNone];
            [render_command_encoder setRenderPipelineState:_shadowMapRenderPipelineState];
        }

        // Draw the 3D entities (each no-ops on zero instances)

        [_furnace_mesh drawWithRenderCommandEncoder:render_command_encoder arena:_ctx.arena];
        [_mine_mesh drawWithRenderCommandEncoder:render_command_encoder arena:_ctx.arena];
        [_truck_mesh drawWithRenderCommandEncoder:render_command_encoder arena:_ctx.arena];
        [_container_mesh drawWithRenderCommandEncoder:render_command_encoder arena:_ctx.arena];
        
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
                //
                // The sky at its own radiance: with the sun at irradiance
                // 1 the two give a sunlit surface about equal shares
                // (day.png's cosine-weighted mean radiance is about 0.2,
                // the sun's share albedo * 0.8 / pi is 0.25)
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
                [encoder setFragmentTexture:_black atIndex:AAPLTextureIndexMetallic];
                [encoder setFragmentTexture:_blue atIndex:AAPLTextureIndexNormal];
                [encoder setFragmentTexture:_white atIndex:AAPLTextureIndexRoughness];
                [encoder setFragmentTexture:_white atIndex:AAPLTextureIndexOcclusion];

                if (show_map) {

                    // Map mode: the world-map quad alone, lit by the same
                    // deferred pipeline as the world it stands in for.
                    [encoder setFragmentTexture:_mapTexture[_mapFrontIndex] atIndex:AAPLTextureIndexAlbedo];
                    [encoder setVertexBuffer:map_vertices.buffer offset:map_vertices.offset atIndex:AAPLBufferIndexVertices];
                    [encoder setVertexBytes:&map_instanced
                                     length:sizeof(MeshInstanced)
                                    atIndex:AAPLBufferIndexInstanced];
                    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip
                                        indexCount:map_index_count
                                         indexType:MTLIndexTypeUInt32
                                       indexBuffer:map_indices.buffer
                                 indexBufferOffset:map_indices.offset];

                } else {

                    [encoder setVertexBytes:&mesh_instanced_things
                                     length:sizeof(MeshInstanced)
                                    atIndex:AAPLBufferIndexInstanced];

                    // The symbol slabs first: their own pipeline marches
                    // the field and writes the hit's depth, so the depth
                    // test orders them against the meshes and terrain as
                    // it did the quads.  Restore the mesh bindings after.
                    if (symbol_count) {
                        [encoder setRenderPipelineState:_symbolGBufferRenderPipelineState];
                        [encoder setVertexBuffer:symbol_instances.buffer
                                          offset:symbol_instances.offset
                                         atIndex:AAPLBufferIndexInstanced];
                        [encoder setFragmentBuffer:symbol_instances.buffer
                                            offset:symbol_instances.offset
                                           atIndex:AAPLBufferIndexInstanced];
                        //[encoder setFragmentTexture:_symbols atIndex:AAPLTextureIndexAlbedo];
                        [encoder setFragmentTexture:_white atIndex:AAPLTextureIndexAlbedo];
                        [encoder setFragmentTexture:_symbolField atIndex:AAPLTextureIndexNormal];
                        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                    vertexStart:0
                                    vertexCount:36
                                  instanceCount:symbol_count];
                        [encoder setRenderPipelineState:_deferredGBufferRenderPipelineState];
                        [encoder setVertexBytes:&mesh_instanced_things
                                         length:sizeof(MeshInstanced)
                                        atIndex:AAPLBufferIndexInstanced];
                        [encoder setFragmentTexture:_blue atIndex:AAPLTextureIndexNormal];
                    }

                    // Terrain tiles (a shadow receiver only, so they skip
                    // the shadow pass entirely): the full PBR material
                    // atlases, with ORM feeding the occlusion / roughness
                    // / metallic slots from one texture.
                    if (terrain_index_count) {
                        [encoder setFragmentTexture:_terrainAlbedo atIndex:AAPLTextureIndexAlbedo];
                        [encoder setFragmentTexture:_terrainNormal atIndex:AAPLTextureIndexNormal];
                        [encoder setFragmentTexture:_terrainORM atIndex:AAPLTextureIndexMetallic];
                        [encoder setFragmentTexture:_terrainORM atIndex:AAPLTextureIndexRoughness];
                        [encoder setFragmentTexture:_terrainORM atIndex:AAPLTextureIndexOcclusion];
                        [encoder setVertexBuffer:terrain_vertices.buffer offset:terrain_vertices.offset atIndex:AAPLBufferIndexVertices];
                        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip
                                            indexCount:terrain_index_count
                                             indexType:MTLIndexTypeUInt32
                                           indexBuffer:terrain_indices.buffer
                                     indexBufferOffset:terrain_indices.offset];
                    }

                }

                [_furnace_mesh drawWithRenderCommandEncoder:encoder arena:_ctx.arena];
                [_mine_mesh drawWithRenderCommandEncoder:encoder arena:_ctx.arena];
                [_truck_mesh drawWithRenderCommandEncoder:encoder arena:_ctx.arena];
                [_container_mesh drawWithRenderCommandEncoder:encoder arena:_ctx.arena];

                // Symbol decals: now that everything that can lie under a
                // slab is in the G-buffer, multiply the slabs' ambient
                // occlusion into its occlusion channel.  No depth test:
                // the box's fragments find their surface from the
                // G-buffer's depth.
                if (decal_count && _model->_show_symbol_decals) {
                    [encoder setRenderPipelineState:_symbolDecalRenderPipelineState];
                    [encoder setDepthStencilState:_ctx.disabledDepthStencilState];
                    [encoder setVertexBuffer:decal_instances.buffer
                                      offset:decal_instances.offset
                                     atIndex:AAPLBufferIndexInstanced];
                    [encoder setFragmentBuffer:decal_instances.buffer
                                        offset:decal_instances.offset
                                       atIndex:AAPLBufferIndexInstanced];
                    [encoder setFragmentTexture:_symbolField atIndex:AAPLTextureIndexNormal];
                    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                vertexStart:0
                                vertexCount:36
                              instanceCount:decal_count];
                    [encoder setDepthStencilState:_ctx.enabledDepthStencilState];
                }

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
                [encoder setFragmentTexture:_deferredLightIrradianceTexture atIndex:AAPLTextureIndexIrradiance];
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
                
                // The sun's irradiance at normal incidence (the Lambert
                // term divides by pi); see ibl_scale above for the balance
                uniforms.radiance = simd_make_float3(1.0f, 1.0f, 1.0f);
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


// Shadow map sizing.  The map must cover the drawable exactly (the screen
// rect samples it texel-per-pixel; see _regenerate_uniforms) plus a margin
// for out-of-plane geometry: off-screen casters on the sunward side shade
// screen pixels, and elevated on-screen receivers sample beyond the
// opposite edge.  A caster or receiver of height h reaches
// h * |light.xy| / light.z world units, times pixels-per-world-unit (which
// scales with the drawable at our fixed camera); an eighth of each
// dimension per side covers heights to about 3 world units without
// modelling either bound.  NB: grid_bounds culls the shadow pass to
// roughly the visible cells anyway, so today a caster more than a cell or
// two off-screen is absent from the map regardless of margin; the shadow
// pass's cull, not this margin, is the binding constraint on how far
// off-screen shadows can originate.
//
// Reallocation policy: quantize the wanted size up to 256-texel steps, so
// a live window resize reallocates at most once per couple hundred pixels
// of growth; grow whenever the map is too small; shrink only when at
// least two whole quanta too big in some dimension, so jitter around a
// step boundary can't thrash.  256 is even, so on the usual all-even
// path the centering offset stays a whole texel and the snap term in
// _regenerate_uniforms is the identity.
- (void)resizeShadowMapForDrawableSize:(CGSize)drawableSize
{
    if (!(drawableSize.width >= 1.0) || !(drawableSize.height >= 1.0))
        return; // degenerate (miniaturized?); keep whatever we have

    // All current Metal GPUs top out at 16384 per dimension
    NSUInteger want_w = MIN((NSUInteger)ceil(drawableSize.width * 1.25 / 256.0) * 256, 16384);
    NSUInteger want_h = MIN((NSUInteger)ceil(drawableSize.height * 1.25 / 256.0) * 256, 16384);

    NSUInteger have_w = _shadowMapTarget.width;  // 0 when not yet allocated
    NSUInteger have_h = _shadowMapTarget.height;

    bool too_small = (have_w < want_w) || (have_h < want_h);
    bool too_big = (have_w >= want_w + 512) || (have_h >= want_h + 512);
    if (too_small || too_big) {
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
        descriptor.textureType = MTLTextureType2D;
        descriptor.width = want_w;
        descriptor.height = want_h;
        descriptor.pixelFormat = MTLPixelFormatDepth32Float;
        descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        descriptor.resourceOptions = MTLResourceStorageModePrivate;
        _shadowMapTarget = [_ctx.device newTextureWithDescriptor:descriptor];
        _shadowMapTarget.label = [NSString stringWithFormat:@"Shadow map texture %lux%lu",
                                  (unsigned long)want_w, (unsigned long)want_h];
        // Any in-flight frame retains the texture it encoded against, so
        // dropping our reference here is safe.
    }

    // Mirror the actual size for _regenerate_uniforms (the caller runs it
    // next).  Unconditional, so the invariant is simply: after this method,
    // _shadow_map_size is the bound texture's size.
    _model->_shadow_map_size.x = (float)_shadowMapTarget.width;
    _model->_shadow_map_size.y = (float)_shadowMapTarget.height;
}

-(void)drawableResize:(CGSize)drawableSize
{

    _model->_gui.viewport_size.x = drawableSize.width;
    _model->_gui.viewport_size.y = drawableSize.height;

    // (Re)allocate the shadow map to cover the new drawable before the
    // uniforms bake in the screen-rect-to-map fit from its size.
    [self resizeShadowMapForDrawableSize:drawableSize];

    _model->_regenerate_uniforms();

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
