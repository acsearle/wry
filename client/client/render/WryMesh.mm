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
    NSUInteger _instanceCapacity;

    NSMutableArray<id<MTLBuffer>>* _instanceBuffers;


}

- (instancetype)initWithDevice:(id<MTLDevice> _Nonnull)device
{
    if (self = [super init]) {
        _device = device;
        _instanceCapacity = 256;
        _instances = (MeshInstanced*) malloc(sizeof(MeshInstanced) * _instanceCapacity);
        _instanceBuffers = [NSMutableArray new];
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

- (void)drawWithRenderCommandEncoder:(id<MTLRenderCommandEncoder> _Nonnull)encoder commandBuffer:(id<MTLCommandBuffer> _Nonnull)buffer
{
    if (_instanceCount == 0)
        return;

    [encoder setVertexBuffer:_vertexBuffer
                      offset:0
                     atIndex:AAPLBufferIndexVertices];

    size_t length = sizeof(MeshInstanced) * _instanceCount;

    id<MTLBuffer> instanceBuffer = nil;
    @synchronized (_instanceBuffers) {
        if ([_instanceBuffers count]) {
            instanceBuffer = [_instanceBuffers lastObject];
            [_instanceBuffers removeLastObject];
        }
    }
    // Pooled buffers are recycled across frames; allocate a new one if this
    // frame needs more instances than the recycled buffer can hold (nil.length
    // is 0, so this also covers the empty-pool case).
    if (instanceBuffer.length < length) {
        instanceBuffer = [_device newBufferWithLength:length options:MTLStorageModeShared];
    }

    [buffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull) {
        @synchronized (self->_instanceBuffers) {
            [self->_instanceBuffers addObject:instanceBuffer];
        }
    }];

    memcpy([instanceBuffer contents], _instances, length);

    [encoder setVertexBuffer:instanceBuffer
                      offset:0
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
