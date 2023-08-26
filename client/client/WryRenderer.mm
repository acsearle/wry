//
//  WryRenderer.mm
//  client
//
//  Created by Antony Searle on 1/7/2023.
//

#include <random>

#include "WryRenderer.h"
#include "ShaderTypes.h"

#include "mesh.hpp"
#include "atlas.hpp"
#include "font.hpp"
#include "platform.hpp"

@implementation WryRenderer
{
    id <MTLDevice> _device;
    id <MTLCommandQueue> _commandQueue;
    
    simd_uint2 _viewportSize;
    
    // shadow map pass

    MTLRenderPassDescriptor* _shadowRenderPassDescriptor;
    id<MTLTexture> _shadowRenderTargetTexture;
    
    id <MTLRenderPipelineState> _shadowRenderPipelineState;
    id <MTLDepthStencilState> _shadowDepthStencilState;
    id <MTLDepthStencilState> _ignoreDepthStencilState;

    
    // screen pass
    
    MTLRenderPassDescriptor* _screenRenderPassDescriptor;
    
    // gbuffers (memoryless?)
    
    id<MTLTexture> _depthRenderTargetTexture;

    id<MTLTexture> _colorRenderTargetTexture; // basic, used for accumulating emissives
    id<MTLTexture> _albedoMetallicRenderTargetTexture;
    id<MTLTexture> _normalRoughnessRenderTargetTexture;
    id<MTLTexture> _depthAsColorRenderTargetTexture;
    
    id <MTLRenderPipelineState> _colorRenderPipelineState;

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
    
    int _mesh_count;
    
    id<MTLTexture> _environmentMap;
    id<MTLTexture> _environmentMapRenderTarget;
    id<MTLTexture> _environmentMapFiltered;

    id<MTLTexture> _brdfLUTtexture;

    id <MTLRenderPipelineState> _lightingPipelineState;
    id <MTLRenderPipelineState> _pointLightPipelineState;

    
    id<MTLBuffer> _screenQuadVertexBuffer;


}

