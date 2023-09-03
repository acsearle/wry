//
//  WryMesh.mm
//  client
//
//  Created by Antony Searle on 2/9/2023.
//

#include "WryMesh.hpp"

@implementation WryMesh
{
    id<MTLDevice> _device;
    MeshUniforms _uniforms;
    MeshInstanced* _instances;
}

- (instancetype)initWithDevice:(id<MTLDevice> _Nonnull)device
{
    if (self = [super init]) {
        _device = device;
        _instances = (MeshInstanced*) malloc(sizeof(MeshInstanced) * 100);
    }
    return self;
}

-(MeshUniforms* _Nonnull) uniforms {
    return &_uniforms;
}

-(MeshInstanced* _Nonnull) instances {
    return _instances;
}

- (void)drawWithRenderCommandEncoder:(id<MTLRenderCommandEncoder> _Nonnull)encoder
{
    [encoder setVertexBuffer:_vertexBuffer
                      offset:0
                     atIndex:AAPLBufferIndexVertices];
    
    assert(_instanceCount < 100);
    NSUInteger length = sizeof(MeshInstanced) * _instanceCount;
    id<MTLBuffer> instanceBuffer = [_device newBufferWithBytes:_instances
                                                        length:length
                                                       options:MTLStorageModeShared];
    [encoder setVertexBuffer:instanceBuffer
                      offset:0
                     atIndex:AAPLBufferIndexInstanced];
    [encoder setVertexBytes:&_uniforms
                     length:sizeof(MeshUniforms)
                    atIndex:AAPLBufferIndexUniforms];

    [encoder setFragmentBytes:&_uniforms
                       length:sizeof(MeshUniforms)
                      atIndex:AAPLBufferIndexUniforms];
    [encoder setFragmentTexture:_emissiveTexture atIndex:AAPLTextureIndexEmissive];
    [encoder setFragmentTexture:_albedoTexture atIndex:AAPLTextureIndexAlbedo];
    [encoder setFragmentTexture:_metallicTexture atIndex:AAPLTextureIndexMetallic];
    [encoder setFragmentTexture:_normalTexture atIndex:AAPLTextureIndexNormal];
    [encoder setFragmentTexture:_roughnessTexture atIndex:AAPLTextureIndexRoughness];
    
    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip
                        indexCount:_indexBuffer.length/sizeof(unsigned int)
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:_indexBuffer
                 indexBufferOffset:0
                     instanceCount:_instanceCount];
    
}

@end
