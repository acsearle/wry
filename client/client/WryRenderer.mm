//
//  WryRenderer.mm
//  client
//
//  Created by Antony Searle on 1/7/2023.
//

// Shared Metal header first for no contamination
#include "ShaderTypes.h"

#include <MetalKit/MetalKit.h>
#include <random>

#include "WryRenderer.h"

#include "atlas.hpp"
#include "font.hpp"
#include "mesh.hpp"
#include "platform.hpp"

@implementation WryRenderer
{
    
    simd_uint2 _viewportSize;
    
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;
    id<MTLLibrary> _library;
    
    
    id<MTLBuffer> _screenQuadVertexBuffer;
    id<MTLDepthStencilState> _enabledDepthStencilState;
    id<MTLDepthStencilState> _disabledDepthStencilState;
    
    
    // shadow map pass
    
    id<MTLRenderPipelineState> _shadowRenderPipelineState;
    id<MTLTexture> _shadowRenderTargetTexture;
    
    
    // deferred render pass
    
    id <MTLRenderPipelineState> _colorRenderPipelineState;
    id <MTLRenderPipelineState> _lightingPipelineState;
    id <MTLRenderPipelineState> _pointLightPipelineState;
    
    id<MTLTexture> _colorRenderTargetTexture;
    id<MTLTexture> _albedoMetallicRenderTargetTexture;
    id<MTLTexture> _normalRoughnessRenderTargetTexture;
    id<MTLTexture> _depthRenderTargetTexture;
    id<MTLTexture> _fresnelTexture;
    id<MTLTexture> _depthAsColorRenderTargetTexture;
    
    
    // conventional compositing for hud
    
    id <MTLRenderPipelineState> _renderPipelineState;
    
    
    
    NSUInteger _frameNum;
    
    wry::atlas* _atlas;
    wry::font* _font;
    std::vector<wry::sprite> _sprites;
    
    std::shared_ptr<wry::model> _model;
    
    wry::subvertex _terrain_triangles[6];
    
    wry::sprite _cube_sprites[2];
    
    wry::sprite _checkerboard_sprite;
    
    id <MTLBuffer> _cube_buffer;
    id <MTLTexture> _cube_normals;
    id <MTLTexture> _cube_colors;
    id <MTLTexture> _cube_roughness;
    id <MTLTexture> _cube_metallic;
    id <MTLTexture> _cube_emissive;
    
    id <MTLTexture> _environments[3];
    
    int _mesh_count;
    
}

-(void) dealloc {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

-(id<MTLTexture>)newTextureFromResource:(NSString*)name
                                 ofType:(NSString*)ext
{
    return [self newTextureFromResource:name ofType:ext withPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB];
}


-(id<MTLTexture>)newTextureFromResource:(NSString*)name
                                 ofType:(NSString*)ext
                        withPixelFormat:(MTLPixelFormat)pixelFormat
{
    wry::image image = wry::from_png(wry::path_for_resource([name UTF8String], [ext UTF8String]));
    
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
    descriptor.textureType = MTLTextureType2D;
    descriptor.pixelFormat = pixelFormat;
    descriptor.width = image.width();
    descriptor.height = image.height();
    descriptor.mipmapLevelCount = 1 + wry::min(__builtin_ctzl(descriptor.width),
                                               __builtin_ctzl(descriptor.height));
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageShaderRead;
    
    id<MTLTexture> texture = [_device newTextureWithDescriptor:descriptor];
    [texture replaceRegion:MTLRegionMake2D(0, 0, image.width(), image.height())
                    mipmapLevel:0
                      withBytes:image.data()
                    bytesPerRow:image.stride() * 4];
    texture.label = name;
    
    id<MTLCommandBuffer> buffer = [_commandQueue commandBuffer];
    
    id<MTLBlitCommandEncoder> encoder = [buffer blitCommandEncoder];
    [encoder generateMipmapsForTexture:texture];
    [encoder optimizeContentsForGPUAccess:texture];
    [encoder endEncoding];
    
    [buffer commit];
    
    return texture;
}

-(id<MTLTexture>)prefilteredEnvironmentMapFromResource:(NSString*)name ofType:ext {
    
    id<MTLTexture> input = [self newTextureFromResource:name ofType:ext];
    
    id<MTLTexture> output = nil;
    {
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
        descriptor.textureType = MTLTextureTypeCube;
        descriptor.width = 256;
        descriptor.height = descriptor.width;
        descriptor.pixelFormat = MTLPixelFormatRGBA32Float;
        descriptor.mipmapLevelCount = 5; //__builtin_ctzl(descriptor.width) + 1;
        descriptor.usage = MTLTextureUsageShaderRead;
        descriptor.resourceOptions = MTLResourceStorageModePrivate;
        output = [_device newTextureWithDescriptor:descriptor];
        output.label = name;
    }
    
    id<MTLRenderPipelineState> pipeline = nil;
    {
        MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.label = @"CubeFilter3";
        descriptor.vertexFunction = [self newFunctionWithName:@"CubeFilterVertex"];
        descriptor.fragmentFunction = [self newFunctionWithName:@"CubeFilterAccumulate3"];
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
        descriptor.colorAttachments[0].blendingEnabled = YES;
        descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
        descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
        descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
        descriptor.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
        pipeline = [self newRenderPipelineStateWithDescriptor:descriptor];
    }
    
    CubeFilterUniforms uniforms;
    {
        // we have position in NDCs with XY01
        //
        
        // translate z=0 near clip plane cordinates to z=1
        simd_float4x4 Z
        = simd_matrix(simd_make_float4(1.0f, 0.0f, 0.0f, 0.0f),
                      simd_make_float4(0.0f, 1.0f, 0.0f, 0.0f),
                      simd_make_float4(0.0f, 0.0f, 1.0f, 0.0f),
                      simd_make_float4(0.0f, 0.0f, 1.0f, 1.0f));
        uniforms.transforms[0]
        = simd_mul(simd_matrix_rotate(M_PI_2, simd_make_float3(0.0f, 1.0f, 0.0f)), Z);
        uniforms.transforms[1]
        = simd_mul(simd_matrix_rotate(-M_PI_2, simd_make_float3(0.0f, 1.0f, 0.0f)), Z);
        uniforms.transforms[2]
        = simd_mul(simd_matrix_rotate(M_PI_2, simd_make_float3(-1.0f, 0.0f, 0.0f)), Z);
        uniforms.transforms[3]
        = simd_mul(simd_matrix_rotate(-M_PI_2, simd_make_float3(-1.0f, 0.0f, 0.0f)), Z);
        uniforms.transforms[4] = Z;
        uniforms.transforms[5]
        = simd_mul(simd_matrix_rotate(M_PI, simd_make_float3(0.0f, 1.0f, 0.0f)), Z);
    }
    
    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    
    for (NSUInteger level = 0; level != output.mipmapLevelCount; ++level) {
        
        id<MTLTexture> target;
        {
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
            descriptor.textureType = MTLTextureTypeCube;
            descriptor.pixelFormat = MTLPixelFormatRGBA32Float;
            descriptor.width = output.width >> level;
            descriptor.height = output.height >> level;
            descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            descriptor.resourceOptions = MTLResourceStorageModePrivate;
            target = [_device newTextureWithDescriptor:descriptor];
            target.label = @"Cube filter accumulate";
        }
        
        MTLRenderPassDescriptor* renderPass = [MTLRenderPassDescriptor new];
        renderPass.colorAttachments[0].texture = target;
        renderPass.colorAttachments[0].loadAction = MTLLoadActionClear;
        renderPass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
        renderPass.colorAttachments[0].storeAction = MTLStoreActionStore;
        renderPass.renderTargetArrayLength = 6;
        
        {
            float roughness = pow(2.0, 1.0 + level - output.mipmapLevelCount);
            float alpha = roughness * roughness;
            uniforms.alpha2 = alpha * alpha;

            id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPass];

            [encoder setRenderPipelineState:pipeline];
            [encoder setVertexBuffer:_screenQuadVertexBuffer
                              offset:0
                             atIndex:AAPLBufferIndexVertices];
            [encoder setVertexBytes:&uniforms
                             length:sizeof(CubeFilterUniforms)
                            atIndex:AAPLBufferIndexUniforms];
            [encoder setFragmentBytes:&uniforms
                               length:sizeof(CubeFilterUniforms)
                              atIndex:AAPLBufferIndexUniforms];
            [encoder setFragmentTexture:input
                                atIndex:AAPLTextureIndexColor];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:6
                      instanceCount:6];
            
            [encoder endEncoding];
        }
        
        {
            id<MTLBlitCommandEncoder> encoder = [commandBuffer blitCommandEncoder];
            
            [encoder copyFromTexture:target
                         sourceSlice:0
                         sourceLevel:0
                           toTexture:output
                    destinationSlice:0
                    destinationLevel:level
                          sliceCount:6
                          levelCount:1 ];
            [encoder optimizeContentsForGPUAccess:output];
            
            [encoder endEncoding];
        }
        
    } // end loop over mip-map levels
    
    [commandBuffer commit];
    
    return output;

}

