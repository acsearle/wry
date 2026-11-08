//
//  ShaderTypes.h
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#ifndef ShaderTypes_h
#define ShaderTypes_h

#include <simd/simd.h>

// [[attribute(index)]] for vertex shader structured input

enum AAPLAttributeIndex {
    AAPLAttributeIndexPosition,
    AAPLAttributeIndexCoordinate,
    AAPLAttributeIndexNormal,
    AAPLAttributeIndexTangent,
    AAPLAttributeIndexBinormal,
    AAPLAttributeIndexColor,
};

// [[buffer(index)]]

enum AAPLBufferIndex {
    AAPLBufferIndexUniforms,
    AAPLBufferIndexVertices,
    AAPLBufferIndexIndices,
    AAPLBufferIndexInstanced,
};

// [[color(index)]] attachment

enum AAPLColorIndex {
    AAPLColorIndexColor,
    AAPLColorIndexAlbedo,
    AAPLColorIndexNormal,
    // scalar material fields packed one per channel, glTF
    // occlusion-roughness-metallic convention: r = occlusion,
    // g = roughness, b = metallic
    AAPLColorIndexMaterial,
    AAPLColorIndexDepth,
    // The image-based light's contribution, kept apart from the direct
    // light (Color) so the screen-space occlusion pass can scale it alone
    // before the composite adds the two
    AAPLColorIndexAmbient,
};

// [[function_constant(index)

enum AAPLFunctionConstantIndex {
    AAPLFunctionConstantIndexHasPerDrawPositionTransform,
    AAPLFunctionConstantIndexHasPerInstancePositionTransform,
    AAPLFunctionConstantIndexHasPerDrawCoordinateTransform,
    AAPLFunctionConstantIndexHasPerInstanceCoordinateTransform,
    AAPLFunctionConstantIndexHasPerDrawColorTransform,
    AAPLFunctionConstantIndexHasPerInstanceColorTransform,
};

// [[raster_order_group(index)]]

enum AAPLRasterOrderGroup {
    AAPLRasterOrderGroupGBuffer,
    AAPLRasterOrderGroupLighting,
};


// [[texture(index)]]

enum AAPLTextureIndex {
    AAPLTextureIndexAlbedo,
    AAPLTextureIndexAlbedoMetallic,
    AAPLTextureIndexClearcoat,
    AAPLTextureIndexClearcoatRoughness,
    AAPLTextureIndexColor,
    AAPLTextureIndexDepth,
    AAPLTextureIndexEmissive,
    AAPLTextureIndexEnvironment,
    AAPLTextureIndexFresnel,
    AAPLTextureIndexMetallic,
    AAPLTextureIndexNormal,
    AAPLTextureIndexNormalRoughness,
    AAPLTextureIndexOcclusion,
    AAPLTextureIndexRoughness,
    AAPLTextureIndexShadow,
    AAPLTextureIndexIrradiance,
};


#pragma mark - Deferred rendering

struct MeshVertex{
    simd_float4 coordinate;
    union {
        struct {
            simd_float4 tangent;
            simd_float4 bitangent;
            simd_float4 normal;
            simd_float4 position;
        };
        simd_float4x4 jacobian; // too cute?
    };
};

// clean up the confusion between cameras and light sources

struct MeshUniforms {
    
    // coordinate systems:
    //
    // tangent space
    //                  normal transform
    // model space
    //                  model transform
    // world space
    //                  view transform
    // eye[light] space
    //                  projection transform
    // clip space
    
    
    // tangent space -> model space -> world space -> eye space -> clip space
    //           normal           model            view        projection
    
    simd_float4 camera_position_world;

    // The camera ray through NDC (x, y), scaled so that its component
    // along the view axis is one: with the Depth G-buffer holding linear
    // view depth (clip w), a pixel's surface is camera + ray * depth.  For
    // a pinhole camera the ray is affine in NDC,
    // ray = camera_ray_centre + x * camera_ray_dx + y * camera_ray_dy,
    // so vertex shaders interpolate it without perspective and kernels
    // evaluate it per tap; nobody needs a matrix.  (xyz; w unused)
    simd_float4 camera_ray_centre;
    simd_float4 camera_ray_dx;
    simd_float4 camera_ray_dy;
    
    matrix_float4x4 view_transform;
    matrix_float4x4 inverse_view_transform;
    
    matrix_float4x4 projection_transform;
    matrix_float4x4 inverse_projection_transform;

