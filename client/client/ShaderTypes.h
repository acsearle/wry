//
//  ShaderTypes.h
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#ifndef ShaderTypes_h
#define ShaderTypes_h

#include <simd/simd.h>

// location attributes

typedef enum
{
    AAPLBufferIndexUniforms,
    AAPLBufferIndexVertices,
    AAPLBufferIndexIndices,
} AAPLBufferIndex;


typedef enum
{
    AAPLTextureIndexColor,
    AAPLTextureIndexNormal,
    AAPLTextureIndexShadow,
    AAPLTextureIndexRoughness,
    AAPLTextureIndexMetallic,
    AAPLTextureIndexEnvironment,
} AAPLTextureIndex;

typedef enum {
    AAPLColorIndexColor,
    AAPLColorIndexAlbedoMetallic,
    AAPLColorIndexNormalRoughness,
    AAPLColorIndexDepthAsColor,
} AAPLColorIndex;


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


typedef struct {
    vector_float4 position;
    vector_float2 texCoord;
    vector_float4 normal;
    vector_float4 tangent;
} MeshVertex;

typedef struct
{
    
    // coordinate systems:
    //
    // tangent space
    //                  normal transform
    // model space
    //                  model transform
    // world space
    //                  view transform
    // eye space
    //                  projection transform
    // clip space
    //
    // light space (eye)
    
    
    // tangent space -> model space -> world space -> eye space -> clip space
    //           normal           model            view        projection
    
    
    matrix_float4x4 model_transform;
    matrix_float4x4 inverse_model_transform;

    matrix_float4x4 view_transform;
    matrix_float4x4 inverse_view_transform;
    vector_float4 camera_world_position;
    
    matrix_float4x4 projection_transform;

    matrix_float4x4 viewprojection_transform;
    
    
    vector_float3 light_direction;
    matrix_float4x4 light_viewprojection_transform;
    
} MeshUniforms;

typedef struct {
    float alpha;
    matrix_float4x4 transforms[6];
    
} cubeFilterUniforms;


#endif /* ShaderTypes_h */
