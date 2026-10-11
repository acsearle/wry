//
//  WryMesh.mm
//  client
//
//  Created by Antony Searle on 2/9/2023.
//

#include "WryMesh.h"

@implementation WryMesh
{
    MeshInstanced* _instances;
    NSUInteger _instanceCapacity;
}

- (instancetype)init
{
    if (self = [super init]) {
        _instanceCapacity = 256;
        _instances = (MeshInstanced*) malloc(sizeof(MeshInstanced) * _instanceCapacity);
    }
    return self;
}

-(MeshInstanced* _Nonnull) instances {
    return _instances;
}

-(void) addInstance:(MeshInstanced)instance {
    if (_instanceCount >= _instanceCapacity) {
        _instanceCapacity *= 2;
        _instances = (MeshInstanced*) realloc(_instances, sizeof(MeshInstanced) * _instanceCapacity);
    }
    _instances[_instanceCount++] = instance;
}

- (void)drawWithRenderCommandEncoder:(id<MTLRenderCommandEncoder> _Nonnull)encoder arena:(WryBufferArena* _Nonnull)arena
{
    if (_instanceCount == 0)
        return;

    [encoder setVertexBuffer:_vertexBuffer
                      offset:0
                     atIndex:AAPLBufferIndexVertices];

    WryBufferSlice instances = [arena copyBytes:_instances
                                         length:sizeof(MeshInstanced) * _instanceCount];
    [encoder setVertexBuffer:instances.buffer
                      offset:instances.offset
                     atIndex:AAPLBufferIndexInstanced];
    [encoder setFragmentTexture:_emissiveTexture atIndex:AAPLTextureIndexEmissive];
    [encoder setFragmentTexture:_albedoTexture atIndex:AAPLTextureIndexAlbedo];
    [encoder setFragmentTexture:_metallicTexture atIndex:AAPLTextureIndexMetallic];
    [encoder setFragmentTexture:_normalTexture atIndex:AAPLTextureIndexNormal];
    [encoder setFragmentTexture:_roughnessTexture atIndex:AAPLTextureIndexRoughness];
    [encoder setFragmentTexture:_occlusionTexture atIndex:AAPLTextureIndexOcclusion];
    
    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip
                        indexCount:_indexBuffer.length / sizeof(uint)
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:_indexBuffer
                 indexBufferOffset:0
                     instanceCount:_instanceCount];
    
}

@end