-(void) dealloc {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (nonnull instancetype)initWithMetalDevice:(nonnull id<MTLDevice>)device
                        drawablePixelFormat:(MTLPixelFormat)drawablePixelFormat
                                      model:(std::shared_ptr<wry::model>)model_
{
    
    if ((self = [super init])) {
        
        _model = model_;
        
        _frameNum = 0;
        
        _device = device;
        id<MTLLibrary> shaderLib = [_device newDefaultLibrary];

        _commandQueue = [_device newCommandQueue];
        
        
        {
            // preparatory rendering
            
            // create a high dynamic range environment map
            
            // filter the environment map for various roughnesses
            
            // we do this by explicit weighted summation over the input map
            // for each pixel of the output map (aka, an n^4 operation)
            
            // this produces excellent quality but the fragment shader times
            // out for sizes > 128
            
            // we slice up the calculation by image rows and use
            // instances with id = face + y * 6
            
            
            {
                std::size_t n = 256;
                MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
                descriptor.textureType = MTLTextureTypeCube;
                descriptor.pixelFormat = MTLPixelFormatRGBA32Float;
                descriptor.width = n;
                descriptor.height = n;
                descriptor.usage = MTLTextureUsageShaderRead;
                descriptor.resourceOptions = MTLResourceStorageModeShared;
                _environmentMap = [_device newTextureWithDescriptor:descriptor];
                _environmentMap.label = @"Environment map";

                // simple env texture
                wry::matrix<simd_float4> a(n, n);
                a = simd_make_float4(0, 0, 0, 0);
                a.sub(0, 0, n/2, n) = simd_make_float4(0.25, 0.5, 1, 0);
                a.sub(n/2, 0, n/2, n) = simd_make_float4(0.5, 0.25, 0.125, 0);
                a.sub(n/8, n/8, n/2, n/16) = simd_make_float4(4, 3, 2, 0) * 4;
                for (int i = 0; i != 6; ++i) {
                    [_environmentMap replaceRegion:MTLRegionMake2D(0, 0, n, n)
                                       mipmapLevel:0
                                             slice:i
                                         withBytes:a.data()
                                       bytesPerRow:a.stride() * sizeof(simd_float4)
                                     bytesPerImage:0];
                }
            }
            
            {
                MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
                descriptor.textureType = MTLTextureTypeCube;
                descriptor.width = _environmentMap.width;
                descriptor.height = _environmentMap.height;
                descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
                descriptor.mipmapLevelCount = 5; // 0.0625, 0.125, 0.25, 0.5, 1
                descriptor.usage = MTLTextureUsageShaderRead;
                descriptor.resourceOptions = MTLResourceStorageModePrivate;
                _environmentMapFiltered = [_device newTextureWithDescriptor:descriptor];
                _environmentMapFiltered.label = @"Environment map filtered";
            }
            
            MTLRenderPassDescriptor* renderPassAccumulate = [MTLRenderPassDescriptor new];
            renderPassAccumulate.colorAttachments[0].loadAction = MTLLoadActionClear;
            renderPassAccumulate.colorAttachments[0].storeAction = MTLStoreActionStore;
            renderPassAccumulate.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
            renderPassAccumulate.renderTargetArrayLength = 6;

            MTLRenderPassDescriptor* renderPassNormalize = [MTLRenderPassDescriptor new];
            renderPassNormalize.colorAttachments[0].loadAction = MTLLoadActionDontCare;
            renderPassNormalize.colorAttachments[0].storeAction = MTLStoreActionStore;
            renderPassNormalize.renderTargetArrayLength = 6;

            id<MTLRenderPipelineState> pipelineStateAccumulate;
            {
                MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
                descriptor.label = @"Cube filter accumulate";
                descriptor.vertexFunction = [shaderLib newFunctionWithName:@"cubeFilterVertex"];
                descriptor.fragmentFunction = [shaderLib newFunctionWithName:@"cubeFilterAccumulate"];
                descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
                descriptor.colorAttachments[0].blendingEnabled = YES;
                descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
                descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
                descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
                descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
                descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
                descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
                descriptor.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
                NSError* error = nil;
                pipelineStateAccumulate = [_device newRenderPipelineStateWithDescriptor:descriptor
                                                                        error:&error];
                if(!pipelineStateAccumulate) {
                    NSLog(@"ERROR: Failed aquiring pipeline state: %@", error);
                    exit(EXIT_FAILURE);
                }
                
            }
            
            id<MTLRenderPipelineState> pipelineStateNormalize;
            {
                MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
                descriptor.label = @"Cube filter normalize";
                descriptor.vertexFunction = [shaderLib newFunctionWithName:@"cubeFilterVertex"];
                descriptor.fragmentFunction = [shaderLib newFunctionWithName:@"cubeFilterNormalize"];
                descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
                descriptor.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
                NSError* error = nil;
                pipelineStateNormalize = [_device newRenderPipelineStateWithDescriptor:descriptor
                                                           error:&error];
                if(!pipelineStateNormalize) {
                    NSLog(@"ERROR: Failed aquiring pipeline state: %@", error);
                    exit(EXIT_FAILURE);
                }
                
            }
            
            NSUInteger nvertices;
            {
                wry::array<simd_float4> a = wry::mesh::clip_space_quad();
                nvertices = a.size();
                _screenQuadVertexBuffer = [_device newBufferWithBytes:a.data()
                                                    length:a.size()*sizeof(simd_float4) options:MTLResourceStorageModeShared];
            }
            
            cubeFilterUniforms uniforms;
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
                = simd_mul(simd_matrix_rotation(M_PI_2, simd_make_float3(0.0f, 1.0f, 0.0f)), Z);
                uniforms.transforms[1]
                = simd_mul(simd_matrix_rotation(-M_PI_2, simd_make_float3(0.0f, 1.0f, 0.0f)), Z);
                uniforms.transforms[2]
                = simd_mul(simd_matrix_rotation(M_PI_2, simd_make_float3(-1.0f, 0.0f, 0.0f)), Z);
                uniforms.transforms[3]
                = simd_mul(simd_matrix_rotation(-M_PI_2, simd_make_float3(-1.0f, 0.0f, 0.0f)), Z);
                uniforms.transforms[4] = Z;
                uniforms.transforms[5]
                = simd_mul(simd_matrix_rotation(M_PI, simd_make_float3(0.0f, 1.0f, 0.0f)), Z);
            }
            
            id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
            
            for (NSUInteger level = 0; level != _environmentMapFiltered.mipmapLevelCount; ++level) {

                id<MTLTexture> renderTargetAccumulate;
                {
                    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
                    descriptor.textureType = MTLTextureTypeCube;
                    descriptor.pixelFormat = MTLPixelFormatRGBA32Float;
                    descriptor.width = _environmentMapFiltered.width >> level;
                    descriptor.height = _environmentMapFiltered.height >> level;
                    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
                    descriptor.resourceOptions = MTLResourceStorageModePrivate;
                    renderTargetAccumulate = [_device newTextureWithDescriptor:descriptor];
                    renderTargetAccumulate.label = @"Cube filter accumulate";
                }
                
                renderPassAccumulate.colorAttachments[0].texture = renderTargetAccumulate;
                
                id<MTLRenderCommandEncoder> accumulateEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassAccumulate];
                uniforms.alpha = pow(2.0,
                                     1.0 + level - _environmentMapFiltered.mipmapLevelCount);
                // 1.0, 0.5, 0.25, 0.125, 0.0625
                [accumulateEncoder setRenderPipelineState:pipelineStateAccumulate];
                [accumulateEncoder setVertexBuffer:_screenQuadVertexBuffer
                                               offset:0
                                              atIndex:AAPLBufferIndexVertices];
                [accumulateEncoder setVertexBytes:&uniforms
                                              length:sizeof(cubeFilterUniforms)
                                             atIndex:AAPLBufferIndexUniforms];
                [accumulateEncoder setFragmentBytes:&uniforms
                                                length:sizeof(cubeFilterUniforms)
                                               atIndex:AAPLBufferIndexUniforms];
                [accumulateEncoder setFragmentTexture:_environmentMap
                                                 atIndex:AAPLTextureIndexColor];
                [accumulateEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                                         vertexStart:0
                                         vertexCount:nvertices
                                       instanceCount:6*_environmentMap.height];
                [accumulateEncoder endEncoding];
                
                id<MTLTexture> renderTargetNormalize;
                {
                    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
                    descriptor.textureType = MTLTextureTypeCube;
                    descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
                    descriptor.width = renderTargetAccumulate.width;
                    descriptor.height = renderTargetAccumulate.width;
                    descriptor.usage = MTLTextureUsageRenderTarget;
                    descriptor.resourceOptions = MTLResourceStorageModePrivate;
                    renderTargetNormalize = [_device newTextureWithDescriptor:descriptor];
                    renderTargetNormalize.label = @"Cube filter normalize";
                }
                
                renderPassNormalize.colorAttachments[0].texture = renderTargetNormalize;
                id<MTLRenderCommandEncoder> normalizeEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassNormalize];
                [normalizeEncoder setRenderPipelineState:pipelineStateNormalize];
                [normalizeEncoder setVertexBuffer:_screenQuadVertexBuffer
                                            offset:0
                                           atIndex:AAPLBufferIndexVertices];
                [normalizeEncoder setVertexBytes:&uniforms
                                           length:sizeof(cubeFilterUniforms)
                                          atIndex:AAPLBufferIndexUniforms];
                [normalizeEncoder setFragmentBytes:&uniforms
                                             length:sizeof(cubeFilterUniforms)
                                            atIndex:AAPLBufferIndexUniforms];
                [normalizeEncoder setFragmentTexture:renderTargetAccumulate
                                              atIndex:AAPLTextureIndexColor];
                [normalizeEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                                      vertexStart:0
                                      vertexCount:nvertices
                                    instanceCount:6];
                [normalizeEncoder endEncoding];
                
                
                id<MTLBlitCommandEncoder> blitCommandEncoder = [commandBuffer blitCommandEncoder];
                [blitCommandEncoder copyFromTexture:renderTargetNormalize
                                        sourceSlice:0
                                        sourceLevel:0
                                          toTexture:_environmentMapFiltered
                                   destinationSlice:0
                                   destinationLevel:level
                                         sliceCount:6
                                         levelCount:1 ];
                [blitCommandEncoder endEncoding];
            }

            [commandBuffer commit];
        }

        // render sun shadow map to texture
        _shadowRenderPassDescriptor = [MTLRenderPassDescriptor new];
        _shadowRenderPassDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
        _shadowRenderPassDescriptor.depthAttachment.storeAction = MTLStoreActionStore;
        _shadowRenderPassDescriptor.depthAttachment.clearDepth = 1.0;

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
            MTLDepthStencilDescriptor* descriptor = [MTLDepthStencilDescriptor new];
            descriptor.depthCompareFunction = MTLCompareFunctionLess;
            descriptor.depthWriteEnabled = YES;
            descriptor.label = @"Shadow map depth/stencil state";
            _shadowDepthStencilState = [_device newDepthStencilStateWithDescriptor:descriptor];
            
            descriptor.depthCompareFunction = MTLCompareFunctionAlways;
            descriptor.depthWriteEnabled = NO;
            _ignoreDepthStencilState = [_device newDepthStencilStateWithDescriptor:descriptor];
            
            
        }
        
        {
            MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
            descriptor.label = @"Shadow map pipeline";
            descriptor.vertexFunction = [shaderLib newFunctionWithName:@"meshVertexShader"];
            descriptor.vertexBuffers[AAPLBufferIndexVertices].mutability = MTLMutabilityImmutable;
            descriptor.vertexBuffers[AAPLBufferIndexUniforms].mutability = MTLMutabilityImmutable;
            descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            NSError* error = nil;
            _shadowRenderPipelineState = [_device newRenderPipelineStateWithDescriptor:descriptor
                                                                                 error:&error];
            if(!_shadowRenderPipelineState) {
                NSLog(@"ERROR: Failed aquiring pipeline state: %@", error);
                exit(EXIT_FAILURE);
            }
        }

        
        
        // render to screen and associated buffers
        _screenRenderPassDescriptor = [MTLRenderPassDescriptor new];
        
        _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexColor].loadAction = MTLLoadActionDontCare;
        _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexColor].storeAction = MTLStoreActionStore;
        _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexColor].clearColor
        // = MTLClearColorMake(0.5, 0.25, 0.125, 1);
        = MTLClearColorMake(0, 0, 0, 0);
        
        _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].loadAction = MTLLoadActionDontCare;
        _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].storeAction = MTLStoreActionDontCare;
        _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].clearColor = MTLClearColorMake(0, 0, 0, 0);

        _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexNormalRoughness].loadAction = MTLLoadActionDontCare;
        _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexNormalRoughness].storeAction = MTLStoreActionDontCare;
        _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexNormalRoughness].clearColor = MTLClearColorMake(0, 0, 0, 0);

        _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexDepthAsColor].loadAction = MTLLoadActionDontCare;
        _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexDepthAsColor].storeAction = MTLStoreActionDontCare;
        _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexDepthAsColor].clearColor = MTLClearColorMake(1, 1, 1, 1);

        _screenRenderPassDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
        _screenRenderPassDescriptor.depthAttachment.storeAction = MTLStoreActionDontCare;
        _screenRenderPassDescriptor.depthAttachment.clearDepth = 1.0;
        
                        
        {
            
            
                        
           
            
            // auto v = wry::mesh::prism(16);
            auto v = wry::mesh::icosahedron();
            _mesh_count = (int) v.size();

            {
              
                auto length = sizeof(MeshVertex)*v.size();
                _cube_buffer = [_device newBufferWithLength:length options:MTLResourceStorageModeShared];
                std::memcpy(_cube_buffer.contents, v.data(), length);

            }
            
            {
                
                // wry::image a(4,4);
                wry::image a;
                a = wry::from_png(wry::path_for_resource("rustediron2_basecolor", "png"));
                //a = wry::from_png(wry::path_for_resource("steelplate1_albedo", "png"));
                
                {
                    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
                    descriptor.textureType = MTLTextureType2D;
                    descriptor.width = a.width();
                    descriptor.height = a.height();
                    descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm_sRGB;
                    descriptor.usage = MTLTextureUsageShaderRead;
                    descriptor.resourceOptions = MTLResourceStorageModeShared;
                    _cube_colors = [_device newTextureWithDescriptor:descriptor];
                    [_cube_colors replaceRegion:MTLRegionMake2D(0, 0, a.width(), a.height())
                                    mipmapLevel:0
                                      withBytes:a.data()
                                    bytesPerRow:a.stride() * 4];
                    _cube_colors.label = @"BaseColor";
                }
                
                a = wry::from_png(wry::path_for_resource("rustediron2_normal", "png"));
                //a = wry::from_png(wry::path_for_resource("steelplate1_normal-ogl", "png"));
                

                {
                    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
                    descriptor.textureType = MTLTextureType2D;
                    descriptor.width = a.width();
                    descriptor.height = a.height();
                    descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
                    descriptor.usage = MTLTextureUsageShaderRead;
                    descriptor.resourceOptions = MTLResourceStorageModeShared;
                    _cube_normals = [_device newTextureWithDescriptor:descriptor];
                    [_cube_normals replaceRegion:MTLRegionMake2D(0, 0, a.width(), a.height())
                                      mipmapLevel:0
                                        withBytes:a.data()
                                      bytesPerRow:a.stride() * 4];
                }
               
                a = wry::from_png(wry::path_for_resource("rustediron2_roughness", "png"));
                //a = wry::from_png(wry::path_for_resource("steelplate1_roughness", "png"));
                
                {
                    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
                    descriptor.textureType = MTLTextureType2D;
                    descriptor.width = a.width();
                    descriptor.height = a.height();
                    descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm_sRGB;
                    descriptor.usage = MTLTextureUsageShaderRead;
                    descriptor.resourceOptions = MTLResourceStorageModeShared;
                    _cube_roughness = [_device newTextureWithDescriptor:descriptor];
                    [_cube_roughness replaceRegion:MTLRegionMake2D(0, 0, a.width(), a.height())
                                     mipmapLevel:0
                                       withBytes:a.data()
                                     bytesPerRow:a.stride() * 4];
                }
                
                a = wry::from_png(wry::path_for_resource("rustediron2_metallic", "png"));
                //a = wry::from_png(wry::path_for_resource("steelplate1_metallic", "png"));
                
                {
                    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
                    descriptor.textureType = MTLTextureType2D;
                    descriptor.width = a.width();
                    descriptor.height = a.height();
                    descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm_sRGB;
                    descriptor.usage = MTLTextureUsageShaderRead;
                    descriptor.resourceOptions = MTLResourceStorageModeShared;
                    _cube_metallic = [_device newTextureWithDescriptor:descriptor];
                    [_cube_metallic replaceRegion:MTLRegionMake2D(0, 0, a.width(), a.height())
                                       mipmapLevel:0
                                         withBytes:a.data()
                                       bytesPerRow:a.stride() * 4];
                }
            
            }
            
            
            {
                /*
                wry::image a(16, 16);
                for (int i = 0; i != 16; ++i)
                    for (int j = 0; j != 16; ++j) {
                        char c = (rand() & 31) + 64;
                        a(i, j) = simd_make_uchar4(c, c, c, 255);
                    }
                
                {
                    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
                    descriptor.textureType = MTLTextureType2D;
                    descriptor.width = a.width();
                    descriptor.height = a.height();
                    descriptor.pixelFormat = MTLPixelFormatRGBA8Snorm;
                    descriptor.usage = MTLTextureUsageShaderRead;
                    descriptor.resourceOptions = MTLResourceStorageModeShared;
                    _cube_colors = [_device newTextureWithDescriptor:descriptor];
                    [_cube_colors replaceRegion:MTLRegionMake2D(0, 0, a.width(), a.height())
                                     mipmapLevel:0
                                       withBytes:a.data()
                                     bytesPerRow:a.stride() * 4];
                }
                 */
            }
            
        }
        
        {
            // main depth buffer
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
            descriptor.textureType = MTLTextureType2D;
            descriptor.width = 1920; // todo resize awareness
            descriptor.height = 1080;
            descriptor.pixelFormat = MTLPixelFormatDepth32Float;
            descriptor.usage = MTLTextureUsageRenderTarget;
            descriptor.resourceOptions = MTLResourceStorageModeMemoryless;
            _depthRenderTargetTexture = [_device newTextureWithDescriptor:descriptor];
            _depthRenderTargetTexture.label = @"Depth";
            
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

        }
        
        
        {
            MTLRenderPipelineDescriptor *descriptor = [[MTLRenderPipelineDescriptor alloc] init];
            
            descriptor.label                           = @"Mesh to gbuffer";
            
            descriptor.vertexFunction                  = [shaderLib newFunctionWithName:@"meshVertexShader"];
            descriptor.vertexBuffers[AAPLBufferIndexUniforms].mutability = MTLMutabilityImmutable;
            descriptor.vertexBuffers[AAPLBufferIndexVertices].mutability = MTLMutabilityImmutable;

            descriptor.fragmentFunction                = [shaderLib newFunctionWithName:@"meshGbufferFragment"];
            descriptor.fragmentBuffers[AAPLBufferIndexUniforms].mutability = MTLMutabilityImmutable;
            descriptor.fragmentBuffers[AAPLBufferIndexVertices].mutability = MTLMutabilityImmutable;

            descriptor.colorAttachments[AAPLColorIndexColor].pixelFormat = drawablePixelFormat;
            descriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].pixelFormat = MTLPixelFormatRGBA16Float;
            descriptor.colorAttachments[AAPLColorIndexNormalRoughness].pixelFormat = MTLPixelFormatRGBA16Float;
            descriptor.colorAttachments[AAPLColorIndexDepthAsColor].pixelFormat = MTLPixelFormatR32Float;
            descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

            NSError *error;
            _colorRenderPipelineState = [_device newRenderPipelineStateWithDescriptor:descriptor
                                                                                error:&error];
            if(!_colorRenderPipelineState) {
                NSLog(@"ERROR: Failed aquiring pipeline state: %@", error);
                exit(EXIT_FAILURE);
            }

            
            descriptor.colorAttachments[AAPLColorIndexColor].blendingEnabled = YES;
            descriptor.colorAttachments[AAPLColorIndexColor].rgbBlendOperation = MTLBlendOperationAdd;
            descriptor.colorAttachments[AAPLColorIndexColor].alphaBlendOperation = MTLBlendOperationAdd;
            descriptor.colorAttachments[AAPLColorIndexColor].sourceRGBBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[AAPLColorIndexColor].sourceAlphaBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[AAPLColorIndexColor].destinationRGBBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[AAPLColorIndexColor].destinationAlphaBlendFactor = MTLBlendFactorOne;

            
            descriptor.label = @"Deferred image-based lighting";
            descriptor.vertexFunction = [shaderLib newFunctionWithName:@"meshLightingVertex"];
            descriptor.fragmentFunction = [shaderLib newFunctionWithName:@"meshLightingFragment"];

            _lightingPipelineState = [_device newRenderPipelineStateWithDescriptor:descriptor
                                                                                error:&error];
            if(!_lightingPipelineState) {
                NSLog(@"ERROR: Failed aquiring pipeline state: %@", error);
                exit(EXIT_FAILURE);
            }

            descriptor.label = @"Deferred point lighting";
            descriptor.fragmentFunction = [shaderLib newFunctionWithName:@"meshPointLightFragment"];
            
            _pointLightPipelineState = [_device newRenderPipelineStateWithDescriptor:descriptor
                                                                               error:&error];
            if(!_pointLightPipelineState) {
                NSLog(@"ERROR: Failed aquiring pipeline state: %@", error);
                exit(EXIT_FAILURE);
            }

        }
                
        {
            
            id <MTLFunction> vertexProgram = [shaderLib newFunctionWithName:@"vertexShader"];
            if(!vertexProgram)
            {
                NSLog(@" ERROR: Couldn't load vertex function from default library");
                return nil;
            }
            
            id <MTLFunction> fragmentProgram = [shaderLib newFunctionWithName:@"fragmentShader"];
            if(!fragmentProgram)
            {
                NSLog(@" ERROR: Couldn't load fragment function from default library");
                return nil;
            }

            id <MTLFunction> vertexProgram4 = [shaderLib newFunctionWithName:@"vertexShader4"];
            if(!vertexProgram4)
            {
                NSLog(@" ERROR: Couldn't load vertex function from default library");
                return nil;
            }

                        
            // Create a pipeline state descriptor to create a compiled pipeline state object
            MTLRenderPipelineDescriptor *renderPipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
            
            renderPipelineDescriptor.label                           = @"MyPipeline";
            
            renderPipelineDescriptor.vertexFunction                  = vertexProgram4;
            renderPipelineDescriptor.vertexBuffers[0].mutability = MTLMutabilityImmutable;
            
            renderPipelineDescriptor.fragmentFunction                = fragmentProgram;
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
            renderPipelineDescriptor.colorAttachments[AAPLColorIndexDepthAsColor].pixelFormat = MTLPixelFormatR32Float;
            renderPipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
                                    
            NSError *error;
            _renderPipelineState = [_device newRenderPipelineStateWithDescriptor:renderPipelineDescriptor
                                                                           error:&error];
            if(!_renderPipelineState)
            {
                NSLog(@"ERROR: Failed aquiring pipeline state: %@", error);
                return nil;
            }
            
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
    
    wry::mesh::prism(4);
    
    return self;
}

