//
//  WryMesh.hpp
//  client
//
//  Created by Antony Searle on 2/9/2023.
//

#ifndef WryMesh_hpp
#define WryMesh_hpp

#import <Metal/Metal.h>

#include "ShaderTypes.h"

@interface WryMesh : NSObject

@property (nonatomic, nullable) id<MTLTexture> albedoTexture;
@property (nonatomic, nullable) id<MTLTexture> normalTexture;
@property (nonatomic, nullable) id<MTLTexture> metallicTexture;
@property (nonatomic, nullable) id<MTLTexture> roughnessTexture;
@property (nonatomic, nullable) id<MTLTexture> emissiveTexture;

@property (nonatomic, nullable) id<MTLBuffer> vertexBuffer;
@property (nonatomic, nullable) id<MTLBuffer> indexBuffer;
@property (nonatomic, nullable) id<MTLBuffer> jacobianBuffer;

@property (nonatomic) unsigned int instanceCount;

-(MeshUniforms* _Nonnull) uniforms;
-(MeshInstanced* _Nonnull) instances;

-(instancetype _Nonnull)initWithDevice:(id<MTLDevice> _Nonnull)device;

-(void) drawWithRenderCommandEncoder:(id<MTLRenderCommandEncoder> _Nonnull)encoder;

@end

#endif /* WryMesh_hpp */
