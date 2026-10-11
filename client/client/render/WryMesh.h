//
//  WryMesh.hpp
//  client
//
//  Created by Antony Searle on 2/9/2023.
//

#ifndef WryMesh_h
#define WryMesh_h

#import <Metal/Metal.h>

#include "ShaderTypes.h"
#import "WryBufferArena.h"

@interface WryMesh : NSObject

@property (nonatomic, nullable) id<MTLTexture> albedoTexture;
@property (nonatomic, nullable) id<MTLTexture> normalTexture;
@property (nonatomic, nullable) id<MTLTexture> metallicTexture;
@property (nonatomic, nullable) id<MTLTexture> roughnessTexture;
@property (nonatomic, nullable) id<MTLTexture> emissiveTexture;
@property (nonatomic, nullable) id<MTLTexture> occlusionTexture;

@property (nonatomic, nullable) id<MTLBuffer> vertexBuffer;
@property (nonatomic, nullable) id<MTLBuffer> indexBuffer;

@property (nonatomic) unsigned int instanceCount;

-(MeshInstanced* _Nonnull) instances;

// Append one instance, growing the backing store as needed.
-(void) addInstance:(MeshInstanced)instance;

-(instancetype _Nonnull)init;

// The instance array is copied into `arena` for this frame's command buffer.
-(void) drawWithRenderCommandEncoder:(id<MTLRenderCommandEncoder> _Nonnull)encoder arena:(WryBufferArena* _Nonnull)arena;

@end

#endif /* WryMesh_h */
