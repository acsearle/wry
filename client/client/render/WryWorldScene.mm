//
//  WryWorldScene.mm
//  client
//
//  Created by Antony Searle on 1/7/2023.
//

// Shared Metal header first for no contamination
#include "ShaderTypes.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <bit>
#include <random>

#include <MetalKit/MetalKit.h>
#include <MetalPerformanceShaders/MetalPerformanceShaders.h>

#include <sqlite3.h>

#include "load_timing.hpp"
#include "WryMesh.h"
#include "WryRenderContext.h"
#include "WryWorldScene.h"

#include "SpriteAtlas.hpp"
#include "csv.hpp"
#include "debug.hpp"
#include "font.hpp"
#include "gui_widget.hpp"
#include "json.hpp"
#include "machine.hpp"
#include "decal_text.hpp"
#include "font.hpp"
#include "number_caption.hpp"
#include "spawner.hpp"
#include "matter.hpp"
#include "mesh.hpp"
#include "palette.hpp"
#include "platform.hpp"
#include "sRGB.hpp"
#include "sdf.hpp"
#include "symbol_sdf.hpp"
#include "term.hpp"
#include "text.hpp"
#include "Wavefront.hpp"
#include "world.hpp"

#include "save.hpp"
#include "server.hpp"

static_assert(WRY_TERRAIN_KINDS == wry::TERRAIN_KIND_COUNT,
              "ShaderTypes.h WryTerrainUniforms is sized per terrain kind");

// One character of a number tile's caption (number_caption.hpp), laid
// out on the tile's face: centred `x` world units right of the face's
// centre, `w` wide and `h` tall, showing `uv_rect` of the cutout atlas
// (_glyphs) in `tint`
struct WryNumberGlyph {
    float x;
    float w;
    float h;
    simd_float4 uv_rect;
    simd_float4 tint;
};

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
    id <MTLRenderPipelineState> _mapGBufferRenderPipelineState;  // the map quad: nearest magnification
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
    // The cutout glyphs (assets.png, whatever atlas _symbols shows), for
    // decals that print a glyph on a surface: the digits on a number tile
    id<MTLTexture> _glyphs;
    // Strings printed as decals (decal_text.hpp): the face, rasterized on
    // first use (the `caption-decals` command) into its own texture, and
    // each atlas cell's caption (an opcode's name, a matter's kind; empty
    // for the rest), the text tools/make_tile_atlas.py prints on the tile
    // faces of assets_tile.png and leaves blank on assets_tile_nocaption.png
    wry::DecalFont _decalFont;
    id<MTLTexture> _decalFontTexture;
    std::array<std::string, wry::SymbolSDF::kCells * wry::SymbolSDF::kCells> _cellCaption;
    std::array<bool, wry::SymbolSDF::kCells * wry::SymbolSDF::kCells> _cellCaptionLight;   // white ink (the dark matter tiles)
    WryMesh* _hackmesh;

    // The symbol field beside the atlas (symbol_sdf.hpp), its per-cell
    // stroke widths, and the slab pipelines that march it in the G-buffer
    // and shadow passes
    id<MTLTexture> _symbolField;
    std::string _symbolAtlasName;      // loaded; the model's _symbol_atlas asks for another
    std::string _symbolAtlasMissing;   // the last name that had no png, so it is not retried every frame
    std::vector<float> _symbolStrokeWidth;
    id<MTLRenderPipelineState> _symbolGBufferRenderPipelineState;
    id<MTLRenderPipelineState> _terrainGBufferRenderPipelineState;
    id<MTLRenderPipelineState> _symbolShadowRenderPipelineState;
    id<MTLRenderPipelineState> _symbolDecalRenderPipelineState;
    id<MTLRenderPipelineState> _decalRenderPipelineState;   // images onto the G-buffer (WryDecalInstance)

    // Screen-space ambient occlusion: the image-based light accumulates
    // in its own attachment, the estimators run over the stored depth
    // and normal between the two passes, and the composite adds the
    // ambient back scaled by the result
    id<MTLTexture> _deferredAmbientColorAttachmentTexture;
    id<MTLTexture> _ssaoTexture;
    id<MTLTexture> _ssaoBlurredTexture;
    // The GTAO taps' depth pyramid (ShaderTypes.h), and a view per level
    // for the kernel that builds it
    id<MTLTexture> _depthPyramid;
    id<MTLTexture> _depthPyramidLevels[WRY_SSAO_DEPTH_PYRAMID_LEVELS];
    id<MTLComputePipelineState> _ssaoDepthPyramidPipelineState;
    id<MTLComputePipelineState> _ssaoGTAOPipelineState;
    id<MTLComputePipelineState> _ssaoLuftPipelineState;
    id<MTLComputePipelineState> _ssaoBlurPipelineState;
    id<MTLComputePipelineState> _ssaoBlurAxisPipelineState;
    id<MTLComputePipelineState> _ssaoBlurTiledPipelineState;
    id<MTLRenderPipelineState> _compositeRenderPipelineState;
    
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
    simd_float4 _token_truck_coordinate;   // the machine's playing piece (atlas row 15)
    std::string _loaded_environment;       // the name _deferredLightImageBasedTexture was made from
    simd_float4 _blank_coordinate;         // the plain tile numbers are printed on
    simd_float4 _dash_coordinate;          // the minus sign's glyph
    // The number tiles' abbreviations (number_caption.hpp): the decimal
    // point and the SI suffixes k, M, G, T, P (atlas row 15 after the
    // token; 'E' is the hex row's)
    simd_float4 _point_coordinate;
    simd_float4 _si_coordinate[5];
    
    WryMesh* _truck_mesh;
    WryMesh* _mine_mesh;
    WryMesh* _furnace_mesh;
    // The visualization of MATTER_SHIPPING_CONTAINER
    WryMesh* _container_mesh;
    // The terrain matters (MATTER_WATER .. MATTER_LIMESTONE) are a dark
    // tile captioned with the kind (atlas row 2, by name from assets.json)
    // carrying a pile of it: sand art in an invisible cube, poured in
    // light grey layers (assets sandbox.obj + sandbox_layers.png,
    // tools/sandbox_gen.py), tinted MATTER_COLOR_SRGB
    WryMesh* _sandbox_mesh;
    // The Ship (spawner.hpp): a bulk carrier assembled from a kit of
    // one-cell modules, prow + four holds + stern (assets ship_*.obj,
    // ship_palette.png, tools/ship_gen.py), authored prow +x
    WryMesh* _ship_prow_mesh;
    WryMesh* _ship_hold_mesh;
    WryMesh* _ship_stern_mesh;
    std::array<simd_float4, std::size(wry::MATTER_NAMES)> _matter_coordinate;
    std::array<bool, std::size(wry::MATTER_NAMES)> _matter_has_coordinate;

    // The shake of a digging machine (Machine::PHASE_MINING): a fresh
    // random offset each frame.  Renderer-side only.
    std::minstd_rand _shake;
    
    // The opcode palette's data and selection now live on
    // _model->_palette_overlay; the renderer reads it for painting only.

    NSCursor* _cursor;
    
}


