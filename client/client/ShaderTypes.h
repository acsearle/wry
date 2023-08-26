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
    AAPLColorIndexNormal,
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
    matrix_float4x4 model_transform; // make 3x4
    matrix_float4x4 viewprojection_transform;
    matrix_float4x4 light_viewprojection_transform;
    
    matrix_float4x4 inverse_model_transform; // make 3x3
    vector_float3 light_direction;
    
    vector_float4 camera_world_position;
    
} MeshUniforms;

typedef struct {
    float alpha;
    matrix_float4x4 transforms[6];
    
} cubeFilterUniforms;


#endif /* ShaderTypes_h */