-(id<MTLRenderPipelineState>) newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor*)descriptor
{
    id<MTLRenderPipelineState> state = nil;
    NSError* error = nil;
    state = [_device newRenderPipelineStateWithDescriptor:descriptor
                                                    error:&error];
    if (!state) {
        NSLog(@"ERROR: Failed aquiring pipeline state: %@", error);
        exit(EXIT_FAILURE);
    }
    return state;
}


-(id<MTLFunction>) newFunctionWithName:(NSString*)name
{
    id <MTLFunction> function = [_library newFunctionWithName:name];
    if (!function) {
        NSLog(@"ERROR: newFunctionWithName:@\"%@\"", name);
        exit(EXIT_FAILURE);
    }
    function.label = name;
    return function;
}

-(nonnull instancetype)initWithMetalDevice:(nonnull id<MTLDevice>)device
                        drawablePixelFormat:(MTLPixelFormat)drawablePixelFormat
                                      model:(std::shared_ptr<wry::model>)model_
{
    
    if ((self = [super init])) {
        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
        
        _model = model_;
        
        _frameNum = 0;
        
        _device = device;
        _library = [_device newDefaultLibrary];

        _commandQueue = [_device newCommandQueue];
        
        {
            wry::array<simd_float4> a = wry::mesh::clip_space_quad();
            _screenQuadVertexBuffer = [_device newBufferWithBytes:a.data()
                                                           length:a.size()*sizeof(simd_float4) options:MTLResourceStorageModeShared];
        }
        
        {
            // Prepare depth-stencil states
            
            MTLDepthStencilDescriptor* descriptor = [MTLDepthStencilDescriptor new];
            
            descriptor.depthCompareFunction = MTLCompareFunctionLess;
            descriptor.depthWriteEnabled = YES;
            descriptor.label = @"Shadow map depth/stencil state";
            _enabledDepthStencilState = [_device newDepthStencilStateWithDescriptor:descriptor];
            
            descriptor.depthCompareFunction = MTLCompareFunctionAlways;
            descriptor.depthWriteEnabled = NO;
            _disabledDepthStencilState = [_device newDepthStencilStateWithDescriptor:descriptor];
            
        }
        
        
        {
            
            _environments[0] = [self prefilteredEnvironmentMapFromResource:@"dawn" ofType:@"png"];
            _environments[1] = [self prefilteredEnvironmentMapFromResource:@"day" ofType:@"png"];
            _environments[2] = [self prefilteredEnvironmentMapFromResource:@"dusk" ofType:@"png"];
                      
        }
        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
        
        {
            {
                MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
                descriptor.textureType = MTLTextureType2D;
                descriptor.pixelFormat = MTLPixelFormatRG16Float;
                descriptor.width = 256;
                descriptor.height = 256;
                descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
                descriptor.resourceOptions = MTLStorageModeShared;
                _fresnelTexture = [_device newTextureWithDescriptor:descriptor];
                _fresnelTexture.label = @"Fresnel term";
            }
            
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor new];
            pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].texture = _fresnelTexture;
            
            id<MTLRenderPipelineState> pipeline = nil;
            {
                MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
                descriptor.vertexFunction = [self newFunctionWithName:@"SplitSumVertex"];
                descriptor.fragmentFunction = [self newFunctionWithName:@"SplitSumFragment"];
                descriptor.colorAttachments[0].pixelFormat = _fresnelTexture.pixelFormat;
                
                pipeline = [self newRenderPipelineStateWithDescriptor:descriptor];
            }
                        
            id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
            id<MTLRenderCommandEncoder> commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
            
            [commandEncoder setRenderPipelineState:pipeline];
            [commandEncoder setVertexBuffer:_screenQuadVertexBuffer
                                     offset:0
                                    atIndex:AAPLBufferIndexVertices];
            [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                               vertexStart:0
                               vertexCount:6];
            [commandEncoder endEncoding];
            [commandBuffer commit];
            
            
        }

        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);

        {
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
            descriptor.textureType = MTLTextureType2D;
            descriptor.width = 1024;
            descriptor.height = 1024;
            descriptor.pixelFormat = MTLPixelFormatDepth32Float;
            descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            descriptor.resourceOptions = MTLResourceStorageModePrivate;
            _shadowRenderTargetTexture = [_device newTextureWithDescriptor:descriptor];
            _shadowRenderTargetTexture.label = @"Shadow map texture";
        }
        
        {
            MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
            descriptor.label = @"Shadow map pipeline";
            descriptor.vertexFunction = [self newFunctionWithName:@"meshVertexShader"];
            descriptor.vertexBuffers[AAPLBufferIndexVertices].mutability = MTLMutabilityImmutable;
            descriptor.vertexBuffers[AAPLBufferIndexUniforms].mutability = MTLMutabilityImmutable;
            descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            _shadowRenderPipelineState = [self newRenderPipelineStateWithDescriptor:descriptor];
        }
        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
        
        {
            {
                
                // auto v = wry::mesh::prism(16);
                /*
                auto p = wry::mesh2::polyhedron::icosahedron();
                wry::mesh2::triangulation q;
                p.triangulate(q);
                q.tesselate();
                q.tesselate();
                q.tesselate();
                for (auto& x : q.vertices) {
                    x.xyz = simd_normalize(x.xyz);
                }
                wry::mesh2::mesh m;
                m.position_from(q);
                m.normal_from_triangle();
                m.texcoord_from_position();
                m.tangent_from_texcoord();
                 */

                wry::mesh2::mesh m; // cogwheel
                {
                    auto C = simd_matrix_rotate(-0.25,
                                                simd_normalize(simd_make_float3(1.0, 1.0f, 1.0f)));
                    auto B = simd_matrix_translate(simd_make_float3(0.0, 0.0, 0.5f));
                    auto A = simd_matrix_scale(0.95f);
                    auto D = simd_mul(A, simd_mul(B, C));
                    wry::mesh2::triangulation q;
                    auto base = wry::mesh2::polygon::regular(12);
                    base.stellate(+1);
                    base.truncate(0.4f);
                    //base.triangulate(q);
                    //auto p = wry::mesh2::polyhedron::frustum(base, 0.75f);
                    auto p = wry::mesh2::polyhedron::extrusion(base, 50, D);
                    p.apply(simd_matrix_scale(0.5));
                    p.triangulate(q);
                    m.position_from(q);
                    m.normal_from_triangle();
                    // m.texcoord_from_position(simd_matrix_scale(0.25f));
                    m.texcoord_from_normal();
                    m.tangent_from_texcoord();
                }
                 
                /*
                wry::mesh2::mesh m;
                {
                    wry::mesh2::triangulation q;
                    auto p = wry::mesh2::polyhedron::icosahedron();
                    // p.apply(simd_matrix_scale(simd_make_float3(-1,1,1)));
                    p.stellate(1.0f);
                    p.triangulate(q);
                    m.position_from(q);
                    m.normal_from_triangle();
                    // m.texcoord_from_position(simd_matrix_scale(0.25f));
                    m.texcoord_from_normal();
                    m.tangent_from_texcoord();
                }
                 */
                
                    
                
                _mesh_count = (int) m.vertices.size();
                
                {
                    
                    size_t length = sizeof(MeshVertex) * _mesh_count;
                    _cube_buffer = [_device newBufferWithLength:length options:MTLResourceStorageModeShared];
                    std::memcpy(_cube_buffer.contents, m.vertices.data(), length);
                    
                }
                
            }

            NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);

            {
                _cube_emissive = [self newTextureFromResource:@"emissive"
                                                       ofType:@"png"];
                //_cube_colors = [self newTextureFromResource:@"rustediron2_basecolor"
                //                                     ofType:@"png"];
                _cube_colors = [self newTextureFromResource:@"albedo"
                                                     ofType:@"png"];
                _cube_metallic = [self newTextureFromResource:@"rustediron2_metallic"
                                                       ofType:@"png"];
                _cube_normals = [self newTextureFromResource:@"rustediron2_normal"
                                                      ofType:@"png"
                                             withPixelFormat:MTLPixelFormatRGBA8Unorm];
                _cube_roughness = [self newTextureFromResource:@"rustediron2_roughness"
                                                        ofType:@"png"];
            }
            
            NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
            
        }
        
        {
            // main depth buffer
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
            descriptor.textureType = MTLTextureType2D;
            descriptor.width = 1920; // todo resize awareness
            descriptor.height = 1080;
            descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
            descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            descriptor.resourceOptions = MTLResourceStorageModeMemoryless;
            _albedoMetallicRenderTargetTexture = [_device newTextureWithDescriptor:descriptor];
            _albedoMetallicRenderTargetTexture.label = @"AlbedoMetallic";
            
            descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
            descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            descriptor.resourceOptions = MTLResourceStorageModeMemoryless;
            _normalRoughnessRenderTargetTexture = [_device newTextureWithDescriptor:descriptor];
            _normalRoughnessRenderTargetTexture.label = @"NormalRoughness";

            descriptor.pixelFormat = MTLPixelFormatR32Float;
            descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            descriptor.resourceOptions = MTLResourceStorageModeMemoryless;
            _depthAsColorRenderTargetTexture = [_device newTextureWithDescriptor:descriptor];
            _depthAsColorRenderTargetTexture.label = @"DepthAsColor";
            
            descriptor.pixelFormat = MTLPixelFormatDepth32Float;
            descriptor.usage = MTLTextureUsageRenderTarget;
            descriptor.resourceOptions = MTLResourceStorageModeMemoryless;
            _depthRenderTargetTexture = [_device newTextureWithDescriptor:descriptor];
            _depthRenderTargetTexture.label = @"Depth";
            

        }
        
        
        {
            MTLRenderPipelineDescriptor *descriptor = [[MTLRenderPipelineDescriptor alloc] init];
            
            descriptor.label                           = @"Mesh to gbuffer";
            
            descriptor.vertexFunction = [self newFunctionWithName:@"meshVertexShader"];
            descriptor.fragmentFunction = [self newFunctionWithName:@"meshGbufferFragment"];
            descriptor.colorAttachments[AAPLColorIndexColor].pixelFormat = drawablePixelFormat;
            descriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].pixelFormat = MTLPixelFormatRGBA16Float;
            descriptor.colorAttachments[AAPLColorIndexNormalRoughness].pixelFormat = MTLPixelFormatRGBA16Float;
            descriptor.colorAttachments[AAPLColorIndexDepth].pixelFormat = MTLPixelFormatR32Float;
            descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

            _colorRenderPipelineState = [self newRenderPipelineStateWithDescriptor:descriptor];
            
            // full-screen post-processing passes
            descriptor.vertexFunction = [self newFunctionWithName:@"meshLightingVertex"];
            descriptor.colorAttachments[AAPLColorIndexColor].blendingEnabled = YES;
            descriptor.colorAttachments[AAPLColorIndexColor].rgbBlendOperation = MTLBlendOperationAdd;
            descriptor.colorAttachments[AAPLColorIndexColor].alphaBlendOperation = MTLBlendOperationAdd;
            descriptor.colorAttachments[AAPLColorIndexColor].sourceRGBBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[AAPLColorIndexColor].sourceAlphaBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[AAPLColorIndexColor].destinationRGBBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[AAPLColorIndexColor].destinationAlphaBlendFactor = MTLBlendFactorOne;
            
            
            descriptor.label = @"Deferred image-based lighting";
            descriptor.fragmentFunction = [self newFunctionWithName:@"meshLightingFragment"];
            _lightingPipelineState = [self newRenderPipelineStateWithDescriptor:descriptor];

            descriptor.label = @"Deferred point lighting";
            descriptor.fragmentFunction = [self newFunctionWithName:@"meshPointLightFragment"];
            _pointLightPipelineState = [self newRenderPipelineStateWithDescriptor:descriptor];

        }
        
        NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);

                
        {
            // Create a pipeline state descriptor to create a compiled pipeline state object
            MTLRenderPipelineDescriptor *renderPipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
            
            renderPipelineDescriptor.label                           = @"MyPipeline";
            
            renderPipelineDescriptor.vertexFunction                  = [self newFunctionWithName:@"vertexShader4"];
            renderPipelineDescriptor.vertexBuffers[0].mutability = MTLMutabilityImmutable;
            
            renderPipelineDescriptor.fragmentFunction                =  [self newFunctionWithName:@"fragmentShader"];
            renderPipelineDescriptor.fragmentBuffers[0].mutability = MTLMutabilityImmutable;
            
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].pixelFormat = drawablePixelFormat;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].blendingEnabled = YES;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].rgbBlendOperation = MTLBlendOperationAdd;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].alphaBlendOperation = MTLBlendOperationAdd;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].sourceRGBBlendFactor = MTLBlendFactorOne;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].sourceAlphaBlendFactor = MTLBlendFactorOne;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexColor].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].pixelFormat = MTLPixelFormatRGBA16Float;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexNormalRoughness].pixelFormat = MTLPixelFormatRGBA16Float;
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexDepth].pixelFormat = MTLPixelFormatR32Float;
            renderPipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
                                    
            _renderPipelineState = [self newRenderPipelineStateWithDescriptor:renderPipelineDescriptor];
            
            NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
            
            _atlas = new wry::atlas(2048, device);
            _font = new wry::font(build_font(*_atlas));
            
            auto img = wry::from_png_and_multiply_alpha(wry::path_for_resource("assets", "png"));
            // wry::draw_bounding_box(img);
            for (int y = 0; y != 256; y += 64) {
                for (int x = 0; x != 2048; x += 64) {
                    auto v = img.sub(y, x, 64, 64);
                    wry::draw_bounding_box(v);
                    wry::sprite s = _atlas->place(v, simd_float2{32, 32});
                    _sprites.push_back(s);
                }
            }
            {
                unsigned char a = (int) round(wry::to_sRGB(1.00f) * 255.0f);
                unsigned char b = (int) round(wry::to_sRGB(0.50f) * 255.0f);
                auto v = wry::image(2, 2);
                v(0, 0) = v(1, 1) = simd_make_uchar4(a, a, a, 255);
                v(0, 1) = v(1, 0) = simd_make_uchar4(b, b, b, 255);
                //v(0, 0) = simd_make_uchar4(  0,   0, 0, 255);
                //v(1, 0) = simd_make_uchar4(  0, 255, 0, 255);
                //v(0, 1) = simd_make_uchar4(255,   0, 0, 255);
                //v(1, 1) = simd_make_uchar4(255, 255, 0, 255);
                _checkerboard_sprite = _atlas->place(v, simd_make_float2(1.0f, 1.0f));
                
            }
        }
    }

    NSLog(@"%s:%d", __PRETTY_FUNCTION__, __LINE__);
    
    return self;
}