- (void)renderToMetalLayer:(nonnull CAMetalLayer*)metalLayer
{
    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];

    MeshUniforms mesh_uniforms;

    simd_float4x4 MeshA;
    {
        simd_float4x4 A = simd_matrix_scale(0.5f);
        simd_float4x4 B = simd_matrix_rotation(_frameNum * 0.007, simd_make_float3(1.0f, 0.0f, 0.0f));
        simd_float4x4 C = simd_matrix_rotation(_frameNum * 0.011, simd_make_float3(0.0f, 0.0f, 1.0f));
        MeshA = simd_mul(C, simd_mul(B, A));

    }
    
    simd_float4x4 MeshB = simd_mul(simd_matrix_translation(simd_make_float3(0.0f, -2.5f, 0.0f)),
                                   simd_matrix_scale(simd_make_float3(2.0, 2.0, 2.0)));
    
    
    _shadowRenderPassDescriptor.depthAttachment.texture = _shadowRenderTargetTexture;
    id <MTLRenderCommandEncoder> shadowRenderCommandEncoder
    = [commandBuffer renderCommandEncoderWithDescriptor:_shadowRenderPassDescriptor];
    {
        
        
        // view: rotate to isometric view angle
        float c = 1.0f / sqrt(3.0f);
        float s = sqrt(2.0f) / sqrt(3.0f);
        //simd_float4x4 A = {{
        //    { 1.0f, 0.0f, 0.0f, 0.0f },
        //    { 0.0f,    c,    -s, 0.0f },
        //    { 0.0f,   s,    c, 0.0f },
        //    { 0.0f, 0.0f, 0.0f, 1.0f },
        //}};
        simd_float4x4 A = simd_matrix_rotation(-M_PI/2, simd_make_float3(1,0,0));
        //c = 1.0f / sqrt(2.0f);
        //s = c;
        c = cos(_frameNum * 0.003 + 1);
        s = sin(_frameNum * 0.003 + 1);
        simd_float4x4 B = simd_matrix_rotation(_frameNum * -0.01 + 1, simd_make_float3(0, 1, 0));
        simd_float4x4 C = {{
            { 0.5f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.5f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.1f, 0.0f },
            { 0.0f, 0.0f, 0.5f, 1.0f },
        }};
        
        mesh_uniforms.viewprojection_transform = simd_mul(C, simd_mul(B, A));
        mesh_uniforms.light_direction = simd_normalize(simd_inverse(mesh_uniforms.viewprojection_transform).columns[2].xyz);

        [shadowRenderCommandEncoder setDepthStencilState:_shadowDepthStencilState];
        [shadowRenderCommandEncoder setVertexBuffer:_cube_buffer
                                             offset:0
                                            atIndex:AAPLBufferIndexVertices];
        [shadowRenderCommandEncoder setRenderPipelineState:_shadowRenderPipelineState];
        [shadowRenderCommandEncoder setCullMode:MTLCullModeFront];

        mesh_uniforms.model_transform = MeshA;
        [shadowRenderCommandEncoder setVertexBytes:&mesh_uniforms
                                            length:sizeof(MeshUniforms)
                                           atIndex:AAPLBufferIndexUniforms];

        [shadowRenderCommandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                                       vertexStart:0
                                       vertexCount:_mesh_count];

        mesh_uniforms.model_transform = MeshB;
        [shadowRenderCommandEncoder setVertexBytes:&mesh_uniforms
                                            length:sizeof(MeshUniforms)
                                           atIndex:AAPLBufferIndexUniforms];
        
        [shadowRenderCommandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                                       vertexStart:0
                                       vertexCount:_mesh_count];

    }
    [shadowRenderCommandEncoder endEncoding];

    


    id<CAMetalDrawable> currentDrawable = [metalLayer nextDrawable];
    _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexColor].texture = currentDrawable.texture;
    _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexAlbedoMetallic].texture = _albedoMetallicRenderTargetTexture;
    _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexNormalRoughness].texture = _normalRoughnessRenderTargetTexture;
    _screenRenderPassDescriptor.colorAttachments[AAPLColorIndexDepthAsColor].texture = _depthAsColorRenderTargetTexture;
    _screenRenderPassDescriptor.depthAttachment.texture = _depthRenderTargetTexture;
    id <MTLRenderCommandEncoder> renderCommandEncoder
    = [commandBuffer renderCommandEncoderWithDescriptor:_screenRenderPassDescriptor];
    
    // [renderCommandEncoder setCullMode:MTLCullModeBack];
    
    {
        {
            // view: rotate to isometric view angle
            float c = 1.0f / sqrt(3.0f);
            float s = sqrt(2.0f) / sqrt(3.0f);
            simd_float4x4 A = {{
                { 1.0f, 0.0f, 0.0f, 0.0f },
                { 0.0f,    c,    -s, 0.0f },
                { 0.0f,   s,    c, 0.0f },
                { 0.0f, 0.0f, 0.0f, 1.0f },
            }};

            float z = 4.0f;
            // view: translate camera back
            simd_float4x4 B = {{
                { 1.0f, 0.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f, 0.0f },
                { 0.0f, 0.0f, 1.0f, 0.0f },
                { 0.0f, 0.0f,    z, 1.0f },
            }};
            
            // projection: perspective
            simd_float4x4 C = {{
                { 1.0f, 0.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f, 0.0f },
                { 0.0f, 0.0f, 1.0f, 1.0f },
                { 0.0f, 0.0f, -1.0f, 0.0f },
            }};
            
            // view: zoom
            simd_float4x4 D = {{
                { z * _viewportSize.y / _viewportSize.x, 0.0f, 0.0f, 0.0f },
                { 0.0f,    z, 0.0f, 0.0f },
                { 0.0f, 0.0f, 1.0f, 0.0f },
                { 0.0f, 0.0f, 0.0f, 1.0f },
            }};

            B = simd_mul(B, A);
            mesh_uniforms.camera_world_position = simd_mul(simd_inverse(B), simd_make_float4(0,0,0,1));
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
                [renderCommandEncoder setVertexBuffer:_cube_buffer
                                               offset:0
                                              atIndex:AAPLBufferIndexVertices];
                [renderCommandEncoder setRenderPipelineState:_colorRenderPipelineState];
                
                [renderCommandEncoder setFragmentTexture:_cube_colors atIndex:AAPLTextureIndexColor];
                [renderCommandEncoder setFragmentTexture:_cube_normals atIndex:AAPLTextureIndexNormal];
                [renderCommandEncoder setFragmentTexture:_cube_roughness atIndex:AAPLTextureIndexRoughness];
                [renderCommandEncoder setFragmentTexture:_cube_metallic atIndex:AAPLTextureIndexMetallic];
                [renderCommandEncoder setFragmentTexture:_shadowRenderTargetTexture atIndex:AAPLTextureIndexShadow];
                [renderCommandEncoder setFragmentTexture:_environmentMapFiltered atIndex:AAPLTextureIndexEnvironment];
                [renderCommandEncoder setDepthStencilState:_shadowDepthStencilState];

                mesh_uniforms.model_transform = MeshA;
                mesh_uniforms.inverse_model_transform = simd_transpose(simd_inverse(mesh_uniforms.model_transform));
                [renderCommandEncoder setVertexBytes:&mesh_uniforms
                                              length:sizeof(MeshUniforms)
                                             atIndex:AAPLBufferIndexUniforms];
                [renderCommandEncoder setFragmentBytes:&mesh_uniforms
                                                length:sizeof(MeshUniforms)
                                               atIndex:AAPLBufferIndexUniforms];
                
                [renderCommandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                                         vertexStart:0
                                         vertexCount:_mesh_count];
                
                mesh_uniforms.model_transform = MeshB;
                mesh_uniforms.inverse_model_transform = simd_transpose(simd_inverse(mesh_uniforms.model_transform));
                [renderCommandEncoder setVertexBytes:&mesh_uniforms
                                              length:sizeof(MeshUniforms)
                                             atIndex:AAPLBufferIndexUniforms];
                [renderCommandEncoder setFragmentBytes:&mesh_uniforms
                                                length:sizeof(MeshUniforms)
                                               atIndex:AAPLBufferIndexUniforms];
                
                [renderCommandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                                         vertexStart:0
                                         vertexCount:_mesh_count];

                // g-buffer is now filled
                                
                [renderCommandEncoder setVertexBuffer:_screenQuadVertexBuffer
                                               offset:0
                                              atIndex:AAPLBufferIndexVertices];
                [renderCommandEncoder setDepthStencilState:_ignoreDepthStencilState];

                // image-based lighting pass:
                
                [renderCommandEncoder setRenderPipelineState:_lightingPipelineState];
                [renderCommandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                                         vertexStart:0
                                         vertexCount:6];
                
                // shadow-casting point-light pass:

                [renderCommandEncoder setRenderPipelineState:_pointLightPipelineState];
                [renderCommandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                                         vertexStart:0
                                         vertexCount:6];

            }
            
            
           

        }

        
    }
    
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
    
    {
        [renderCommandEncoder setRenderPipelineState:_renderPipelineState];
        MyUniforms uniforms;
        uniforms.position_transform = matrix_float4x4{{
            {2.0f / _viewportSize.x, 0.0f, 0.0f},
            {0.0f, -2.0f / _viewportSize.y, 0.0f, 0.0f},
            { 0.0f, 0.0f, 1.0f, 0.0f },
            {-1.0f, +1.0f, 0.0f, 1.0f},
        }};
        [renderCommandEncoder setVertexBytes:&uniforms
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
        
        
        _atlas->commit(renderCommandEncoder);
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
            dispatch_semaphore_signal(self->_atlas->_semaphore);
        }];

        
    }
    
    [renderCommandEncoder endEncoding];
        
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
