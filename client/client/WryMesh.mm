//
//  WryMesh.mm
//  client
//
//  Created by Antony Searle on 2/9/2023.
//

#include "WryMesh.h"

@implementation WryMesh
{
    id<MTLDevice> _device;
    MeshInstanced* _instances;
    
    NSMutableArray<id<MTLBuffer>>* _instanceBuffers;
    
    
}

- (instancetype)initWithDevice:(id<MTLDevice> _Nonnull)device
{
    if (self = [super init]) {
        _device = device;
        _instances = (MeshInstanced*) malloc(sizeof(MeshInstanced) * 100);
        _instanceBuffers = [NSMutableArray new];
    }
    return self;
}

-(MeshInstanced* _Nonnull) instances {
    return _instances;
}

- (void)drawWithRenderCommandEncoder:(id<MTLRenderCommandEncoder> _Nonnull)encoder commandBuffer:(id<MTLCommandBuffer> _Nonnull)buffer
{
    [encoder setVertexBuffer:_vertexBuffer
                      offset:0
                     atIndex:AAPLBufferIndexVertices];
    
    id<MTLBuffer> instanceBuffer = nil;
    @synchronized (_instanceBuffers) {
        if ([_instanceBuffers count]) {
            instanceBuffer = [_instanceBuffers lastObject];
            [_instanceBuffers removeLastObject];
        } else {
            size_t length = sizeof(MeshInstanced) * 1000;
            instanceBuffer = [_device newBufferWithLength:length options:MTLStorageModeShared];
        }
    }
    
    [buffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull) {
        @synchronized (self->_instanceBuffers) {
            [self->_instanceBuffers addObject:instanceBuffer];
        }
    }];
    
    assert(_instanceCount < 1000);
    size_t length = sizeof(MeshInstanced) * _instanceCount;

    memcpy([instanceBuffer contents], _instances, length);
        
    [encoder setVertexBuffer:instanceBuffer
                      offset:0
                     atIndex:AAPLBufferIndexInstanced];
    [encoder setFragmentTexture:_emissiveTexture atIndex:AAPLTextureIndexEmissive];
    [encoder setFragmentTexture:_albedoTexture atIndex:AAPLTextureIndexAlbedo];
    [encoder setFragmentTexture:_metallicTexture atIndex:AAPLTextureIndexMetallic];
    [encoder setFragmentTexture:_normalTexture atIndex:AAPLTextureIndexNormal];
    [encoder setFragmentTexture:_roughnessTexture atIndex:AAPLTextureIndexRoughness];
    
    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip
                        indexCount:_indexBuffer.length / sizeof(uint)
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:_indexBuffer
                 indexBufferOffset:0
                     instanceCount:_instanceCount];
    
}

@end