- (void)renderToMetalLayer:(nonnull CAMetalLayer*)metalLayer
{
    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];

    MeshUniforms mesh_uniforms;

    simd_float4x4 MeshA;
    {
        simd_float4x4 A = simd_matrix_scale(0.5f);
        simd_float4x4 B = simd_matrix_rotate(_frameNum * 0.0007*3, simd_make_float3(1.0f, 0.0f, 0.0f));
        simd_float4x4 C = simd_matrix_rotate(_frameNum * 0.0011*3, simd_make_float3(0.0f, 0.0f, 1.0f));
        MeshA = simd_mul(C, simd_mul(B, A));

    }
    
    simd_float4x4 MeshB = simd_mul(simd_matrix_translate(simd_make_float3(0.0f, -20.5f, 0.0f)),
                                   simd_matrix_scale(simd_make_float3(2.0, 2.0, 2.0)));
    

   
    float phaseOfDay = 1; //_frameNum * -0.01 + 1;
   
   
    // Render shadow map
    
    {
        
        id <MTLRenderCommandEncoder> encoder = ^ {
            MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor new];
            descriptor.depthAttachment.loadAction = MTLLoadActionClear;
            descriptor.depthAttachment.storeAction = MTLStoreActionStore;
            descriptor.depthAttachment.clearDepth = 1.0;
            descriptor.depthAttachment.texture = self->_shadowRenderTargetTexture;
            return [commandBuffer renderCommandEncoderWithDescriptor:descriptor];
        } ();
        
        simd_float4x4 A = simd_matrix_rotate(-M_PI/2, simd_make_float3(1,0,0));
        simd_float4x4 B = simd_matrix_rotate(phaseOfDay, simd_make_float3(0, 1, 0));
        simd_float4x4 C = {{
            { 0.5f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.5f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.1f, 0.0f },
            { 0.0f, 0.0f, 0.5f, 1.0f },
        }};
        
        mesh_uniforms.viewprojection_transform = simd_mul(C, simd_mul(B, A));
        mesh_uniforms.light_direction = simd_normalize(simd_inverse(mesh_uniforms.viewprojection_transform).columns[2].xyz);
        mesh_uniforms.radiance = sqrt(simd_saturate(cos(phaseOfDay)));
        
        [encoder setRenderPipelineState:_shadowRenderPipelineState];
        [encoder setCullMode:MTLCullModeFront];
        [encoder setDepthStencilState:_enabledDepthStencilState];
        [encoder setVertexBuffer:_cube_buffer
                          offset:0
                         atIndex:AAPLBufferIndexVertices];
        
        mesh_uniforms.model_transform = MeshA;
        [encoder setVertexBytes:&mesh_uniforms
                         length:sizeof(MeshUniforms)
                        atIndex:AAPLBufferIndexUniforms];
        
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:_mesh_count];
        
        mesh_uniforms.model_transform = MeshB;
        [encoder setVertexBytes:&mesh_uniforms
                         length:sizeof(MeshUniforms)
                        atIndex:AAPLBufferIndexUniforms];
        
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:_mesh_count];
        
        [encoder endEncoding];
        
    }
    
    
    
    id<CAMetalDrawable> currentDrawable = [metalLayer nextDrawable];

    {
        // Deferred render pass
        
        id <MTLRenderCommandEncoder> encoder = ^ {
            
            MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor new];
            
            descriptor.colorAttachments[AAPLColorIndexColor].clearColor = MTLClearColorMake(0, 0, 0, 0);
            descriptor.colorAttachments[AAPLColorIndexColor].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexColor].storeAction = MTLStoreActionStore;
            descriptor.colorAttachments[AAPLColorIndexColor].texture = currentDrawable.texture;
            
            descriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].clearColor = MTLClearColorMake(0, 0, 0, 0);
            descriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].storeAction = MTLStoreActionDontCare;
            descriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].texture = self->_albedoMetallicRenderTargetTexture;
            
            descriptor.colorAttachments[AAPLColorIndexNormalRoughness].clearColor = MTLClearColorMake(0, 0, 0, 0);
            descriptor.colorAttachments[AAPLColorIndexNormalRoughness].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexNormalRoughness].storeAction = MTLStoreActionDontCare;
            descriptor.colorAttachments[AAPLColorIndexNormalRoughness].texture = self->_normalRoughnessRenderTargetTexture;
            
            descriptor.colorAttachments[AAPLColorIndexDepth].clearColor = MTLClearColorMake(1, 1, 1, 1);
            descriptor.colorAttachments[AAPLColorIndexDepth].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[AAPLColorIndexDepth].storeAction = MTLStoreActionDontCare;
            descriptor.colorAttachments[AAPLColorIndexDepth].texture = self->_depthAsColorRenderTargetTexture;
            
            descriptor.depthAttachment.clearDepth = 1.0;
            descriptor.depthAttachment.loadAction = MTLLoadActionClear;
            descriptor.depthAttachment.storeAction = MTLStoreActionDontCare;
            descriptor.depthAttachment.texture = self->_depthRenderTargetTexture;
            
            return [commandBuffer renderCommandEncoderWithDescriptor:descriptor];
            
        } ();
        
        {
            // view: rotate to isometric view angle
            simd_float4x4 A = simd_matrix_rotate(atan2(1, sqrt(2)), simd_make_float3(-1, 0, 0));

            float z = 4.0f;
            // view: translate camera back
            simd_float4x4 B = simd_matrix_translate(simd_make_float3(0, 0, z));
            
            // projection: perspective
            simd_float4x4 C = {{
                { 1.0f, 0.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f, 0.0f },
                { 0.0f, 0.0f, 1.0f, 1.0f },
                { 0.0f, 0.0f, -1.0f, 0.0f },
            }};
            
            // view: zoom
            simd_float4x4 D
            = simd_matrix_scale(simd_make_float3(z * _viewportSize.y / _viewportSize.x, z, 1));

            B = simd_mul(B, A);
            mesh_uniforms.origin = simd_mul(simd_inverse(B), simd_make_float4(0,0,0,1));
            D = simd_mul(D, B);
            mesh_uniforms.view_transform = D;
            mesh_uniforms.inverse_view_transform = simd_inverse(D);
            C = simd_mul(C, D);
            
            // camera world location is...
            
            {
                // map from clip coordinates to texture coordinates
                mesh_uniforms.light_viewprojection_transform
                    = simd_mul(simd_matrix_ndc_to_tc,
                               mesh_uniforms.viewprojection_transform);
                mesh_uniforms.viewprojection_transform = C;
                [encoder setVertexBuffer:_cube_buffer
                                               offset:0
                                              atIndex:AAPLBufferIndexVertices];
                [encoder setRenderPipelineState:_colorRenderPipelineState];
                
                [encoder setFragmentTexture:_cube_emissive atIndex:AAPLTextureIndexEmissive];
                [encoder setFragmentTexture:_cube_colors atIndex:AAPLTextureIndexAlbedo];
                [encoder setFragmentTexture:_cube_metallic atIndex:AAPLTextureIndexMetallic];
                [encoder setFragmentTexture:_cube_normals atIndex:AAPLTextureIndexNormal];
                [encoder setFragmentTexture:_cube_roughness atIndex:AAPLTextureIndexRoughness];
                [encoder setFragmentTexture:_shadowRenderTargetTexture atIndex:AAPLTextureIndexShadow];
                [encoder setFragmentTexture:_fresnelTexture atIndex:AAPLTextureIndexFresnel];
                [encoder setDepthStencilState:_enabledDepthStencilState];
                [encoder setCullMode:MTLCullModeBack];

                mesh_uniforms.model_transform = MeshA;
                mesh_uniforms.inverse_model_transform = simd_transpose(simd_inverse(mesh_uniforms.model_transform));
                [encoder setVertexBytes:&mesh_uniforms
                                              length:sizeof(MeshUniforms)
                                             atIndex:AAPLBufferIndexUniforms];
                [encoder setFragmentBytes:&mesh_uniforms
                                                length:sizeof(MeshUniforms)
                                               atIndex:AAPLBufferIndexUniforms];
                
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                         vertexStart:0
                                         vertexCount:_mesh_count];
                
                mesh_uniforms.model_transform = MeshB;
                mesh_uniforms.inverse_model_transform = simd_transpose(simd_inverse(mesh_uniforms.model_transform));
                [encoder setVertexBytes:&mesh_uniforms
                                              length:sizeof(MeshUniforms)
                                             atIndex:AAPLBufferIndexUniforms];
                [encoder setFragmentBytes:&mesh_uniforms
                                                length:sizeof(MeshUniforms)
                                               atIndex:AAPLBufferIndexUniforms];
                
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                         vertexStart:0
                                         vertexCount:_mesh_count];

                // G-buffers are now initialized
                                
                [encoder setVertexBuffer:_screenQuadVertexBuffer
                                               offset:0
                                              atIndex:AAPLBufferIndexVertices];
                [encoder setDepthStencilState:_disabledDepthStencilState];

                // image-based lighting passes:

                [encoder setRenderPipelineState:_lightingPipelineState];

                simd_float3x3 T[3];
                float kk[3];
                {
                    simd_float3 Y = simd_make_float3(0, 1, 0);
                    T[0] = simd_matrix3x3(simd_matrix_rotate(4.5, Y));
                    T[1] = simd_matrix3x3(simd_matrix_rotate(0, Y));
                    T[2] = simd_matrix3x3(simd_matrix_rotate(-1, Y));
                    kk[0] = pow(simd_saturate(sin(phaseOfDay+1-1)), 2);
                    kk[1] = sqrt(simd_saturate(sin(phaseOfDay+1)));
                    kk[2] = pow(simd_saturate(sin(phaseOfDay+1+2)), 2);
                }
                
                for (int i = 0; i != 3; ++i) {
                    mesh_uniforms.ibl_scale = kk[i];
                    mesh_uniforms.ibl_transform = T[i];
                    [encoder setFragmentBytes:&mesh_uniforms
                                       length:sizeof(MeshUniforms)
                                      atIndex:AAPLBufferIndexUniforms];
                    [encoder setFragmentTexture:_environments[i] atIndex:AAPLTextureIndexEnvironment];
                    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                vertexStart:0
                                vertexCount:6];
                }
                
                // shadow-casting point-light pass:
                [encoder setRenderPipelineState:_pointLightPipelineState];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                         vertexStart:0
                                         vertexCount:6];
            }
        }
    
        
        [encoder setRenderPipelineState:_renderPipelineState];
        MyUniforms uniforms;
        uniforms.position_transform = matrix_float4x4{{
            {2.0f / _viewportSize.x, 0.0f, 0.0f},
            {0.0f, -2.0f / _viewportSize.y, 0.0f, 0.0f},
            { 0.0f, 0.0f, 1.0f, 0.0f },
            {-1.0f, +1.0f, 0.0f, 1.0f},
        }};
        [encoder setVertexBytes:&uniforms
                                      length:sizeof(uniforms)
                                     atIndex:AAPLBufferIndexUniforms ];
        
        auto draw_text = [=](wry::rect<float> x, wry::string_view v) {
            
            auto valign = (_font->height + _font->ascender + _font->descender) / 2; // note descender is negative
            
            auto xy = x.a;
            xy.y += valign;
            while (v) {
                auto c = *v; ++v;
                auto q = _font->charmap.find(c);
                if (q != _font->charmap.end()) {
                    
                    if (xy.x + q->second.advance > x.b.x) {
                        xy.x = x.a.x;
                        xy.y += _font->height;
                    }
                    if (xy.y - _font->descender > x.b.y) {
                        return xy;
                    }
                    
                    wry::sprite s = q->second.sprite_;
                    _atlas->push_sprite(s + xy);
                    xy.x += q->second.advance;
                    
                } else if (c == '\n') {
                    xy.x = x.a.x;
                    xy.y += _font->height;
                }
            }
            xy.y -= valign;
            return xy;
        };
        
      
        {
            auto guard = std::unique_lock{_model->_mutex};
            float y = 1080;
            simd_float2 z;
            bool first = true;
            for (auto p = _model->_console.end(); (y >= 0) && (p != _model->_console.begin());) {
                --p;
                y -= _font->height;
                z = draw_text({0, y, 1920, 1080}, *p);
                if (first) {
                    draw_text(wry::rect<float>{z.x, z.y, 1920, 1080 }, (_frameNum & 0x40) ? "_" : " ");
                    first = false;
                }
            }
            
        }
        
     
        
        _atlas->commit(encoder);
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
            dispatch_semaphore_signal(self->_atlas->_semaphore);
        }];

        [encoder endEncoding];
        
    }
            
    [commandBuffer presentDrawable:currentDrawable];
    
    [commandBuffer commit];
    
    _frameNum++;
    
}