-(void) dealloc {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

// The decodes the init below makes, by the same paths and premultiplication
// (newTextureFromResource: and newMipmappedTextureFromResource: premultiply;
// the symbol atlas and glyphs do not).  The tiny constant-colour textures
// and the palette are left out: their decode is cheaper than the round
// trip.  A list that drifts from the init costs only speed: an asset taken
// but never requested decodes on the spot, and one requested but never
// taken stays decoded in the preload (memory, until the context dies).
+ (void)preloadAssetsWithContext:(WryRenderContext*)context {
    wry::AssetPreload* preload = context.assetPreload;
    if (!preload)
        return;
    // Longest first: the pool starts them in order
    preload->symbol_field(wry::path_for_resource("assets_tile", "png"));
    preload->image(wry::path_for_resource("assets_tile", "png"), false);
    preload->image(wry::path_for_resource("assets", "png"), false);
    for (const char* name : {"terrain/Terrain_Albedo", "terrain/Terrain_NormalGL", "terrain/Terrain_ORM",
                             "day", "sandbox_photo", "Container20_AO", "Container20_Normal", "testpattern"})
        preload->image(wry::path_for_resource(name, "png"), true);
    for (const char* name : {"sandbox.obj", "truck2.obj", "furnace.obj", "mine.obj",
                             "ship_prow.obj", "ship_hold.obj", "ship_stern.obj", "Container20_Proxy.obj"})
        preload->mesh(name);
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

        // WRY_LOAD_TIMING (core/load_timing.hpp): the pipelines and other
        // device objects up to the first texture
        std::optional<wry::LoadTimer> pipelinesTimer(std::in_place, "  pipelines and device state");
                
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
        _loaded_environment = "day";
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
            descriptor.colorAttachments[AAPLColorIndexAmbient].pixelFormat = MTLPixelFormatRGBA16Float;
            descriptor.colorAttachments[AAPLColorIndexAmbient].writeMask = MTLColorWriteMaskNone;
            descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::fragment_function"];
            descriptor.label = @"Deferred G-buffer";
            _deferredGBufferRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];

            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::map_fragment_function"];
            descriptor.label = @"Deferred G-buffer (world map)";
            _mapGBufferRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];

            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::symbol_vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::symbol_fragment_function"];
            descriptor.label = @"Symbol slab G-buffer";
            _symbolGBufferRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];

            // The board: the terrain inked per fragment from its kind map
            // and the symbol field's terrain row (ShaderTypes.h,
            // WryTerrainUniforms)
            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::terrain_vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::terrain_fragment_function"];
            descriptor.label = @"Board G-buffer";
            _terrainGBufferRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];

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

            // Image decals paint the emitted light, the albedo, the
            // normal (embossing) and the material's occlusion, roughness
            // and metallic in place (WryDecalInstance); the depth stays
            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::decal_vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::decal_fragment_function"];
            descriptor.label = @"Decal";
            descriptor.colorAttachments[AAPLColorIndexColor].writeMask = MTLColorWriteMaskAll;
            descriptor.colorAttachments[AAPLColorIndexAlbedo].writeMask = MTLColorWriteMaskAll;
            descriptor.colorAttachments[AAPLColorIndexNormal].writeMask = MTLColorWriteMaskAll;
            descriptor.colorAttachments[AAPLColorIndexMaterial].writeMask = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue;
            _decalRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];
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
        
            // The image-based light alone writes the ambient attachment
            descriptor.colorAttachments[AAPLColorIndexAmbient].writeMask = MTLColorWriteMaskAll;
            descriptor.colorAttachments[AAPLColorIndexColor].writeMask = MTLColorWriteMaskNone;
            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::lighting_vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::image_based_lighting_fragment_function"];
            descriptor.label = @"Deferred image-based light";
            _deferredLightImageBasedRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];
            descriptor.colorAttachments[AAPLColorIndexAmbient].writeMask = MTLColorWriteMaskNone;
            descriptor.colorAttachments[AAPLColorIndexColor].writeMask = MTLColorWriteMaskAll;

            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::lighting_vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::directional_lighting_fragment_function"];
            descriptor.label = @"Deferred shadowcasting directional light";
            _deferredLightDirectionalShadowcastingRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];

            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::lighting_vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::point_lighting_fragment_function"];
            descriptor.label = @"Deferred point light";
            _deferredLightPointRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];

        }

        {
            // The composite reads both light attachments and writes the
            // sum; no blending.  It runs in the second pass, which binds
            // only the two light attachments (and the depth buffer), so
            // the stored G-buffers can be read as textures there.
            MTLRenderPipelineDescriptor *descriptor = [[MTLRenderPipelineDescriptor alloc] init];
            descriptor.colorAttachments[AAPLColorIndexColor].pixelFormat = _ctx.drawablePixelFormat;
            descriptor.colorAttachments[AAPLColorIndexColor].blendingEnabled = NO;
            descriptor.colorAttachments[AAPLColorIndexAmbient].pixelFormat = MTLPixelFormatRGBA16Float;
            descriptor.colorAttachments[AAPLColorIndexAmbient].writeMask = MTLColorWriteMaskNone;
            descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            descriptor.vertexFunction = [_ctx newFunctionWithName:@"deferred::composite_vertex_function"];
            descriptor.fragmentFunction = [_ctx newFunctionWithName:@"deferred::composite_fragment_function"];
            descriptor.label = @"Composite direct + occluded ambient";
            _compositeRenderPipelineState = [_ctx newRenderPipelineStateWithDescriptor:descriptor];
        }

        {
            // The occlusion estimators and their blur
            NSError* error = nil;
            _ssaoGTAOPipelineState = [_ctx.device newComputePipelineStateWithFunction:[_ctx newFunctionWithName:@"deferred::ssao_gtao_kernel"] error:&error];
            assert(_ssaoGTAOPipelineState);
            _ssaoLuftPipelineState = [_ctx.device newComputePipelineStateWithFunction:[_ctx newFunctionWithName:@"deferred::ssao_luft_kernel"] error:&error];
            assert(_ssaoLuftPipelineState);
            _ssaoBlurPipelineState = [_ctx.device newComputePipelineStateWithFunction:[_ctx newFunctionWithName:@"deferred::ssao_blur_kernel"] error:&error];
            assert(_ssaoBlurPipelineState);
            _ssaoBlurAxisPipelineState = [_ctx.device newComputePipelineStateWithFunction:[_ctx newFunctionWithName:@"deferred::ssao_blur_axis_kernel"] error:&error];
            assert(_ssaoBlurAxisPipelineState);
            _ssaoBlurTiledPipelineState = [_ctx.device newComputePipelineStateWithFunction:[_ctx newFunctionWithName:@"deferred::ssao_blur_tiled_kernel"] error:&error];
            assert(_ssaoBlurTiledPipelineState);
            _ssaoDepthPyramidPipelineState = [_ctx.device newComputePipelineStateWithFunction:[_ctx newFunctionWithName:@"deferred::ssao_depth_pyramid_kernel"] error:&error];
            assert(_ssaoDepthPyramidPipelineState);
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
            // The symbol atlas and its field are loaded by
            // -loadSymbolAtlasNamed: below, once the opcode table exists
            pipelinesTimer.reset();
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
            // loaded linear.  Each is 4 x 4 tiles of 256 px; the mip
            // kernel stays inside a tile.
            _terrainAlbedo = [_ctx newMipmappedTextureFromResource:@"terrain/Terrain_Albedo"
            // _terrainAlbedo = [_ctx newMipmappedTextureFromResource:@"white"
                                                            ofType:@"png"
                                                   withPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB
                                                          cellSize:256];
            _terrainNormal = [_ctx newMipmappedTextureFromResource:@"terrain/Terrain_NormalGL"
            // _terrainNormal = [_ctx newMipmappedTextureFromResource:@"blue"
                                                            ofType:@"png"
                                                   withPixelFormat:MTLPixelFormatRGBA8Unorm
                                                          cellSize:256];
            _terrainORM = [_ctx newMipmappedTextureFromResource:@"terrain/Terrain_ORM"
            // _terrainORM = [_ctx newMipmappedTextureFromResource:@"orange"
                                                            ofType:@"png"
                                                   withPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB
                                                          cellSize:256];
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
                wry::LoadTimer timer("mesh ", v.c_str());
                // Preloaded (parsed and vertexified on the pool), or now
                std::optional<wry::mesh::mesh> preloaded;
                if (wry::AssetPreload* preload = _ctx.assetPreload) {
                    wry::LoadTimer timer("  take preloaded ", v.c_str());
                    preloaded = preload->take_mesh(v);
                }
                if (!preloaded) {
                    wry::LoadTimer timer("  from_obj and MeshVertexify ", v.c_str());
                    preloaded = wry::from_obj(v);
                    preloaded->MeshVertexify();
                }
                wry::mesh::mesh& m = *preloaded;
                
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
            f(_sandbox_mesh, "sandbox.obj");
            {
                id<MTLTexture> palette = [_ctx newMipmappedTextureFromResource:@"ship_palette"
                                                                        ofType:@"png"
                                                               withPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB];
                f(_ship_prow_mesh, "ship_prow.obj");
                f(_ship_hold_mesh, "ship_hold.obj");
                f(_ship_stern_mesh, "ship_stern.obj");
                // painted wood: a dielectric (f's default is fully
                // metallic, the white texture's blue channel)
                for (WryMesh* m : {_ship_prow_mesh, _ship_hold_mesh, _ship_stern_mesh}) {
                    m.albedoTexture = palette;
                    m.metallicTexture = _black;
                }
            }
            // loose stuff is a matte dielectric: roughness 1 (the white
            // texture's green channel) and metallic 0 (black's blue)
            _sandbox_mesh.roughnessTexture = _white;
            _sandbox_mesh.metallicTexture = _black;
            _sandbox_mesh.albedoTexture = [_ctx newMipmappedTextureFromResource:@"sandbox_photo"
                                                                         ofType:@"png"
                                                                withPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB];

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

                
            {
                wry::LoadTimer timer("loadSymbolAtlasNamed assets_tile");
                [self loadSymbolAtlasNamed:@"assets_tile"];
            }

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

                // Laid out in screen space by the overlay
                // (PaletteOverlay::cells_rect); controls._transform is
                // unused.
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

// The symbol atlas NAME.png (the assets folder; "assets" is the cutout
// glyphs, "assets_tile" the same glyphs printed on tiles, from
// tools/make_tile_atlas.py) and the field beside it, then the opcode
// labels stamped into the uppercase opcode cells and the mip chain.
// Called once from init and again whenever the `atlas` console command
// names another atlas (-encodeIntoCommandBuffer:); the textures in
// flight stay alive under their encoders.
- (void)loadSymbolAtlasNamed:(NSString*)name
{
    using namespace ::simd;
    using namespace ::wry;

    Table<wry::String, i64> name_to_opcode;
    for (auto&& [k, v] : wry::OPCODE_NAMES)
        name_to_opcode.emplace(v + 7, k);

    // Mip levels allocated now, generated below once the opcode
    // labels have been stamped into level 0.
    _symbols = [_ctx newTextureWithMipStorageFromResource:name ofType:@"png"];
    // The cutout glyphs for decals, always the plain atlas: straight
    // alpha, coloured margins, mips filled now (nothing is stamped on it)
    _glyphs = [_ctx newTextureFromResource:@"assets"
                                    ofType:@"png"
                           withPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB
                                 mipmapped:YES
                             premultiplied:NO];
    _glyphs.label = @"Glyphs";
    [_ctx generateMipmapsForTexture:_glyphs
                           cellSize:_glyphs.width / wry::SymbolSDF::kCells
                      straightAlpha:YES];
    {
        // The field beside the atlas (symbol_sdf.hpp): from its
        // cache, or built from the atlas's silhouette at half the
        // atlas resolution and cached
        auto t0 = std::chrono::steady_clock::now();
        wry::LoadTimer timer("  symbol field");
        const std::filesystem::path atlasPath = wry::path_for_resource(wry::StringView(name.UTF8String), "png");
        std::optional<wry::SymbolSDF> preloaded;
        if (wry::AssetPreload* preload = _ctx.assetPreload)
            preloaded = preload->take_symbol_field(atlasPath);
        wry::SymbolSDF sdf = preloaded ? std::move(*preloaded) : wry::symbol_sdf_for_atlas(atlasPath);
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

    // Defaults matching the shipped atlas layout, overwritten by
    // the by-name capture below; a json missing the names
    // degrades to these rather than rendering garbage
    constexpr float kCells = (float)wry::SymbolSDF::kCells;
    _digit_zero_coordinate = make<float4>(0.0f / kCells, 10.0f / kCells, 0.0f, 1.0f);
    _true_coordinate       = make<float4>(0.0f / kCells, 11.0f / kCells, 0.0f, 1.0f);
    _false_coordinate      = make<float4>(1.0f / kCells, 11.0f / kCells, 0.0f, 1.0f);
    _cdot_coordinate       = make<float4>(0.0f / kCells,  2.0f / kCells, 0.0f, 1.0f);
    _border_coordinate     = make<float4>(0.0f / kCells,  3.0f / kCells, 0.0f, 1.0f);
    _token_truck_coordinate = make<float4>(0.0f / kCells, 15.0f / kCells, 0.0f, 1.0f);
    _blank_coordinate       = make<float4>(4.0f / kCells, 11.0f / kCells, 0.0f, 1.0f);
    _dash_coordinate        = make<float4>(11.0f / kCells, 0.0f / kCells, 0.0f, 1.0f);
    _point_coordinate       = make<float4>(1.0f / kCells, 15.0f / kCells, 0.0f, 1.0f);
    for (int k = 0; k != 5; ++k)
        _si_coordinate[k] = make<float4>((float)(2 + k) / kCells, 15.0f / kCells, 0.0f, 1.0f);
    _matter_has_coordinate.fill(false);
    for (std::string& caption : _cellCaption)
        caption.clear();
    _cellCaptionLight.fill(false);

    try {
        auto x = json::from_file<ContiguousDeque<ContiguousDeque<String>>>("assets.json");
        ulong i = 0;
        for (const ContiguousDeque<String>& y : x) {
            ulong j = 0;
            for (const String& z : y) {
                // printf("loading image for %.*s\n", (int) z.chars.size(), (const char*) z.chars.data());
                simd_float4 coordinate = make<float4>(j / kCells, i / kCells, 0.0f, 1.0f);
                {
                    // The cell's caption, by make_tile_atlas.py's rule:
                    // rows 0-11, an uppercase name (Python's isupper:
                    // some letter, none lowercase) with underscores as
                    // spaces, the MATTER_ prefix dropped (white on the
                    // dark matter tiles)
                    std::string_view sv((const char*) z.chars.data(), z.chars.size());
                    const bool upper = std::any_of(sv.begin(), sv.end(), [](char c) { return c >= 'A' && c <= 'Z'; })
                        && std::none_of(sv.begin(), sv.end(), [](char c) { return c >= 'a' && c <= 'z'; });
                    const size_t k = (size_t)i * wry::SymbolSDF::kCells + (size_t)j;
                    if (i < 12 && j < (ulong)wry::SymbolSDF::kCells && upper) {
                        const bool matter = sv.starts_with("MATTER_");
                        std::string text(matter ? sv.substr(7) : sv);
                        std::replace(text.begin(), text.end(), '_', ' ');
                        _cellCaption[k] = std::move(text);
                        _cellCaptionLight[k] = matter;
                    }
                }
                // auto h = name_to_opcode._inner._hasher.get_hash(z);
                // printf("hash \"%.*s\" -> %llu\n", (int) z.chars.size(), (const char*) z.chars.data(), h);
                auto p = name_to_opcode.find(z);
                // printf("p choice\n");
                if (p == name_to_opcode.end()) {
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
                    else if (sv == "token_truck")
                        _token_truck_coordinate = coordinate;
                    else if (sv == "blank")
                        _blank_coordinate = coordinate;
                    else if (sv == "dash1")
                        _dash_coordinate = coordinate;
                    else if (sv == "point")
                        _point_coordinate = coordinate;
                    else if (sv.starts_with("si_") && sv.size() == 4
                             && std::string_view("kMGTP").find(sv[3]) != std::string_view::npos)
                        _si_coordinate[std::string_view("kMGTP").find(sv[3])] = coordinate;
                    // The matter tiles: the MATTER code's own name
                    else if (sv.starts_with("MATTER_")) {
                        wry::MATTER m = wry::MATTER_from_name(std::string(sv).c_str());
                        if ((m >= 0) && ((size_t)m < _matter_coordinate.size())) {
                            _matter_coordinate[(size_t)m] = coordinate;
                            _matter_has_coordinate[(size_t)m] = true;
                        }
                    }
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
                else /* if (p != name_to_opcode.end()) */ {
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
    // fill the mip chain.  Blocking; nothing has sampled it yet.  The
    // kernel stays inside each kCells x kCells cell
    {
        wry::LoadTimer timer("  symbol atlas mipmaps");
        [_ctx generateMipmapsForTexture:_symbols
                               cellSize:_symbols.width / wry::SymbolSDF::kCells
                          straightAlpha:YES];
    }

    _symbolAtlasName = name.UTF8String;
    NSLog(@"symbol atlas: %@", name);
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

// The decal face (decal_text.hpp), rasterized once on first use:
// Noto Sans Condensed SemiBold (OFL, assets/Noto_Sans/OFL.txt) at
// kDecalEmTexels to the em, about three texels per pixel for a tile
// caption at the closest zoom, into a straight-alpha texture with the
// binomial mips (the whole image one cell; the glyphs' padding keeps them
// apart down to level 4)
- (void)loadDecalFont
{
    constexpr float kDecalEmTexels = 96.0f;
    auto t0 = std::chrono::steady_clock::now();
    _decalFont = wry::build_decal_font("Noto_Sans/static/NotoSans_Condensed-SemiBold.ttf", kDecalEmTexels);
    MTLTextureDescriptor* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB
                                                           width:(NSUInteger)_decalFont.width
                                                          height:(NSUInteger)_decalFont.height
                                                       mipmapped:YES];
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    _decalFontTexture = [_ctx.device newTextureWithDescriptor:descriptor];
    [_decalFontTexture replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)_decalFont.width, (NSUInteger)_decalFont.height)
                         mipmapLevel:0
                           withBytes:_decalFont.rgba.data()
                         bytesPerRow:(NSUInteger)_decalFont.width * 4];
    _decalFontTexture.label = @"Decal font";
    [_ctx generateMipmapsForTexture:_decalFontTexture cellSize:0 straightAlpha:YES];
    _decalFont.rgba = {};   // on the GPU now
    auto t1 = std::chrono::steady_clock::now();
    NSLog(@"decal font: %d x %d texels at %.0f to the em, cap height %.3f em, ready in %.0f ms",
          _decalFont.width, _decalFont.height, kDecalEmTexels, _decalFont.cap_height,
          std::chrono::duration<double, std::milli>(t1 - t0).count());
}

// A number's caption as glyphs on its tile: number_caption's text (at
// most four characters and a minus) in a row of monospace advances, the
// minus and the decimal point taking 0.6 of one, shrunk to fit the face.
// Each character shows the central half of its atlas cell (its frame);
// the narrow ones the central quarter of the frame's width (6 of its 24
// units: the dash's middle, the point), the minus in the digits' blue
// (its dash glyph is white).
- (std::vector<WryNumberGlyph>)numberGlyphs:(int64_t)value
{
    using namespace simd;
    const float kDigitPitch = 12.5f / 24.0f;   // the monospace digits' advance, frame units -> world
    const float kNumberWidth = 0.78f;          // the row's room on the face
    const float cell_uv = 1.0f / (float)wry::SymbolSDF::kCells;
    const std::string text = wry::number_caption(value);
    auto narrow = [](char ch) { return ch == '-' || ch == '.'; };
    const float kNarrowAdvance = 0.6f;
    float advances = 0.0f;
    for (char ch : text)
        advances += narrow(ch) ? kNarrowAdvance : 1.0f;
    const float scale = std::min(1.0f, kNumberWidth / (kDigitPitch * advances));
    std::vector<WryNumberGlyph> result;
    float cursor = -0.5f * advances;
    for (char ch : text) {
        const float advance = narrow(ch) ? kNarrowAdvance : 1.0f;
        simd_float4 cell;
        if (ch == '-')
            cell = _dash_coordinate;
        else if (ch == '.')
            cell = _point_coordinate;
        else if (ch >= '0' && ch <= '9')
            cell = _digit_zero_coordinate + make<float4>((float)(ch - '0') * cell_uv, 0.0f, 0.0f, 0.0f);
        else if (ch == 'E')
            cell = _digit_zero_coordinate + make<float4>(14.0f * cell_uv, 0.0f, 0.0f, 0.0f);
        else {
            const size_t k = std::string_view("kMGTP").find(ch);
            assert(k != std::string_view::npos);
            cell = _si_coordinate[k];
        }
        WryNumberGlyph g;
        g.x = (cursor + 0.5f * advance) * kDigitPitch * scale;
        g.w = scale * (narrow(ch) ? 0.25f : 1.0f);
        g.h = scale;
        g.uv_rect = narrow(ch)
            ? make<float4>(cell.x + 0.4375f * cell_uv, cell.y + 0.25f * cell_uv, 0.125f * cell_uv, 0.5f * cell_uv)
            : make<float4>(cell.x + 0.25f * cell_uv, cell.y + 0.25f * cell_uv, 0.5f * cell_uv, 0.5f * cell_uv);
        g.tint = (ch == '-') ? wry::from_sRGB(make<float4>(0x5b / 255.0f, 0x5b / 255.0f, 1.0f, 1.0f))
                             : make<float4>(1.0f, 1.0f, 1.0f, 1.0f);
        result.push_back(g);
        cursor += advance;
    }
    return result;
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
            coordinate = _blank_coordinate;   // the caption is printed on below
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
        if (held.is_inty()) {
            // The number tile's caption, as the board prints it
            // (-numberGlyphs:), composited over the blank tile in linear
            // light: each glyph's atlas rect cut from the cutout atlas
            // and resampled bilinearly into its place on the frame (one
            // world unit, image y down, the decal's local +y up)
            const NSUInteger atlas = _glyphs.width;
            for (const WryNumberGlyph& g : [self numberGlyphs:held.as_int()]) {
                const NSUInteger x0 = (NSUInteger)std::lround(g.uv_rect.x * atlas);
                const NSUInteger y0 = (NSUInteger)std::lround(g.uv_rect.y * _glyphs.height);
                const NSUInteger sw = (NSUInteger)std::lround(g.uv_rect.z * atlas);
                const NSUInteger sh = (NSUInteger)std::lround(g.uv_rect.w * _glyphs.height);
                std::vector<unsigned char> source(sw * sh * 4);
                [_glyphs getBytes:source.data()
                      bytesPerRow:sw * 4
                       fromRegion:MTLRegionMake2D(x0, y0, sw, sh)
                      mipmapLevel:0];
                auto texel = [&](long sx, long sy, int k) -> float {
                    sx = std::clamp(sx, 0L, (long)sw - 1);
                    sy = std::clamp(sy, 0L, (long)sh - 1);
                    return source[((size_t)sy * sw + (size_t)sx) * 4 + (size_t)k] / 255.0f;
                };
                for (NSUInteger py = 0; py != cell; ++py) {
                    for (NSUInteger px = 0; px != cell; ++px) {
                        // The pixel centre in the glyph quad's local
                        // -0.5 .. 0.5 square (Shaders.metal decal uv)
                        const float u = ((float)px + 0.5f) / (float)cell - 0.5f;
                        const float v = 0.5f - ((float)py + 0.5f) / (float)cell;
                        const float lx = (u - g.x) / g.w;
                        const float ly = v / g.h;
                        if (std::abs(lx) > 0.5f || std::abs(ly) > 0.5f)
                            continue;
                        const float fx = (lx + 0.5f) * (float)sw - 0.5f;
                        const float fy = (0.5f - ly) * (float)sh - 0.5f;
                        const long ix = (long)std::floor(fx), iy = (long)std::floor(fy);
                        const float tx = fx - (float)ix, ty = fy - (float)iy;
                        float rgba[4];
                        for (int k = 0; k != 4; ++k) {
                            const float a = texel(ix, iy, k) * (1.0f - tx) + texel(ix + 1, iy, k) * tx;
                            const float b = texel(ix, iy + 1, k) * (1.0f - tx) + texel(ix + 1, iy + 1, k) * tx;
                            rgba[k] = a * (1.0f - ty) + b * ty;
                        }
                        const float alpha = rgba[3];
                        if (alpha <= 0.0f)
                            continue;
                        unsigned char* d = (unsigned char*)tile.data() + py * tile.stride_bytes() + px * 4;
                        for (int k = 0; k != 3; ++k) {
                            const float ink = wry::from_sRGB(rgba[k]) * g.tint[k];
                            const float under = wry::from_sRGB(d[k] / 255.0f);
                            d[k] = (unsigned char)std::lround(std::clamp(wry::to_sRGB(ink * alpha + under * (1.0f - alpha)), 0.0f, 1.0f) * 255.0f);
                        }
                    }
                }
            }
        }
        unsigned char* p[1];
        p[0] = (unsigned char*) tile.data();
        // The atlas is straight alpha with coloured margins, so say so,
        // or AppKit reads the margin as premultiplied colour
        NSBitmapImageRep* a = [[NSBitmapImageRep alloc]
                              initWithBitmapDataPlanes:p
                                            pixelsWide:(NSInteger) cell
                                            pixelsHigh:(NSInteger) cell
                                         bitsPerSample:8
                                       samplesPerPixel:4
                                              hasAlpha:YES
                                              isPlanar:NO
                                        colorSpaceName:NSCalibratedRGBColorSpace
                                          bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
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
    // The palette's cells are laid out in points by
    // PaletteOverlay::cells_rect, which the hit test also uses; draw them in
    // screen space with the GUI quad pipeline: a Panel-
    // style plate docked to the bottom edge (it extends off-screen, so only
    // its top corners show and its shadow reads as that of a plate running
    // on below the edge), and on it a rounded square per opcode, one gap
    // apart, each under its glyph from _symbols.  One instanced draw with
    // _symbols bound; the untextured quads ignore it.
    if (!_model->show_map()) {
        auto& palette = _model->_palette_overlay;
        auto& m = palette.controls()._payload;
        const float vw = _model->_gui.viewport_size.x;
        const float vh = _model->_gui.viewport_size.y;

        // PaletteOverlay's layout, which its hit test also uses.
        const float ppp = _model->_gui.pixels_per_point();
        auto px_rect = [&](float i0, float j0, float i1, float j1) {
            return palette.cells_rect(i0, j0, i1, j1, simd_make_float2(vw, vh), ppp);
        };

        using wry::gui::Button;
        using wry::gui::Painter;
        using wry::gui::Panel;
        const float gap = Panel::gap(_ctx.font);
        const float cell_radius = Button::default_corner_radius(_ctx.font);
        Painter scale;
        scale.pixels_per_point = ppp;
        const float shadow_sigma = scale.shadow_sigma();
        const float2 shadow_offset = scale.shadow_offset();
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
                                 + 3.0f * shadow_sigma
                                 + shadow_offset.y);
            push(Painter::blurred_round_rect_quad(
                plate + shadow_offset, radii(panel_radius),
                shadow_sigma, RGBA8Unorm_sRGB(0.0f, 0.0f, 0.0f, 1.0f)));
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
                g.flags = WryQuadFlagTextured | WryQuadFlagFiltered | WryQuadFlagStraight;
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
    if (!_model->show_map()) {
        using wry::WorldMap;

        // 256 pt, one point per map texel; 12 pt in from the corner
        const float ppp = _model->_gui.pixels_per_point();
        const float SIZE = 256.0f * ppp;
        const float MARGIN = 12.0f * ppp;
        float2 vp = _model->_gui.viewport_size;
        const float bx0 = vp.x - MARGIN - SIZE;   // box, screen px, y-down
        // below the money line (drawn with the GUI, below)
        const float by0 = MARGIN + [self moneyLineHeight] + MARGIN * 0.5f;

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
        painter.pixels_per_point = _model->_gui.pixels_per_point();
        painter.frame_count = (uint64_t)_frame_count;
        // Default clip: the full viewport.  Widgets push tighter clips
        // around their own contents via Painter::push_clip / pop_clip.
        painter.clip = wry::rect<float>{
            0.0f, 0.0f,
            _model->_gui.viewport_size.x, _model->_gui.viewport_size.y,
        };
        // The money (World::_money), right-justified above the minimap:
        // "$1234"
        if (!_model->show_map() && painter.font) {
            const float MARGIN = painter.pt(12.0f);
            char text[32];
            snprintf(text, sizeof(text), "$%lld",
                     (long long)_model->_world_to_render._ptr->_money);
            float width = 0.0f;
            for (const char* c = text; *c; ++c) {
                auto g = painter.font->charmap.find((char32_t)*c);
                if (g != painter.font->charmap.end())
                    width += g->second.advance;
            }
            const float right = _model->_gui.viewport_size.x - MARGIN;
            const float baseline = MARGIN + painter.font->ascender;
            painter.draw_text_run(simd_make_float2(right - width, baseline),
                                  wry::StringView(text),
                                  RGBA8Unorm_sRGB(1.0f, 1.0f, 1.0f, 1.0f));
        }
        // What the keys do to what is in hand, centred just above the
        // palette (WorldState::hand_hint)
        if (!_model->show_map() && painter.font && _model->is_holding()) {
            const std::string hint = _model->hand_hint();
            const float width = painter.marked_text_width(wry::StringView(hint.c_str()));
            const float top = _model->_palette_overlay.top_pixels();
            const float baseline = top - 6.0f * _model->_gui.pixels_per_point() + painter.font->descender;   // 6 pt clear of the palette
            painter.draw_marked_text(simd_make_float2((_model->_gui.viewport_size.x - width) * 0.5f, baseline),
                                     wry::StringView(hint.c_str()),
                                     RGBA8Unorm_sRGB(1.0f, 1.0f, 1.0f, 1.0f));
        }
        _model->_stack.paint(painter);
        _model->_gui.overlays.paint(painter);
    }

    _ctx.atlas->commit((__bridge void*)encoder,
                       (__bridge void*)_ctx.overlayQuadRenderPipelineState);
}

// The height of the money line above the minimap: one line of the GUI
// font
-(float)moneyLineHeight {
    return _ctx.font ? (_ctx.font->ascender - _ctx.font->descender)
                     : 16.0f * _model->_gui.pixels_per_point();
}

-(void)resetCursor {
    [_cursor set];
}

// WryScene: the world scene asks for no successor; the host replaces it with a
// main menu on `main-menu` (GuiContext::on_main_menu).
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

    const bool show_map = _model->show_map();

    // Another atlas asked for (the `atlas` command): swap it in before
    // anything samples the symbols this frame
    {
        const wry::String& asked = _model->_symbol_atlas;
        std::string name((const char*)asked.chars.data(), asked.chars.size());
        // Captions printed as decals (`caption-decals`) go on the tile
        // faces made blank for them, laid out the same
        // (make_tile_atlas.py --no-caption-text)
        if (_model->_caption_decals > 0 && name == "assets_tile")
            name = "assets_tile_nocaption";
        if (_model->_caption_decals > 0 && !_decalFontTexture)
            [self loadDecalFont];
        if (name != _symbolAtlasName && name != _symbolAtlasMissing) {
            std::error_code ec;
            if (!name.empty() && std::filesystem::exists(wry::path_for_resource(wry::StringView(name.c_str()), "png"), ec)) {
                [self loadSymbolAtlasNamed:[NSString stringWithUTF8String:name.c_str()]];
            } else {
                NSLog(@"symbol atlas: no %s.png in the assets folder; keeping %s", name.c_str(), _symbolAtlasName.c_str());
                _symbolAtlasMissing = name;
            }
        }
    }


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
    WryBufferSlice print_instances = {};
    NSUInteger print_count = 0;
    WryBufferSlice string_instances = {};
    NSUInteger string_count = 0;
    // The board: the cells in view as a dense map of terrain kinds, and
    // the look that inks them (deferred::terrain_fragment_function)
    WryBufferSlice terrain_kinds = {};
    WryTerrainUniforms board = {};
    // Map mode: the world-map quad, in place of all of the above.
    WryBufferSlice map_vertices = {};
    WryBufferSlice map_indices = {};
    NSUInteger map_index_count = 0;
    MeshInstanced map_instanced = mesh_instanced_things;
    _furnace_mesh.instanceCount = 0;
    _mine_mesh.instanceCount = 0;
    _truck_mesh.instanceCount = 0;
    _container_mesh.instanceCount = 0;
    _sandbox_mesh.instanceCount = 0;
    _ship_prow_mesh.instanceCount = 0;
    _ship_hold_mesh.instanceCount = 0;
    _ship_stern_mesh.instanceCount = 0;
    // raid model for data

    if (show_map) {

        // Map mode: a single quad covering the mapped world rect, textured
        // by the current map at one texel per tile, through the same lit
        // G-buffer path as the world -- none of the per-tile / per-entity
        // content is built or drawn.  Tile (x, y) spans x +/- 0.5, so the
        // rect runs [X0 - 0.5, X0 + EXTENT - 0.5) per axis and uv 0..1
        // lands texel centers on tile centers.
        //
        // The zoom (1/64 canonically; pinch varies it, below ZOOM_MAP) is
        // a model transform: scale the quad by the zoom about the camera
        // focus, leaving the camera itself at its default, so the view
        // direction and lighting are the world's own.
        map_instanced.model_transform = _model->map_model_transform(_model->_zoom);
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
        std::vector<WryDecalInstance> prints;   // image decals: the digits on number tiles
        std::vector<WryDecalInstance> strings;  // image decals from the decal font (_decalFontTexture)
        size_t symbol_hint = 2
            + (size_t)(grid_bounds.b.x - grid_bounds.a.x) * (size_t)(grid_bounds.b.y - grid_bounds.a.y)
            + ptrs.size();
        symbols.reserve(symbol_hint);
        decals.reserve(symbol_hint);

        // A string printed as decals from the decal font (decal_text.hpp)
        // on a surface: its baseline through `location` along `across`,
        // capitals cap_height world units tall up `up`, centred on
        // `location` or starting there, shrunk to max_width (<= 0: never);
        // ink of `colour` (linear rgb; a scales the coverage), raised
        // `emboss` world units at full size (negative presses it in; 0
        // prints it flat).  The boxes reach 0.04 units either side of the
        // surface along across x up, and print only on surfaces facing
        // within 60 degrees of it.  Nothing without the font
        // (-loadDecalFont, on with `caption-decals`).
        // Text is small and seen at a slant, where the footprint's
        // longer axis picks a mip that blurs the strokes across: sample
        // finer (measured at zoom 100: the captions' contrast)
        const float kStringLodBias = -0.75f;
        auto emit_string = [&](simd_float3 location, simd_float3 across, simd_float3 up,
                               std::string_view text, float cap_height, float max_width,
                               wry::DecalAlign align, simd_float4 colour, float emboss) {
            if (!_decalFontTexture)
                return;
            simd_float3 normal = simd_cross(across, up);
            wry::DecalTextRun run = wry::layout_decal_text(_decalFont, text, cap_height, max_width, align);
            for (const wry::DecalGlyphPlacement& g : run.glyphs) {
                simd_float4x4 model = simd_matrix(simd_make_float4(across * g.size.x, 0.0f),
                                                  simd_make_float4(up * g.size.y, 0.0f),
                                                  simd_make_float4(normal * 0.08f, 0.0f),
                                                  simd_make_float4(location + across * g.centre.x + up * g.centre.y, 1.0f));
                WryDecalInstance d = {};
                d.model_transform = model;
                d.inverse_model_transform = simd_inverse(model);
                d.uv_rect = g.uv_rect;
                d.tint = colour;
                d.material = make<float4>(0.5f, 0.0f, 0.0f, 0.0f);   // the ink keeps the surface's finish
                d.normal_cutoff = 0.5f;
                d.emboss = emboss * run.scale;
                // The bevel: about two texels of the face at its mip
                d.emboss_radius = 2.0f / (float)_decalFont.height;
                d.lod_bias = kStringLodBias;
                strings.push_back(d);
            }
        };
        // The caption of the atlas cell at `coordinate` (_cellCaption),
        // printed on the face of its tile as decals when `caption-decals`
        // is on, in the band make_tile_atlas.py leaves for it: the face
        // is `face`, the glyph frame (24 units to the world unit, y down)
        // centred on it, laid out by tile_caption_layout at
        // _caption_decals percent of the baked size
        const float caption_size = (float)_model->_caption_decals / 100.0f;
        auto emit_caption = [&](simd_float3 face, simd_float4 coordinate, simd_float3 across, simd_float3 up) {
            if (caption_size <= 0.0f || !_decalFontTexture)
                return;
            const int cx = (int)std::lround(coordinate.x * wry::SymbolSDF::kCells);
            const int cy = (int)std::lround(coordinate.y * wry::SymbolSDF::kCells);
            if (cx < 0 || cy < 0 || cx >= wry::SymbolSDF::kCells || cy >= wry::SymbolSDF::kCells)
                return;
            const size_t k = (size_t)cy * wry::SymbolSDF::kCells + (size_t)cx;
            const std::string& name = _cellCaption[k];
            if (name.empty())
                return;
            const float unit = 1.0f / wry::TileCaption::kFrame;
            const simd_float4 ink = _cellCaptionLight[k] ? make<float4>(1.0f, 1.0f, 1.0f, 1.0f)
                                                         : make<float4>(0.0f, 0.0f, 0.0f, 1.0f);
            wry::TileCaption caption = wry::tile_caption_layout(_decalFont, name);
            for (size_t n = 0; n != caption.lines.size(); ++n) {
                // A larger size keeps the last baseline and spreads the
                // lines above it in proportion
                const float baseline = caption.baselines.back()
                    + (caption.baselines[n] - caption.baselines.back()) * std::max(caption_size, 1.0f);
                const float baseline_up = 0.5f - baseline * unit;
                emit_string(face + up * baseline_up, across, up, caption.lines[n],
                            caption.cap * caption_size * unit, wry::TileCaption::kWidth * unit,
                            wry::DecalAlign::centre, ink, 0.0f);
            }
        };

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
        // A slab of the cell at `coordinate`, `stroke` texels thick with
        // fillets of `fillet`; `width` is the cell's stroke width, which
        // sizes the decal
        auto emit_slab = [&](simd_float4 location, simd_float4 coordinate,
                             simd_float3 across, simd_float3 up,
                             float stroke, float fillet, float width, bool decal, float half_xy) {
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
            s.half_extent = make<float4>(half_xy, half_xy, stroke * 0.5f, texels_per_cell * 0.5f);
            s.top_radius = fillet;
            s.bottom_radius = fillet;
            s.stroke_width = width;
            s.decal_reach = 0.0f;
            s.clear_coat = _model->_coat_tiles;
            if (decal && _model->_symbol_decals && normal.z > 0.99f) {
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
        // A symbol: as thick as its cell's stroke, filleted by a quarter of it
        auto emit_symbol = [&](simd_float4 location, simd_float4 coordinate,
                               simd_float3 across, simd_float3 up) {
            const int cx = std::clamp((int)std::lround(coordinate.x * wry::SymbolSDF::kCells), 0, wry::SymbolSDF::kCells - 1);
            const int cy = std::clamp((int)std::lround(coordinate.y * wry::SymbolSDF::kCells), 0, wry::SymbolSDF::kCells - 1);
            // Capped: a solid tile's stroke is the whole cell; 20 texels
            // keeps a ground symbol's underside (centred at z = 0.1, 12.8
            // texels up) above the ground, where its decal can land
            const float width = std::max(_symbolStrokeWidth[(size_t)cy * wry::SymbolSDF::kCells + (size_t)cx], 2.0f);
            const float stroke = std::min(width, 20.0f);
            emit_slab(location, coordinate, across, up, stroke, stroke * 0.25f, width, true, texels_per_unit * 0.5f);
            emit_caption(location.xyz + simd_cross(across, up) * (stroke * 0.5f / texels_per_unit), coordinate, across, up);
        };
        // A number: the blank tile with the value's caption (at most
        // four characters and a minus: "1234", "-12k", "1.2M";
        // number_caption.hpp) printed on its face as decals
        // (WryDecalInstance) from the cutout atlas's glyphs
        // (-numberGlyphs:)
        // The digits' relief: the glyph's coverage as a height over the
        // face (negative presses it in), world units at full size (it
        // shrinks with the row), and the bevel's half-width, glyph-atlas
        // texels (a cell is 256 across)
        const float kNumberEmboss = 0.012f;
        const float kNumberEmbossTexels = 3.0f;
        auto emit_number = [&](simd_float4 location, simd_float3 across, simd_float3 up, int64_t value) {
            emit_symbol(location, _blank_coordinate, across, up);
            simd_float3 normal = simd_cross(across, up);
            // The tile's face: emit_symbol's thickness cap, half of it out
            simd_float3 face = location.xyz + normal * (10.0f / texels_per_unit);
            for (const WryNumberGlyph& g : [self numberGlyphs:value]) {
                simd_float4x4 model = simd_matrix(simd_make_float4(across * g.w, 0.0f),
                                                  simd_make_float4(up * g.h, 0.0f),
                                                  simd_make_float4(normal * 0.08f, 0.0f),
                                                  simd_make_float4(face + across * g.x, 1.0f));
                WryDecalInstance d = {};
                d.model_transform = model;
                d.inverse_model_transform = simd_inverse(model);
                d.uv_rect = g.uv_rect;
                d.tint = g.tint;
                d.material = make<float4>(0.5f, 0.0f, 0.0f, 0.0f);   // the ink keeps the tile's finish
                d.normal_cutoff = 0.5f;
                d.emboss = kNumberEmboss * g.h;   // g.h is the row's shrink
                d.emboss_radius = kNumberEmbossTexels / (float)wry::SymbolSDF::kCells / 256.0f;
                prints.push_back(d);
            }
        };

        // How tall a term stands when drawn: a chip is emit_symbol's
        // slab (its thickness cap); a terrain matter is that slab plus
        // its pile; the shipping container is its mesh.  A machine's
        // stack piles its terms each on the one below, so the step up is
        // the term's own height.
        const float slab_thickness = 20.0f / texels_per_unit;
        // The pile: the sandbox is authored as a unit cube (Z-up, floor
        // centred on the origin); it is seated on the tile's face 0.6 of
        // a unit on a side, its centre 1/12 of a unit north of the
        // tile's, clear of the caption on the south edge (the frame's
        // panel above the caption band, make_tile_atlas.py)
        constexpr float pile_scale = 0.6f;
        constexpr float pile_height = pile_scale;   // the peak meets the lid
        constexpr float pile_north = 2.0f / 24.0f;
        auto has_tile = [&](wry::Term value) -> bool {
            if (!value.is_matter())
                return false;
            auto m = (size_t)value.as_matter();
            return (m < _matter_has_coordinate.size()) && _matter_has_coordinate[m];
        };
        auto term_height = [&](wry::Term value) -> float {
            if (has_tile(value))
                return slab_thickness + pile_height;
            if (value.is_matter())
                return 0.5f;   // the container
            return slab_thickness;
        };
        // A matter tile and its pile, the tile centred at `location`
        // (lying flat, world-aligned like every symbol)
        // A pile alone, standing on `base` (its floor's centre)
        auto emit_pile = [&](simd_float4 base, wry::Term value) {
            const size_t m = (size_t)value.as_matter();
            auto A = simd_matrix_translate(base) * simd_matrix_scale(pile_scale);
            MeshInstanced pile;
            pile.model_transform = A;
            pile.inverse_transpose_model_transform = inverse(transpose(A));
            const uint8_t* c = m < std::size(wry::MATTER_COLOR_SRGB)
                ? wry::MATTER_COLOR_SRGB[m] : wry::MATTER_COLOR_SRGB[0];
            pile.albedo = make<float4>(wry::from_sRGB(c[0] / 255.0f),
                                       wry::from_sRGB(c[1] / 255.0f),
                                       wry::from_sRGB(c[2] / 255.0f), 1.0f);
            [_sandbox_mesh addInstance:pile];
        };
        auto emit_matter_tile = [&](simd_float4 location, wry::Term value) {
            const size_t m = (size_t)value.as_matter();
            emit_symbol(location, _matter_coordinate[m],
                        make<float3>(1.0f, 0.0f, 0.0f), make<float3>(0.0f, 1.0f, 0.0f));
            simd_float4 base = location;
            base.y += pile_north;
            base.z += slab_thickness * 0.5f;
            emit_pile(base, value);
        };
        // The holds of the Ships in view: what is put in a hold is drawn
        // as its pile alone, standing on the hold's floor (ship_gen.py
        // HOLD_FLOOR), peeking over the deck
        constexpr float ship_hold_floor = 0.02f;
        std::vector<wry::Coordinate> ship_holds;
        // The shipping container: its mesh standing at `base` (its
        // underside), heading-aligned by `yaw`
        auto emit_container = [&](simd_float4 base, float yaw) {
            auto A = simd_matrix_translate(base);
            A = A * simd_matrix_rotate(yaw, make<float3>(0.0, 0.0, -1.0));
            A = A * simd_matrix_scale(container_mesh_scale);
            MeshInstanced m;
            m.model_transform = A;
            m.inverse_transpose_model_transform = inverse(transpose(A));
            m.albedo = make<float4>(1.0, 1.0, 1.0, 1.0);
            [_container_mesh addInstance:m];
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
                                
                // While digging the machine is at rest on its cell (the
                // dig's times bracket the dig, not a move), shaking
                const bool digging = p->_phase == wry::Machine::PHASE_MINING;
                float t = 1.0;
                if (!digging && (tnow < p->_new_time)) {
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
                // the camera follows this one: next frame centres on it
                if (p->_entity_id == _model->_follow)
                    _model->_follow_drawn_at = x1;
                if (digging) {
                    // a new small random offset every frame; the carried
                    // stack shakes with the token
                    std::uniform_real_distribution<float> jiggle(-0.035f, 0.035f);
                    location.x += jiggle(_shake);
                    location.y += jiggle(_shake);
                }
                
                
                auto A = simd_matrix_translate(location);
                
                A = A * simd_matrix_rotate(atan2(dx1.x, dx1.y), make<float3>(0.0, 0.0, -1.0));
                
                // The machine is a playing piece: the pentagon token of
                // atlas row 15 (tools/make_assets.py g_token_truck), one
                // colour through, as tall as it is wide, with its apex to
                // the heading.  It hovers: its underside clears a tile on
                // the board by a hair, so the tiles it drives over slide
                // under it rather than through it (its shadow shows the
                // gap).  The truck mesh (_truck_mesh) is no longer
                // instanced.
                const float token_lift = slab_thickness + 0.015f;
                {
                    const float thickness = _model->_token_height * texels_per_unit;
                    simd_float2 heading = simd_length(dx1) > 1e-4f ? simd_normalize(dx1) : make<float2>(0.0f, 1.0f);
                    simd_float3 up = make<float3>(heading.x, heading.y, 0.0f);
                    simd_float3 across = make<float3>(heading.y, -heading.x, 0.0f);
                    simd_float4 centre = location;
                    centre.z = token_lift + _model->_token_height * 0.5f;
                    // No occlusion decal: that models a thin slab's
                    // underside, and a standing piece has none to show
                    // The cuboid reaches past the frame for the apex,
                    // which overhangs it by 0.12 units
                    emit_slab(centre, _token_truck_coordinate, across, up, thickness, _model->_token_fillet * texels_per_unit, thickness, false, texels_per_unit * 0.6f);
                    symbols.back().clear_coat = _model->_coat_token;
                }
                
                // The carried stack, piled on the token: each item sits
                // on the one below, a hair apart, and the step up is the
                // item's own height (term_height: a chip, a matter tile
                // and its pile, a container)
                const float stack_gap = 0.004f;
                float base = token_lift + _model->_token_height;
                const float yaw = atan2(dx1.x, dx1.y);
                for (int i = (int) wry::PersistentStack<wry::Term>::size(p->_stack); i--;) {
                    wry::Term value = wry::PersistentStack<wry::Term>::at(p->_stack, i);
                    const float item_height = term_height(value);
                    simd_float4 under = location;     // the item's underside
                    under.z = base;
                    simd_float4 centre = location;    // a chip's or tile's centre
                    centre.z = base + slab_thickness * 0.5f;
                    base += item_height + stack_gap;
                    if (has_tile(value)) {
                        emit_matter_tile(centre, value);
                    } else if (value.is_matter()) {
                        // held matter rides as a mesh at this stack slot,
                        // heading-aligned with the machine
                        emit_container(under, yaw);
                    } else if (value.is_opcode()) {
                        emit_symbol(centre, _opcode_to_coordinate[value.as_opcode()],
                                    make<float3>(1.0f, 0.0f, 0.0f), make<float3>(0.0f, 1.0f, 0.0f));
                    } else if (wry::term_is_boolean(value)) {
                        emit_symbol(centre, wry::term_as_boolean(value) ? _true_coordinate : _false_coordinate,
                                    make<float3>(1.0f, 0.0f, 0.0f), make<float3>(0.0f, 1.0f, 0.0f));
                    } else if (value.is_inty()) {
                        emit_number(centre, make<float3>(1.0f, 0.0f, 0.0f), make<float3>(0.0f, 1.0f, 0.0f), value.as_int());
                    } else {
                        // not renderable yet (heap integer, string, ...):
                        // the same neutral dot the ground uses, rather than
                        // a garbage hex digit cut from the bit pattern
                        emit_symbol(centre, _cdot_coordinate,
                                    make<float3>(1.0f, 0.0f, 0.0f), make<float3>(0.0f, 1.0f, 0.0f));
                    }
                }
                
            } else if (auto p = dynamic_cast<const wry::Ship*>(q)) {

                // The kit, module by module from the prow aft, each in
                // its cell and turned to the ship's heading (the modules
                // are authored prow +x, i.e. heading east)
                const float yaw = (float)((p->_heading & 3) - wry::HEADING_EAST) * (float)M_PI_2;
                for (int k = 0; k != wry::Ship::SHIP_LENGTH; ++k) {
                    wry::Coordinate c = p->cell(k);
                    simd_float4 location = make<float4>(_model->render_position(c), 0.0f, 1.0f);
                    auto A = simd_matrix_translate(location)
                        * simd_matrix_rotate(yaw, make<float3>(0.0, 0.0, -1.0));
                    MeshInstanced m;
                    m.model_transform = A;
                    m.inverse_transpose_model_transform = inverse(transpose(A));
                    m.albedo = make<float4>(1.0, 1.0, 1.0, 1.0);
                    WryMesh* mesh = (k == 0) ? _ship_prow_mesh
                        : (k == wry::Ship::SHIP_LENGTH - 1) ? _ship_stern_mesh
                        : _ship_hold_mesh;
                    [mesh addInstance:m];
                    if ((k != 0) && (k != wry::Ship::SHIP_LENGTH - 1))
                        ship_holds.push_back(c);
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
                    
                    // Upright on the source's front face: image right is
                    // world x, image up is world z
                    const simd_float4 front = location + make<float4>(0.0f, -0.1f, 0.0f, 0.0f);
                    if (wry::term_is_boolean(r->_of_this)) {
                        emit_symbol(front, wry::term_as_boolean(r->_of_this) ? _true_coordinate : _false_coordinate,
                                    make<float3>(1.0f, 0.0f, 0.0f), make<float3>(0.0f, 0.0f, 1.0f));
                    } else {
                        emit_number(front, make<float3>(1.0f, 0.0f, 0.0f), make<float3>(0.0f, 0.0f, 1.0f),
                                    r->_of_this.is_inty() ? r->_of_this.as_int() : 0);
                    }

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
                    if (has_tile(q)
                        && std::find(ship_holds.begin(), ship_holds.end(), tile_at(i, j)) != ship_holds.end()) {
                        emit_pile(make<float4>(i, j, ship_hold_floor, 1.0f), q);
                        continue;
                    }
                    if (wry::term_is_boolean(q)) {
                        coordinate = wry::term_as_boolean(q)
                            ? _true_coordinate : _false_coordinate;
                    } else if (q.is_inty()) {
                        emit_number(location, make<float3>(1.0f, 0.0f, 0.0f), make<float3>(0.0f, 1.0f, 0.0f), q.as_int());
                        continue;
                    } else if (q.is_opcode()) {
                        //printf("q is opcode\n");
                        auto p = _opcode_to_coordinate.find(q.as_opcode());
                        if (p != _opcode_to_coordinate.end()) {
                            coordinate = p->second;
                        } else {
                            printf("q is opcode but was not found, %d\n", q.as_opcode());
                        }
                    } else if (has_tile(q)) {
                        // a terrain matter: its tile and pile
                        emit_matter_tile(location, q);
                        continue;
                    } else if (q.is_matter()) {
                        // the container renders as a mesh sitting on an
                        // ordinary empty tile
                        coordinate = _cdot_coordinate;
                        emit_container(location, 0.0f);
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

            // The board: the terrain kinds of the cells in view, as a
            // dense byte map over grid_bounds (clamped about its centre
            // to a sane size), sourced by Morton-range descent over the
            // terrain map like the entity query above; cells that have
            // no terrain yet (beyond the generated region) are marked
            // absent and the fragment shader discards them, showing the
            // clear colour.
            {
                constexpr int kMaxCells = 512;
                simd_int2 a = grid_bounds.a, b = grid_bounds.b;
                for (int axis = 0; axis != 2; ++axis) {
                    if (b[axis] - a[axis] > kMaxCells) {
                        int c = (a[axis] + b[axis]) / 2;
                        a[axis] = c - kMaxCells / 2;
                        b[axis] = a[axis] + kMaxCells;
                    }
                }
                board.origin = a;
                board.size = b - a;
                board.focus = simd_make_int2(focus_tile.x, focus_tile.y);
                std::vector<uint8_t> map((size_t)board.size.x * (size_t)board.size.y,
                                         (uint8_t)WRY_TERRAIN_KIND_NONE);
                visit_in_region(new_world->_terrain_for_coordinate,
                                tile_at(a.x, a.y),
                                tile_at(b.x - 1, b.y - 1),
                                [&](Coordinate xy, wry::Terrain t) {
                    if ((t < 0) || (t >= wry::TERRAIN_KIND_COUNT))
                        t = wry::TERRAIN_ROCK;   // an unknown code: plain ground
                    float2 r = _model->render_position(xy);
                    int i = (int)std::lround(r.x) - a.x;
                    int j = (int)std::lround(r.y) - a.y;
                    if ((i < 0) || (i >= board.size.x) || (j < 0) || (j >= board.size.y))
                        return;
                    map[(size_t)j * (size_t)board.size.x + (size_t)i] = (uint8_t)t;
                });
                terrain_kinds = [_ctx.arena copyBytes:map.data() length:map.size()];

                // The look: a pastel per kind, near-black ink, and the
                // lattice's opacity and reach per kind, with the
                // dimensions from the `board` command
                auto linear = [](float r, float g, float b) {
                    return make<float4>(wry::from_sRGB(r / 255.0f), wry::from_sRGB(g / 255.0f), wry::from_sRGB(b / 255.0f), 1.0f);
                };
                const float tint = _model->_board_tint;
                auto pastel = [&](float r, float g, float b) {
                    simd_float4 c = linear(r, g, b);
                    return simd_mix(make<float4>(1.0f, 1.0f, 1.0f, 1.0f), c, make<float4>(tint, tint, tint, 0.0f));
                };
                // The pastels are terrain.hpp's TERRAIN_COLOR_SRGB, so
                // the world map (world_map.cpp) is inked the same
                for (int k = 0; k != wry::TERRAIN_KIND_COUNT; ++k) {
                    const uint8_t* c = wry::TERRAIN_COLOR_SRGB[k];
                    board.tint[k] = pastel(c[0], c[1], c[2]);
                }
                // x: line opacity, y: arm (fraction of an edge drawn from
                // each corner), z: mark opacity, w: share of cells marked
                const float la = _model->_board_line_alpha, ma = _model->_board_mark_alpha;
                board.lattice[wry::TERRAIN_WATER] = make<float4>(0.45f * la, 1.0f, ma, 0.55f);
                board.lattice[wry::TERRAIN_SAND]  = make<float4>(0.7f * la, 1.0f, ma, 0.4f);
                board.lattice[wry::TERRAIN_GRASS] = make<float4>(la, 1.0f, ma, 0.0f);
                board.lattice[wry::TERRAIN_ROCK]  = make<float4>(la, 1.0f, ma, 0.5f);
                // the ores are prospects: most cells marked, so a patch
                // reads by its marks as well as its tint
                board.lattice[wry::TERRAIN_COAL]      = make<float4>(la, 1.0f, ma, 0.8f);
                board.lattice[wry::TERRAIN_IRON_ORE]  = make<float4>(la, 1.0f, ma, 0.8f);
                board.lattice[wry::TERRAIN_LIMESTONE] = make<float4>(la, 1.0f, ma, 0.7f);
                board.line_color = linear(38, 38, 44);
                board.misregister = simd_make_float2(_model->_board_misregister_x, _model->_board_misregister_y);
                board.tint_blur = _model->_board_tint_blur;
                board.line_width = _model->_board_line_width;
                board.line_min_pixels = _model->_board_line_min_pixels;
                board.mark_bias = _model->_board_mark_bias;
                board.roughness = 0.9f;
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
        print_count = prints.size();
        if (print_count)
            print_instances = [_ctx.arena copyBytes:prints.data()
                                             length:prints.size() * sizeof(WryDecalInstance)];
        string_count = strings.size();
        if (string_count)
            string_instances = [_ctx.arena copyBytes:strings.data()
                                              length:strings.size() * sizeof(WryDecalInstance)];

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
        [_sandbox_mesh drawWithRenderCommandEncoder:render_command_encoder arena:_ctx.arena];
        [_ship_prow_mesh drawWithRenderCommandEncoder:render_command_encoder arena:_ctx.arena];
        [_ship_hold_mesh drawWithRenderCommandEncoder:render_command_encoder arena:_ctx.arena];
        [_ship_stern_mesh drawWithRenderCommandEncoder:render_command_encoder arena:_ctx.arena];
        
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

            // Normal and depth are stored: the screen-space occlusion
            // pass reads them between the two passes
            descriptor.colorAttachments[AAPLColorIndexNormal].clearColor = MTLClearColorMake(0, 0, 0, 0);
            descriptor.colorAttachments[AAPLColorIndexNormal].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexNormal].storeAction = MTLStoreActionStore;
            descriptor.colorAttachments[AAPLColorIndexNormal].texture = _deferredNormalColorAttachmentTexture;

            // clear to (occlusion 1, roughness 1, metallic 0, no clear
            // coat), matching the background surface properties the old
            // layout cleared to
            descriptor.colorAttachments[AAPLColorIndexMaterial].clearColor = MTLClearColorMake(1, 1, 0, 0);
            descriptor.colorAttachments[AAPLColorIndexMaterial].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexMaterial].storeAction = MTLStoreActionDontCare;
            descriptor.colorAttachments[AAPLColorIndexMaterial].texture = _deferredMaterialColorAttachmentTexture;

            // linear view depth; 0 is the sky
            descriptor.colorAttachments[AAPLColorIndexDepth].clearColor = MTLClearColorMake(0, 0, 0, 0);
            descriptor.colorAttachments[AAPLColorIndexDepth].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexDepth].storeAction = MTLStoreActionStore;
            descriptor.colorAttachments[AAPLColorIndexDepth].texture = _deferredDepthColorAttachmentTexture;

            descriptor.colorAttachments[AAPLColorIndexAmbient].clearColor = MTLClearColorMake(0, 0, 0, 1);
            descriptor.colorAttachments[AAPLColorIndexAmbient].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexAmbient].storeAction = MTLStoreActionStore;
            descriptor.colorAttachments[AAPLColorIndexAmbient].texture = _deferredAmbientColorAttachmentTexture;
            
            descriptor.depthAttachment.clearDepth = 1.0;
            descriptor.depthAttachment.loadAction = MTLLoadActionClear;
            descriptor.depthAttachment.storeAction = MTLStoreActionDontCare;
            descriptor.depthAttachment.texture = _deferredDepthAttachmentTexture;
            
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
                // `environment NAME` swaps the map: prefilter the new one
                // (a few milliseconds of GPU, once)
                if (_model->_environment != _loaded_environment) {
                    NSString* name = [NSString stringWithUTF8String:_model->_environment.c_str()];
                    if (id<MTLTexture> t = [self prefilteredEnvironmentMapFromResource:name ofType:@"png"])
                        _deferredLightImageBasedTexture = t;
                    // and the diffuse term's irradiance cube, or the scene
                    // keeps its ambient from the old sky
                    if (id<MTLTexture> t = [self irradianceMapFromResource:name ofType:@"png"])
                        _deferredLightIrradianceTexture = t;
                    _loaded_environment = _model->_environment;
                }
                uniforms.ibl_scale = _model->_exposure;
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
                    // deferred pipeline as the world it stands in for,
                    // its texels magnified as flat cells.
                    [encoder setRenderPipelineState:_mapGBufferRenderPipelineState];
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
                    [encoder setRenderPipelineState:_deferredGBufferRenderPipelineState];

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
                        [encoder setFragmentTexture:_symbols atIndex:AAPLTextureIndexAlbedo];
                        // [encoder setFragmentTexture:_white atIndex:AAPLTextureIndexAlbedo];
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

                    // The board (a shadow receiver only, so it skips the
                    // shadow pass entirely): one quad over the kind map,
                    // inked per fragment from the symbol field's terrain
                    // row.  Restore the mesh bindings after.
                    if (terrain_kinds.buffer) {
                        [encoder setRenderPipelineState:_terrainGBufferRenderPipelineState];
                        [encoder setVertexBytes:&board length:sizeof(WryTerrainUniforms) atIndex:AAPLBufferIndexInstanced];
                        [encoder setFragmentBytes:&board length:sizeof(WryTerrainUniforms) atIndex:AAPLBufferIndexInstanced];
                        [encoder setFragmentBuffer:terrain_kinds.buffer offset:terrain_kinds.offset atIndex:AAPLBufferIndexVertices];
                        [encoder setFragmentTexture:_symbolField atIndex:AAPLTextureIndexNormal];
                        // A ground quad seen from above only; culling buys nothing
                        [encoder setCullMode:MTLCullModeNone];
                        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                    vertexStart:0
                                    vertexCount:6];
                        [encoder setCullMode:MTLCullModeBack];
                        [encoder setRenderPipelineState:_deferredGBufferRenderPipelineState];
                        [encoder setVertexBytes:&mesh_instanced_things
                                         length:sizeof(MeshInstanced)
                                        atIndex:AAPLBufferIndexInstanced];
                        [encoder setFragmentTexture:_blue atIndex:AAPLTextureIndexNormal];
                    }

                }

                [_furnace_mesh drawWithRenderCommandEncoder:encoder arena:_ctx.arena];
                [_mine_mesh drawWithRenderCommandEncoder:encoder arena:_ctx.arena];
                [_truck_mesh drawWithRenderCommandEncoder:encoder arena:_ctx.arena];
                [_container_mesh drawWithRenderCommandEncoder:encoder arena:_ctx.arena];
                [_sandbox_mesh drawWithRenderCommandEncoder:encoder arena:_ctx.arena];
                [_ship_prow_mesh drawWithRenderCommandEncoder:encoder arena:_ctx.arena];
                [_ship_hold_mesh drawWithRenderCommandEncoder:encoder arena:_ctx.arena];
                [_ship_stern_mesh drawWithRenderCommandEncoder:encoder arena:_ctx.arena];

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

                // Image decals: the digits printed on the number tiles,
                // onto whatever the G-buffer holds inside their boxes
                if (print_count) {
                    [encoder setRenderPipelineState:_decalRenderPipelineState];
                    [encoder setDepthStencilState:_ctx.disabledDepthStencilState];
                    [encoder setVertexBuffer:print_instances.buffer
                                      offset:print_instances.offset
                                     atIndex:AAPLBufferIndexInstanced];
                    [encoder setFragmentBuffer:print_instances.buffer
                                        offset:print_instances.offset
                                       atIndex:AAPLBufferIndexInstanced];
                    [encoder setFragmentTexture:_glyphs atIndex:AAPLTextureIndexAlbedo];
                    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                vertexStart:0
                                vertexCount:36
                              instanceCount:print_count];
                    [encoder setDepthStencilState:_ctx.enabledDepthStencilState];
                    [encoder setFragmentTexture:_symbols atIndex:AAPLTextureIndexAlbedo];
                }

                // String decals: the tile captions (`caption-decals`),
                // the same pipeline over the decal font's texture
                if (string_count) {
                    [encoder setRenderPipelineState:_decalRenderPipelineState];
                    [encoder setDepthStencilState:_ctx.disabledDepthStencilState];
                    [encoder setVertexBuffer:string_instances.buffer
                                      offset:string_instances.offset
                                     atIndex:AAPLBufferIndexInstanced];
                    [encoder setFragmentBuffer:string_instances.buffer
                                        offset:string_instances.offset
                                       atIndex:AAPLBufferIndexInstanced];
                    [encoder setFragmentTexture:_decalFontTexture atIndex:AAPLTextureIndexAlbedo];
                    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                vertexStart:0
                                vertexCount:36
                              instanceCount:string_count];
                    [encoder setDepthStencilState:_ctx.enabledDepthStencilState];
                    [encoder setFragmentTexture:_symbols atIndex:AAPLTextureIndexAlbedo];
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
                // A warm sun against the blue sky, so a white board lands
                // near white rather than blue
                uniforms.radiance = simd_make_float3(1.08f, 1.0f, 0.84f) * _model->_exposure;
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
        
        
        [encoder endEncoding];

        // Screen-space ambient occlusion over the stored depth and
        // normal, into _ssaoBlurredTexture
        WrySSAOUniforms ssao = {};
        ssao.camera_position_world = uniforms.camera_position_world;
        ssao.camera_ray_centre = uniforms.camera_ray_centre;
        ssao.camera_ray_dx = uniforms.camera_ray_dx;
        ssao.camera_ray_dy = uniforms.camera_ray_dy;
        ssao.viewport = simd_make_float2((float)_deferredDepthColorAttachmentTexture.width,
                                         (float)_deferredDepthColorAttachmentTexture.height);
        // One NDC unit across spans half the viewport; the ray basis
        // vector across is perpendicular to the view axis, so at unit
        // depth a world unit across is this many pixels
        ssao.pixels_per_unit = 0.5f * ssao.viewport.x / simd_length(uniforms.camera_ray_dx.xyz);
        ssao.radius = _model->_ssao_radius;
        ssao.strength = _model->_ssao_strength;
        ssao.mode = _model->_ssao_mode;
        ssao.show = _model->_ssao_show;
        ssao.slices = _model->_ssao_slices;
        ssao.steps = _model->_ssao_steps;
        ssao.pyramid = _model->_ssao_pyramid;
        ssao.blur = _model->_ssao_blur;
        ssao.blur_radius = _model->_ssao_blur_radius;
        // The texture the composite reads: the blurred one, unless the
        // separable blur's second pass wrote back into the first
        id<MTLTexture> aoFinal = _ssaoBlurredTexture;
        for (int repeat = 0; ssao.mode != WRY_SSAO_OFF && repeat != _model->_ssao_repeat; ++repeat) {
            id<MTLComputeCommandEncoder> compute = [command_buffer computeCommandEncoder];
            compute.label = @"Screen-space occlusion";
            MTLSize grid = MTLSizeMake(_ssaoTexture.width, _ssaoTexture.height, 1);
            MTLSize group = MTLSizeMake(16, 16, 1);
            if (ssao.mode == WRY_SSAO_GTAO && ssao.pyramid) {
                // The depth pyramid first: whole 32 x 32 tiles, the
                // kernel bound-checks the edges
                [compute setComputePipelineState:_ssaoDepthPyramidPipelineState];
                [compute setTexture:_deferredDepthColorAttachmentTexture atIndex:0];
                for (int i = 0; i != WRY_SSAO_DEPTH_PYRAMID_LEVELS; ++i)
                    [compute setTexture:_depthPyramidLevels[i] atIndex:1 + i];
                [compute setBytes:&ssao length:sizeof(ssao) atIndex:0];
                MTLSize tiles = MTLSizeMake((_depthPyramid.width + 31) / 32, (_depthPyramid.height + 31) / 32, 1);
                [compute dispatchThreadgroups:tiles threadsPerThreadgroup:group];
            }
            [compute setComputePipelineState:ssao.mode == WRY_SSAO_LUFT ? _ssaoLuftPipelineState : _ssaoGTAOPipelineState];
            [compute setTexture:ssao.mode == WRY_SSAO_LUFT ? _deferredDepthColorAttachmentTexture : _depthPyramid atIndex:0];
            [compute setTexture:_deferredDepthColorAttachmentTexture atIndex:3];
            [compute setTexture:_deferredNormalColorAttachmentTexture atIndex:1];
            [compute setTexture:_ssaoTexture atIndex:2];
            [compute setBytes:&ssao length:sizeof(ssao) atIndex:0];
            [compute dispatchThreads:grid threadsPerThreadgroup:group];
            switch (ssao.blur) {
                case WRY_SSAO_BLUR_SEPARABLE: {
                    // Across into the blurred texture, then down back into
                    // the occlusion texture
                    [compute setComputePipelineState:_ssaoBlurAxisPipelineState];
                    simd_int2 axis = simd_make_int2(1, 0);
                    [compute setTexture:_ssaoTexture atIndex:0];
                    [compute setTexture:_ssaoBlurredTexture atIndex:1];
                    [compute setBytes:&axis length:sizeof(axis) atIndex:1];
                    [compute dispatchThreads:grid threadsPerThreadgroup:group];
                    axis = simd_make_int2(0, 1);
                    [compute setTexture:_ssaoBlurredTexture atIndex:0];
                    [compute setTexture:_ssaoTexture atIndex:1];
                    [compute setBytes:&axis length:sizeof(axis) atIndex:1];
                    [compute dispatchThreads:grid threadsPerThreadgroup:group];
                    aoFinal = _ssaoTexture;
                    break;
                }
                case WRY_SSAO_BLUR_TILED: {
                    // Whole threadgroups: the kernel stages a tile per group
                    [compute setComputePipelineState:_ssaoBlurTiledPipelineState];
                    [compute setTexture:_ssaoTexture atIndex:0];
                    [compute setTexture:_ssaoBlurredTexture atIndex:1];
                    MTLSize groups = MTLSizeMake((grid.width + 15) / 16, (grid.height + 15) / 16, 1);
                    [compute dispatchThreadgroups:groups threadsPerThreadgroup:group];
                    aoFinal = _ssaoBlurredTexture;
                    break;
                }
                default: {
                    [compute setComputePipelineState:_ssaoBlurPipelineState];
                    [compute setTexture:_ssaoTexture atIndex:0];
                    [compute setTexture:_ssaoBlurredTexture atIndex:1];
                    [compute dispatchThreads:grid threadsPerThreadgroup:group];
                    aoFinal = _ssaoBlurredTexture;
                    break;
                }
            }
            [compute endEncoding];
        }

        // The second pass, over the two light attachments only (plus the
        // depth buffer, untouched): the composite adds the occluded
        // ambient to the direct light, reading the stored depth G-buffer
        // as a texture for its upsample, then the GUI goes on top.
        descriptor.colorAttachments[AAPLColorIndexColor].loadAction = MTLLoadActionLoad;
        descriptor.colorAttachments[AAPLColorIndexAmbient].loadAction = MTLLoadActionLoad;
        descriptor.colorAttachments[AAPLColorIndexAmbient].storeAction = MTLStoreActionDontCare;
        descriptor.colorAttachments[AAPLColorIndexAlbedo].texture = nil;
        descriptor.colorAttachments[AAPLColorIndexNormal].texture = nil;
        descriptor.colorAttachments[AAPLColorIndexMaterial].texture = nil;
        descriptor.colorAttachments[AAPLColorIndexDepth].texture = nil;
        descriptor.depthAttachment.loadAction = MTLLoadActionDontCare;
        encoder = [command_buffer renderCommandEncoderWithDescriptor:descriptor];
        encoder.label = @"Composite and GUI";
        [encoder setRenderPipelineState:_compositeRenderPipelineState];
        [encoder setDepthStencilState:_ctx.disabledDepthStencilState];
        [encoder setCullMode:MTLCullModeNone];
        [encoder setVertexBuffer:_ctx.screenTriangleStripVertexBuffer
                          offset:0
                         atIndex:AAPLBufferIndexVertices];
        [encoder setFragmentBytes:&ssao length:sizeof(ssao) atIndex:AAPLBufferIndexUniforms];
        [encoder setFragmentTexture:aoFinal atIndex:AAPLTextureIndexColor];
        [encoder setFragmentTexture:_deferredDepthColorAttachmentTexture atIndex:AAPLTextureIndexNormal];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                    vertexStart:0
                    vertexCount:4];

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
        
    if (_model->_show_bloom) {
        // Screen-space bloom by simple Gaussian blur (toggle-bloom; off by
        // default, and then not computed at all).  NB: it runs after the
        // composite-and-GUI pass, so it blurs the HUD into itself too
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
    // With bloom on, present the light plus 1/8 of its blur
    return _model->_show_bloom ? _addedTexture : _deferredLightColorAttachmentTexture;

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

    // Normal and depth are stored for the screen-space occlusion pass
    descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
    descriptor.storageMode = MTLStorageModePrivate;
    _deferredNormalColorAttachmentTexture = [_ctx.device newTextureWithDescriptor:descriptor];
    _deferredNormalColorAttachmentTexture.label = @"Normal G-buffer";

    descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
    descriptor.storageMode = MTLStorageModeMemoryless;
    _deferredMaterialColorAttachmentTexture = [_ctx.device newTextureWithDescriptor:descriptor];
    _deferredMaterialColorAttachmentTexture.label = @"Material G-buffer";
    
    descriptor.pixelFormat = MTLPixelFormatR32Float;
    descriptor.storageMode = MTLStorageModePrivate;
    _deferredDepthColorAttachmentTexture = [_ctx.device newTextureWithDescriptor:descriptor];
    _deferredDepthColorAttachmentTexture.label = @"Depth G-buffer";

    descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
    _deferredAmbientColorAttachmentTexture = [_ctx.device newTextureWithDescriptor:descriptor];
    _deferredAmbientColorAttachmentTexture.label = @"Ambient light G-buffer";

    // The occlusion runs at half resolution: (visibility, linear depth)
    // per 2 x 2 block, the depth in full precision for the composite's
    // bilateral upsample
    descriptor.pixelFormat = MTLPixelFormatRG32Float;
    descriptor.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
    descriptor.width = (drawableSize.width + 1) / 2;
    descriptor.height = (drawableSize.height + 1) / 2;
    _ssaoTexture = [_ctx.device newTextureWithDescriptor:descriptor];
    _ssaoTexture.label = @"Screen-space occlusion (half resolution)";
    _ssaoBlurredTexture = [_ctx.device newTextureWithDescriptor:descriptor];
    _ssaoBlurredTexture.label = @"Screen-space occlusion, blurred (half resolution)";

    // The GTAO taps' depth pyramid: full resolution, built from the
    // Depth G-buffer each frame.  R32Float: as R16Float the ground plane
    // quantizes into terraces whose steps the taps read as horizons
    // (horizontal banding at native resolution)
    descriptor.pixelFormat = MTLPixelFormatR32Float;
    descriptor.width = drawableSize.width;
    descriptor.height = drawableSize.height;
    descriptor.mipmapLevelCount = WRY_SSAO_DEPTH_PYRAMID_LEVELS;
    _depthPyramid = [_ctx.device newTextureWithDescriptor:descriptor];
    _depthPyramid.label = @"Depth pyramid";
    for (int i = 0; i != WRY_SSAO_DEPTH_PYRAMID_LEVELS; ++i) {
        _depthPyramidLevels[i] = [_depthPyramid newTextureViewWithPixelFormat:MTLPixelFormatR32Float
                                                                   textureType:MTLTextureType2D
                                                                        levels:NSMakeRange(i, 1)
                                                                        slices:NSMakeRange(0, 1)];
        _depthPyramidLevels[i].label = [NSString stringWithFormat:@"Depth pyramid level %d", i];
    }
    descriptor.mipmapLevelCount = 1;
    descriptor.width = drawableSize.width;
    descriptor.height = drawableSize.height;
    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeMemoryless;
    
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
