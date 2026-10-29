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
};

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
#define WRY_SYMBOL_SDF_RANGE 16.0f
#define WRY_SYMBOL_SDF_FAR_RANGE 128.0f

typedef struct
{
    matrix_float4x4 model_transform;
    matrix_float4x4 inverse_model_transform;
    simd_float4 cell;           // xy: the cell's top-left atlas uv; z: the cell's uv extent; w unused
    simd_float4 half_extent;    // texels: x, y across the cell, z the half thickness; w unused
    float top_radius;           // fillet radii, texels; each below half the
    float bottom_radius;        // stroke width, their sum below the thickness
    float stroke_width;         // texels: the cell's, the decal's far edge
    float decal_reach;          // texels beyond the silhouette the ambient-occlusion decal fades out by; 0 for none
} WrySymbolInstance;



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