- (void)drawableResize:(CGSize)drawableSize
{
    _viewportSize.x = drawableSize.width;
    _viewportSize.y = drawableSize.height;
    
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
    descriptor.textureType = MTLTextureType2D;
    descriptor.width = _viewportSize.x;
    descriptor.height = _viewportSize.y;
    descriptor.pixelFormat = MTLPixelFormatDepth32Float;
    descriptor.usage = MTLTextureUsageRenderTarget;
    descriptor.resourceOptions = MTLResourceStorageModeMemoryless;
    _depthRenderTargetTexture = [_device newTextureWithDescriptor:descriptor];
    
}

@end




#if 0

Paste bin

/*
 {
 static double t = 0;
 t += 0.01;
 int i = (int) t;
 i = i % 4;
 wry::vertex v[3];
 v[0].v = _terrain_triangles[i+0];
 v[0].color = simd_uchar4{255, 255, 255, 255 };
 v[1].v = _terrain_triangles[i+1];
 v[1].color = simd_uchar4{255, 255, 255, 255 };
 v[2].v = _terrain_triangles[i+2];
 v[2].color = simd_uchar4{255, 255, 255, 255 };
 _atlas->push_triangle(v);
 }*/
/*
 {
 wry::vertex v[6];
 v[0].color = simd_uchar4{255, 255, 255, 255 };
 v[1].color = simd_uchar4{255, 255, 255, 255 };
 v[2].color = simd_uchar4{255, 255, 255, 255 };
 v[3].color = simd_uchar4{255, 255, 255, 255 };
 v[4].color = simd_uchar4{255, 255, 255, 255 };
 v[5].color = simd_uchar4{255, 255, 255, 255 };
 
 
 for (int i = 0; i != 10; ++i) {
 for (int j = 0; j != 10; ++j) {
 int x = i*64 + j*16;
 int y = i*16 + j*64;
 v[0].v.position = simd_make_float2(x   , y   );
 v[1].v.position = simd_make_float2(x+64, y+16);
 v[2].v.position = simd_make_float2(x+16, y+64);
 v[3].v.position = simd_make_float2(x+16, y+64);
 v[4].v.position = simd_make_float2(x+64, y+16);
 v[5].v.position = simd_make_float2(x+80, y+80);
 int a = wry::hash(i + j*123) & 1;
 int b = wry::hash((i+1) + j*123) & 1;
 int c = wry::hash(i + (j+1)*123) & 1;
 int d = wry::hash((i+1) + (j+1)*123) & 1;
 a = a | (b << 1) | (c << 2);
 b = (b << 1) | (c << 0) | (d << 2);
 auto f = [&](int a, wry::vertex* v) {
 switch (a) {
 case 0:
 v[0].v.texCoord = _terrain_triangles[0].texCoord;
 v[1].v.texCoord = _terrain_triangles[1].texCoord;
 v[2].v.texCoord = _terrain_triangles[2].texCoord;
 break;
 case 1:
 v[0].v.texCoord = _terrain_triangles[3].texCoord;
 v[1].v.texCoord = _terrain_triangles[1].texCoord;
 v[2].v.texCoord = _terrain_triangles[2].texCoord;
 break;
 case 2:
 v[0].v.texCoord = _terrain_triangles[2].texCoord;
 v[1].v.texCoord = _terrain_triangles[3].texCoord;
 v[2].v.texCoord = _terrain_triangles[1].texCoord;
 break;
 case 3:
 v[0].v.texCoord = _terrain_triangles[3].texCoord;
 v[1].v.texCoord = _terrain_triangles[4].texCoord;
 v[2].v.texCoord = _terrain_triangles[2].texCoord;
 break;
 case 4:
 v[0].v.texCoord = _terrain_triangles[1].texCoord;
 v[1].v.texCoord = _terrain_triangles[2].texCoord;
 v[2].v.texCoord = _terrain_triangles[3].texCoord;
 break;
 case 5:
 v[0].v.texCoord = _terrain_triangles[4].texCoord;
 v[1].v.texCoord = _terrain_triangles[2].texCoord;
 v[2].v.texCoord = _terrain_triangles[3].texCoord;
 break;
 case 6:
 v[0].v.texCoord = _terrain_triangles[2].texCoord;
 v[1].v.texCoord = _terrain_triangles[3].texCoord;
 v[2].v.texCoord = _terrain_triangles[4].texCoord;
 break;
 case 7:
 v[0].v.texCoord = _terrain_triangles[3].texCoord;
 v[1].v.texCoord = _terrain_triangles[4].texCoord;
 v[2].v.texCoord = _terrain_triangles[5].texCoord;
 break;
 default:
 v[0].v.texCoord = simd_make_float2(0, 0);
 v[1].v.texCoord = simd_make_float2(0, 0);
 v[2].v.texCoord = simd_make_float2(0, 0);
 }
 };
 f(a, v + 0);
 f(b, v + 3);
 
 
 */