    matrix_float4x4 viewprojection_transform;
    matrix_float4x4 inverse_viewprojection_transform;

    // light-specifc
    
    matrix_float4x4 light_viewprojection_transform;
    simd_float3 radiance;

    // for the sun
    simd_float3 light_direction;
    matrix_float4x4 light_viewprojectiontexture_transform; // shadow
    // for point lights
    simd_float4 light_position;
    
    // for image-based lights
    simd_float4 ibl_scale;
    matrix_float3x3 ibl_transform;
    
};

struct MeshInstanced {
    matrix_float4x4 model_transform;
    matrix_float4x4 inverse_transpose_model_transform;
    simd_float4 albedo;
    simd_float4 emissive;
    float metallic;
    float roughness;
};



#pragma mark - Cube filtering

struct CubeFilterUniforms {
    float alpha2;
    matrix_float4x4 transforms[6]; // per face projections
};

#pragma mark - Legacy

typedef struct
{
    // Positions in pixel space (i.e. a value of 100 indicates 100 pixels from the origin/center)
    simd_float2 position; // 8
    
    simd_float2 texCoord; // 8
    
    // vector_uchar4 color;
    // srgba8unorm
    unsigned int color; // 4
    
} MyVertex;

typedef struct
{
    // Positions in pixel space (i.e. a value of 100 indicates 100 pixels from the origin/center)
    struct {
        simd_float4 position; // 16
        
        simd_float2 texCoord; // 8
        
        // 8 bytes wasted, fixme
    };
    
    // vector_uchar4 color;
    // srgba8unorm
    unsigned int color; // 4
    
} MyVertex4;

#pragma mark - GUI quads

// One axis-aligned GUI primitive per instance: a quad, optionally sampling
// the sprite atlas, optionally masked by an antialiased rounded box.  The
// vertex shader (quadVertexShader) expands each instance to a 4-vertex
// triangle strip.  Glyphs, solid fills and rounded boxes all share this
// one instance stream, so draw order is submission order.
//
// All coordinates are drawable pixels, y-down.
//
// `shape` is the rectangle the rounded-box mask is measured from; `draw`
// is the rectangle actually rasterized.  They differ when the mask's soft
// edge spills outside the shape, and when the CPU clips `draw` to a
// parent's clip rect (the shape, and hence the mask, is unchanged by
// clipping).  `uv` is the atlas texCoord at draw's (x0, y0) and (x1, y1)
// corners, so clipping lerps uv along with draw.
//
// 128-bit fields first, 32-bit fields after, so the C and MSL layouts
// agree without hidden padding.

enum WryQuadFlag {
    WryQuadFlagTextured = 1u << 0,  // multiply by the atlas sample at uv
    WryQuadFlagMasked   = 1u << 1,  // multiply by rounded-box coverage
    WryQuadFlagFiltered = 1u << 2,  // sample trilinear (default: nearest,
                                    // for texels drawn 1:1, e.g. glyphs)
    WryQuadFlagStraight = 1u << 3,  // the texture is straight alpha (the symbol
                                    // atlas): premultiply the sample for the
                                    // (one, one minus alpha) blend
    WryQuadFlagOutlined = 1u << 4,  // a glyph: draw a dark outline under the
                                    // textured coverage by dilating the
                                    // atlas alpha (see quadFragmentShader)
};

// Outline radius of WryQuadFlagOutlined glyphs, in atlas texels (drawn 1:1
// as pixels, so the outline scales with the glyphs if they are scaled).
// The dilation taps the integer offsets within this radius; font.mm pads
// each glyph's atlas image to keep those taps inside it.
#define WRY_GLYPH_OUTLINE_RADIUS 2.25f

typedef struct
{
    simd_float4 shape;   // x0, y0, x1, y1
    simd_float4 draw;    // x0, y0, x1, y1
    simd_float4 uv;      // u0, v0, u1, v1 at draw's corners
    simd_float4 radii;   // corner radii: (x0,y0) (x1,y0) (x1,y1) (x0,y1)
    unsigned int color;  // RGBA8Unorm_sRGB, straight alpha
    float sigma;         // mask edge Gaussian prefilter, pixels
    unsigned int flags;  // WryQuadFlag
    unsigned int _pad;
} WryQuadInstance;

typedef struct
{
    // float scale;
    // vector_uint2 viewportSize;
    matrix_float4x4 position_transform;
} MyUniforms;

