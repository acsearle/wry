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
    AAPLColorIndexAlbedoMetallic,
    AAPLColorIndexNormalRoughness,
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

typedef struct
{
    // float scale;
    // vector_uint2 viewportSize;
    matrix_float4x4 position_transform;
} MyUniforms;



//static constexpr constant uint32_t AAPLMaxTotalThreadsPerObjectThreadgroup = 1;
//static constexpr constant uint32_t AAPLMaxTotalThreadsPerMeshThreadgroup = 2;
//static constexpr constant uint32_t AAPLMaxThreadgroupsPerMeshGrid = 8;


struct BezierPayload {
    int something;
};

namespace bezier {
    
    struct Character {
        simd_float2 position;
        unsigned int glyph_id;
        unsigned int _padding[1];
    };
    
    struct GlyphInformation {
        simd_float2 a;
        simd_float2 b;
        unsigned int bezier_begin;
        unsigned int bezier_end;
        unsigned int _padding[2];
    };
    
    struct BezierControlPoints {
        simd_float2 a;
        simd_float2 b;
        simd_float2 c;
        simd_float2 _padding[1];
    };

}

#endif /* ShaderTypes_h */