/*
 v[0].v.texCoord = _terrain_triangles[k+0].texCoord;
 v[1].v.texCoord = _terrain_triangles[k+1].texCoord;
 v[2].v.texCoord = _terrain_triangles[k+2].texCoord;
 v[3].v.texCoord = _terrain_triangles[k+2].texCoord;
 v[4].v.texCoord = _terrain_triangles[k+1].texCoord;
 v[5].v.texCoord = _terrain_triangles[k+0].texCoord;
 */
/*
 _atlas->push_triangle(v + 0);
 _atlas->push_triangle(v + 3);
 }
 }
 }
 */


/*
 char buf[256];
 snprintf(buf, 256,
 "Futura Medium Condensed\n"
 "Frame number %ld\n"
 "Zwölf Boxkämpfer jagen Viktor quer über den großen Sylter Deich",
 (unsigned long) _frameNum);
 
 // now we finally need some application state, a console, which is
 // going to be an array<string>, of which you can edit the last string
 
 // blinking cursor
 draw_text(gl::rect<float>{0,0, 1024, 1024 }, (_frameNum & 0x40) ? "_" : " ");
 */




//{

// step one:
//

//{
// when we rescale, the pixels stay anchored to
// rhs and this looks bad; they aren't anchored at midscreen

// map pixels to screen space
/*
 simd_float4x4 A = {
 {
 {2.0f / _viewportSize.x, 0.0f, 0.0f, 0.0f},
 {0.0, -2.0f / _viewportSize.y, 0.0f, 0.0f},
 {0.0f, 0.0f, 1.0f, 0.0f},
 {-1.0f, +1.0f, 0.0f, 1.0f}
 }
 };
 
 
 // float c = sqrt(0.5f);
 // float s = c;
 float c = 1.0f / sqrt(3.0f);
 float s = sqrt(2.0f) / sqrt(3.0f);
 simd_float4x4 B = {
 {
 {1.0f, 0.0f, 0.0f, 0.0f},
 {0.0f, c, s, 0.0f},
 {0.0f, -s, c, 0.0f},
 {0.0f, 0.0f, 0.0f, 1.0f}
 }
 };
 
 // rotate to mix Y and Z
 
 float dz = 4.0f;
 // translate camera back
 simd_float4x4 C = {
 {
 {1.0f, 0.0f, 0.0f, 0.0f },
 {0.0f, 1.0f, 0.0f, 0.0f },
 {0.0f, 0.0f, 1.0f, 0.0f },
 {0.0f, 0.0f, dz, 1.0f },
 }
 };
 
 // perspective project
 simd_float4x4 D = {
 {
 {1.0f, 0.0f, 0.0f, 0.0f },
 {0.0f, 1.0f, 0.0f, 0.0f },
 {0.0f, 0.0f, 1.0f, 1.0f },
 {0.0f, 0.0f, 0.0f, 0.0f },
 }
 };
 
 // zoom
 simd_float4x4 E = {
 {
 {dz*sqrtf(2), 0.0f, 0.0f, 0.0f },
 {0.0f, dz*sqrtf(2), 0.0f, 0.0f },
 {0.0f, 0.0f, 1.0f, 0.0f },
 {0.0f, 0.0f, 0.0f, 1.0f },
 }
 };
 
 A = simd_mul(B, A);
 A = simd_mul(C, A);
 A = simd_mul(D, A);
 A = simd_mul(E, A);
 */