#pragma mark - Screen-space ambient occlusion

// WrySSAOMode: which estimator the occlusion pass runs (WorldState::_ssao_mode)
#define WRY_SSAO_OFF 0
#define WRY_SSAO_GTAO 1
#define WRY_SSAO_LUFT 2

// The GTAO taps read a depth pyramid (R32Float, this many levels, built
// from the Depth G-buffer each frame by ssao_depth_pyramid_kernel); a tap
// reads the level its distance from the pixel selects, so far taps touch
// few texels.  Not R16Float: a flat plane quantizes into terraces whose
// steps the taps read as horizons (banding)
#define WRY_SSAO_DEPTH_PYRAMID_LEVELS 5

// WrySSAOUniforms::blur: which kernel runs the depth-aware blur over the
// half-resolution (visibility, depth) blocks.  The same filter three ways,
// a measuring aid until the choice is made (see ssao_blur_kernel)
#define WRY_SSAO_BLUR_DIRECT 0      // the (2r + 1)^2 window, read per block
#define WRY_SSAO_BLUR_SEPARABLE 1   // two dispatches of 2r + 1 reads
#define WRY_SSAO_BLUR_TILED 2       // the window over a threadgroup-memory tile
#define WRY_SSAO_BLUR_MAX_RADIUS 4  // sizes the tile

typedef struct {
    // The camera and its ray basis, as MeshUniforms has them
    simd_float4 camera_position_world;
    simd_float4 camera_ray_centre;
    simd_float4 camera_ray_dx;
    simd_float4 camera_ray_dy;
    simd_float2 viewport;           // pixels
    float pixels_per_unit;          // across the view axis, at unit depth
    float radius;                   // world units
    float strength;                 // 1 = as estimated
    int mode;                       // WRY_SSAO_*
    int show;                       // nonzero: composite the factor itself, as grey
    int slices;                     // GTAO: slice directions per pixel
    int steps;                      // GTAO: taps per side per slice
    int pyramid;                    // GTAO: taps read the depth pyramid (else the Depth G-buffer)
    int blur;                       // WRY_SSAO_BLUR_*
    int blur_radius;                // blur taps per side, 1 to WRY_SSAO_BLUR_MAX_RADIUS
} WrySSAOUniforms;

#pragma mark - The board

// The terrain, drawn as a board (Shaders.metal, deferred::terrain_*): one
// quad over the cells in view, every fragment finding its cell's kind in
// a dense per-frame byte map and inking the board itself -- a pastel
// tint per kind, a world-width lattice on the cell edges, and the kind's
// marks from the symbol field's terrain row (tools/make_assets.py,
// TERRAIN_ROW).  The tint is looked up on a lattice offset from the
// lines' and blurred, the misregistration of a two-colour print.
// Distances are world units unless named otherwise.

#define WRY_TERRAIN_KIND_NONE 255
#define WRY_TERRAIN_KINDS 7
// The symbol atlas row of the marks of terrain kinds 0-3 (water, sand,
// grass, rock), at column kind * 4 + variant; the ores (coal, iron ore,
// limestone: kinds 4-6) are in WRY_TERRAIN_ORE_PATTERN_ROW at column
// (kind - 4) * 4 + variant (tools/make_assets.py TERRAIN_ROW / ORE_TERRAIN_ROW)
#define WRY_TERRAIN_PATTERN_ROW 14
#define WRY_TERRAIN_ORE_PATTERN_ROW 12
#define WRY_TERRAIN_PATTERN_VARIANTS 4

typedef struct {
    simd_int2 origin;               // render-space cell of kinds[0]
    simd_int2 size;                 // cells spanned by the map (x fastest)
    simd_int2 focus;                // world tile at render-space origin (the variant hash wants world coordinates)
    simd_float4 tint[WRY_TERRAIN_KINDS];        // linear rgb per kind; a unused
    simd_float4 lattice[WRY_TERRAIN_KINDS];     // per kind: x line opacity, y arm (fraction of an edge drawn from each corner, 1 = whole), z mark opacity, w the share of cells that carry a mark
    simd_float4 line_color;         // linear rgb of the ink; a unused
    simd_float2 misregister;        // tint lattice offset from the lines'
    float tint_blur;                // width of the tint's soft edge
    float line_width;               // the lattice line
    float line_min_pixels;          // below this on screen the line keeps its width and fades instead
    float mark_bias;                // grown (negative) or shrunk mark edges
    float roughness;
    float _padding;
} WryTerrainUniforms;

