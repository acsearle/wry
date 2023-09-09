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
        simd_float4x4 jacobian;
    };
};

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
    
    vector_float4 origin;
    
    matrix_float4x4 view_transform;
    matrix_float4x4 inverse_view_transform;
    
    matrix_float4x4 projection_transform;
    matrix_float4x4 inverse_projection_transform;

    matrix_float4x4 viewprojection_transform;
    matrix_float4x4 inverse_viewprojection_transform;

    // light-specifc
    
    vector_float3 light_direction;
    matrix_float4x4 light_viewprojection_transform;
    vector_float3 radiance;
    
    // IBL multiplier
    vector_float4 ibl_scale;
    matrix_float3x3 ibl_transform;
    
};

struct MeshInstanced {
    matrix_float4x4 model_transform;
    matrix_float4x4 inverse_transpose_model_transform;
    simd_float4 albedo;
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
    vector_float2 position; // 8
    
    vector_float2 texCoord; // 8
    
    // vector_uchar4 color;
    // srgba8unorm
    unsigned int color; // 4
    
} MyVertex;

typedef struct
{
    // Positions in pixel space (i.e. a value of 100 indicates 100 pixels from the origin/center)
    struct {
        vector_float4 position; // 16
        
        vector_float2 texCoord; // 8
        
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


#endif /* ShaderTypes_h */