/*
 uniforms.position_transform = matrix_float4x4{{
 {40.0f / _viewportSize.x, 0.0f, 0.0f, 0.0f},
 {0.0f, -40.0f / _viewportSize.y, 0.0f, -40.0f / _viewportSize.x},
 {0.0f, 0.0f, 1.0f, 0.0f},
 {-20.0f, +20.0f, 0.0f, 40.0f} // (0, 0) -> top left
 // {0.0f, 0.0f} // (0, 0) -> center
 }};
 */
//uniforms.position_transform = A;

//}

//}

// [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];

// _atlas->push_sprite(_font->charmap['a'].sprite_ + gl::vec2{100,100});

// _atlas->push_sprite(gl::sprite{{{0,0},{0,0}},{{2048,2048},{1,1}}}+gl::vec2{256,256});
//{
/*
 simd_int2 origin;
 {
 auto guard = std::unique_lock{_model->_mutex};
 origin = simd_make_int2(_model->_yx.x, _model->_yx.y);
 }
 auto zz = origin;
 simd_int2 c;
 c.x = (origin.x) >> 6;
 c.y = (origin.y) >> 6;
 
 origin.x &= 0x0000003F;
 origin.y &= 0x0000003F;
 origin.x -= 32;
 origin.y -= 32;
 
 //NSLog(@"%d %d\n", origin.x, origin.y);
 
 
 simd_int2 b;
 b.x = 32 + (int) _viewportSize.x;
 b.y = 32 + (int) _viewportSize.y;
 */