#pragma mark - Symbol slabs

// The thickened symbols (render/symbol_sdf.hpp): one instance per symbol,
// drawn as a 12-triangle cuboid whose fragments ray-march the filleted
// slab inside it (Shaders.metal, deferred::symbol_fragment_function).
// Slab space is the cell's source texels: x right and y up across the
// symbol, z out of its face, origin at the cell's centre; the model
// transform carries that to the world, with a uniform scale, so normals
// go through it too.

// Source texels spanned by the field's distance channel; must agree with
// SymbolSDF::kRange
// The field's distance channels, in source texels: the fine one (r) for
// the slab's walls, the coarse one (a) for the decal's reach
// Cells per side of the atlas and the field; must agree with SymbolSDF::kCells
#define WRY_SYMBOL_ATLAS_CELLS 16
#define WRY_SYMBOL_SDF_RANGE 16.0f
#define WRY_SYMBOL_SDF_FAR_RANGE 128.0f

typedef struct
{
    matrix_float4x4 model_transform;
    matrix_float4x4 inverse_model_transform;
    simd_float4 cell;           // xy: the cell's top-left atlas uv; z: the cell's uv extent; w: uv per texel
    simd_float4 half_extent;    // texels: xy the cuboid's half extents about the cell's centre, z the half thickness, w the cell's half extent
    float top_radius;           // fillet radii, texels; each below half the
    float bottom_radius;        // stroke width, their sum below the thickness
    float stroke_width;         // texels: the cell's, the decal's far edge
    float decal_reach;          // texels beyond the silhouette the ambient-occlusion decal fades out by; 0 for none
} WrySymbolInstance;

#pragma mark - Decals

// An image projected onto whatever the G-buffer already holds inside a
// box (Shaders.metal, deferred::decal_*), drawn after the geometry of
// the frame.  Decal space is the unit box: x and y in [-0.5, 0.5] across
// the image (y up the image), z in [-0.5, 0.5] along its projection
// (+z toward the viewer of the print), origin at the image's centre; the
// model transform carries that to the world, any scale per axis.  Each
// fragment reads the G-buffer's depth, finds the surface point in decal
// space, rejects points outside the box or on surfaces facing away from
// +z by more than the cutoff, samples the image (straight alpha) and
// blends the stored albedo toward tint.rgb * image.rgb by image.a *
// tint.a; roughness and metallic blend toward the instance's by the same
// coverage times their weights.  Normals are left alone for now (a
// normal map would resolve in this same frame).
typedef struct {
    matrix_float4x4 model_transform;
    matrix_float4x4 inverse_model_transform;
    simd_float4 uv_rect;        // xy: the image's top-left uv (decal (-0.5, +0.5)); zw: uv per unit of x and of -y
    simd_float4 tint;           // rgb multiplies the image's colour; a scales its coverage
    simd_float4 material;       // x roughness and y metallic to blend toward; z and w their weights (0 leaves the surface's)
    float normal_cutoff;        // the surface normal's least cosine with the decal's +z
    float _padding[3];
} WryDecalInstance;



//static constexpr constant uint32_t AAPLMaxTotalThreadsPerObjectThreadgroup = 1;
//static constexpr constant uint32_t AAPLMaxTotalThreadsPerMeshThreadgroup = 2;
//static constexpr constant uint32_t AAPLMaxThreadgroupsPerMeshGrid = 8;


namespace otf {
    
    struct BezierUniforms {
        matrix_float4x4 transformation;
        matrix_float4x4 inverse_transformation;
        simd_float2 pixel_size;
    };
    
    struct PlacedGlyph {
        simd_float2 position;
        unsigned int glyph_index;
        unsigned int _padding[1];
    };
    
    struct GlyphData {
        simd_float2 a;
        simd_float2 b;
        unsigned int bezier_begin;
        unsigned int bezier_end;
        unsigned int _padding[2];
    };
    
//    struct QuadraticBezier {
//        simd_float2 a;
//        simd_float2 b;
//        simd_float2 c;
//        simd_float2 _padding[1];
//    };

    struct CubicBezier {
        simd_float2 a;
        simd_float2 b;
        simd_float2 c;
        simd_float2 d;
    };

    
    struct PerParagraph {
        // Holonomic transformation from (u,v,0,1) to screen
        simd_float4x4 transform;
    };

}

#endif /* ShaderTypes_h */
