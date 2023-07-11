//
//  ShaderTypes.h
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#ifndef ShaderTypes_h
#define ShaderTypes_h

#include <simd/simd.h>

typedef enum MyVertexInputIndex
{
    MyVertexInputIndexVertices = 0,
    MyVertexInputIndexUniforms = 1,
} MyVertexInputIndex;

typedef enum AAPLTextureIndex
{
    AAPLTextureIndexBaseColor = 0,
} AAPLTextureIndex;

typedef struct
{
    // Positions in pixel space (i.e. a value of 100 indicates 100 pixels from the origin/center)
    vector_float2 position;
    
    vector_float2 texCoord;
    
    // vector_uchar4 color;
    // srgba8unorm
    unsigned int color;
    
} MyVertex;

typedef struct
{
    // float scale;
    // vector_uint2 viewportSize;
    matrix_float3x2 position_transform;
} MyUniforms;


#endif /* ShaderTypes_h */