//for (int y = origin.y, i = -c.y; y < b.y; y += 64, ++i) {
//  for (int x = origin.x, j = -c.x; x < b.x; x += 64, ++j) {
//    auto s = _sprites[_model->_world(simd_make_int2(i, j)).x];
/*
 printf("%f %f %f %f\n",
 s.a.position.x,
 s.a.position.y,
 s.a.position.z,
 s.a.position.w);
 */

//     _atlas->push_sprite(s + simd_make_float2(x, y));
//_atlas->push_sprite(_sprites[48] + simd_make_float2(x, y));
//}
//}

//_atlas->push_sprite(_cube_sprites[0] + simd_make_float2(512+zz.x, 512+zz.y));

// now laboriously draw cube

/*
 {
 auto o = simd_make_float4(512+zz.x, 512+zz.y, 0, 0);
 // simd_make_float2(512+zz.x, 512+zz.y)
 wry::vertex v[4];
 float h = -128.0f / _viewportSize.y;
 v[0].v.position = simd_make_float4(-32.0f, -32.0f, 0.0f, 1.0f);
 v[1].v.position = simd_make_float4(-32.0f, +32.0f, 0.0f, 1.0f);
 v[2].v.position = simd_make_float4(-32.0f, -32.0f, h, 1.0f);
 v[3].v.position = simd_make_float4(-32.0f, +32.0f, h, 1.0f);
 v[0].color = simd_make_uchar4(255, 255, 255, 255);
 v[1].color = simd_make_uchar4(255, 255, 255, 255);
 v[2].color = simd_make_uchar4(255, 255, 255, 255);
 v[3].color = simd_make_uchar4(255, 255, 255, 255);
 
 {
 simd_float2 a = _cube_sprites[1].a.texCoord;
 simd_float2 b = _cube_sprites[1].b.texCoord;
 a += 96.0f / 2048;
 b -= 96.0f / 2048;
 auto c = simd_make_float2(a.x, b.y);
 auto d = simd_make_float2(b.x, a.y);
 v[0].v.texCoord = b;
 v[1].v.texCoord = c;
 v[2].v.texCoord = d;
 v[3].v.texCoord = a;
 }
 
 
 {
 simd_float4x4 R = {{
 { 0.0f, -1.0f, 0.0f, 0.0f},
 { 1.0f, 0.0f, 0.0f, 0.0f},
 { 0.0f, 0.0f, 1.0f, 0.0f},
 { 0.0f, 0.0f, 0.0f, 1.0f},
 }};
 for (int j = 0; j != 4; ++j) {
 wry::vertex u[4];
 for (int i = 0; i != 4; ++i) {
 u[i] = v[i];
 u[i].v.position += o;
 }
 _atlas->push_quad(u);
 
 for (int i = 0; i != 4; ++i) {
 v[i].v.position = simd_mul(R, v[i].v.position);
 }
 
 }
 }
 
 // finally, top of cube
 v[0].v.position = simd_make_float4(-32.0f, -32.0f, h, 1.0f);
 v[1].v.position = simd_make_float4(-32.0f, +32.0f, h, 1.0f);
 v[2].v.position = simd_make_float4(+32.0f, -32.0f, h, 1.0f);
 v[3].v.position = simd_make_float4(+32.0f, +32.0f, h, 1.0f);
 {
 auto a = (_cube_sprites[0].a.texCoord + _cube_sprites[0].b.texCoord) / 2;
 for (int i = 0; i != 4; ++i) {
 v[i].v.position += o;
 v[i].v.texCoord = a;
 }
 }
 _atlas->push_quad(v);
 
 }
 */


/*
 simd_int2 a = {-32, -32}, b;
 a.x += (int)yx.x;
 a.y += (int)yx.y;
 b.x = a.x + (int)_viewportSize.x;
 b.y = a.y + (int)_viewportSize.y;
 
 int kk = 0;
 for (int y = a.y; y < b.y; y += 64) {
 for (int x = a.x; x < b.x; x += 64) {
 _atlas->push_sprite(_sprites[4] + gl::vec2{x, y} + yx);
 }
 }
 */
//}


/*
 NSError *error = nil;
 MTKTextureLoader *loader = [[MTKTextureLoader alloc] initWithDevice: _device];
 
 NSURL* url = [NSURL fileURLWithPath:@"/Users/antony/Downloads/dawn.png"];
 
 _big_sky = [loader newTextureWithContentsOfURL:url options:nil error:&error];
 
 NSLog(@"%lu %lu\n", (unsigned long)_big_sky.width, (unsigned long)_big_sky.height);
 
 if(error)
 {
 NSLog(@"Error creating the texture from %@: %@", url.absoluteString, error.localizedDescription);
 return nil;
 }
 */


{
    std::size_t n = 256;
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
    descriptor.textureType = MTLTextureTypeCube;
    descriptor.pixelFormat = MTLPixelFormatRGBA32Float;
    descriptor.width = n;
    descriptor.height = n;
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.resourceOptions = MTLResourceStorageModeShared;
    _environmentMapFiltered = [_device newTextureWithDescriptor:descriptor];
    _environmentMapFiltered.label = @"Environment map";
    
    
    // simple env texture
    
    
    wry::matrix<simd_float4> a(n, n);
    
    auto foo = [&](NSUInteger i) {
        [_environmentMapFiltered replaceRegion:MTLRegionMake2D(0, 0, n, n)
                                   mipmapLevel:0
                                         slice:i
                                     withBytes:a.data()
                                   bytesPerRow:a.stride() * sizeof(simd_float4)
                                 bytesPerImage:0];
    };
    
    // dark ground
    a = simd_make_float4(0.25, 0.125, 0.0625, 0);
    foo(3);
    // bright sky
    a = simd_make_float4(0.25, 0.5, 1, 0);
    foo(2);
    // ground
    a.sub(n/2, 0, n/2, n) = simd_make_float4(0.5, 0.25, 0.125, 0);
    // feature
    a.sub(3*n/8, n/2, n/4, n/4) = simd_make_float4(1, 1, 1, 0) * 0.125;
    // lamp
    a.sub(n/8, n/8, n/2, n/16) = simd_make_float4(1, 1, 1, 1) * 10;
    foo(0);
    a.sub(n/8, n/8, n/2, n/16) = simd_make_float4(0, 1, 1, 0) * 10;
    foo(1);
    a.sub(n/8, n/8, n/2, n/16) = simd_make_float4(1, 0, 1, 0) * 10;
    foo(4);
    a.sub(n/8, n/8, n/2, n/16) = simd_make_float4(1, 1, 0, 0) * 10;
    foo(5);
    
}

#endif
