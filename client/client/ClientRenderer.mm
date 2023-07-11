//
//  ClientRenderer.mm
//  client
//
//  Created by Antony Searle on 1/7/2023.
//

#include "ClientRenderer.h"
#include "ShaderTypes.h"

#include "atlas.hpp"
#include "font.hpp"
#include "platform.hpp"

@implementation ClientRenderer
{
    // renderer global ivars
    id <MTLDevice>              _device;
    id <MTLCommandQueue>        _commandQueue;
    id <MTLRenderPipelineState> _pipelineState;
    id <MTLTexture>             _depthTarget;
    
    // Render pass descriptor which creates a render command encoder to draw to the drawable
    // textures
    MTLRenderPassDescriptor *_drawableRenderDescriptor;
    
    vector_uint2 _viewportSize;
    
    NSUInteger _frameNum;
    
    wry::atlas* _atlas;
    wry::font* _font;
    
    std::shared_ptr<wry::model> _model;
}

-(void) dealloc {
    printf("~ClientRenderer\n");
}

- (nonnull instancetype)initWithMetalDevice:(nonnull id<MTLDevice>)device
                        drawablePixelFormat:(MTLPixelFormat)drawabklePixelFormat
                                      model:(std::shared_ptr<wry::model>)model_
{
    
    if ((self = [super init])) {
        
        _model = model_;
        
        _frameNum = 0;
        
        _device = device;
        
        _commandQueue = [_device newCommandQueue];
        
        _drawableRenderDescriptor = [MTLRenderPassDescriptor new];
        _drawableRenderDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
        _drawableRenderDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        _drawableRenderDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0.5, 0.25, 1);
                
        {
            id<MTLLibrary> shaderLib = [_device newDefaultLibrary];
            if(!shaderLib)
            {
                NSLog(@" ERROR: Couldnt create a default shader library");
                // assert here because if the shader libary isn't loading, nothing good will happen
                return nil;
            }
            
            id <MTLFunction> vertexProgram = [shaderLib newFunctionWithName:@"vertexShader"];
            if(!vertexProgram)
            {
                NSLog(@">> ERROR: Couldn't load vertex function from default library");
                return nil;
            }
            
            id <MTLFunction> fragmentProgram = [shaderLib newFunctionWithName:@"fragmentShader"];
            if(!fragmentProgram)
            {
                NSLog(@" ERROR: Couldn't load fragment function from default library");
                return nil;
            }

                        
            // Create a pipeline state descriptor to create a compiled pipeline state object
            MTLRenderPipelineDescriptor *pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
            
            pipelineDescriptor.label                           = @"MyPipeline";
            pipelineDescriptor.vertexFunction                  = vertexProgram;
            pipelineDescriptor.fragmentFunction                = fragmentProgram;
            pipelineDescriptor.colorAttachments[0].pixelFormat = drawabklePixelFormat;
            
            pipelineDescriptor.colorAttachments[0].blendingEnabled = YES;
            pipelineDescriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
            pipelineDescriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
            pipelineDescriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
            pipelineDescriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            pipelineDescriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
            pipelineDescriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                        
            NSError *error;
            _pipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                                                     error:&error];
            if(!_pipelineState)
            {
                NSLog(@"ERROR: Failed aquiring pipeline state: %@", error);
                return nil;
            }
            
            _atlas = new wry::atlas(2048, device);
            
            _font = new wry::font(build_font(*_atlas));
            
            auto img = wry::from_png_and_multiply_alpha(wry::path_for_resource("assets", "png"));
            wry::draw_bounding_box(img);
            for (int x = 0; x != 2048; x += 64)
                _atlas->place(img.sub(0, x, 64, 64), gl::vec2{32, 32});
            
            
            
            
            
        }
    }
    return self;
}

- (void)renderToMetalLayer:(nonnull CAMetalLayer*)metalLayer
{
    
    // -> called on CVDisplayLink thread after, bad access
    
    // Create a new command buffer for each render pass to the current drawable.
    id <MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    
    id<CAMetalDrawable> currentDrawable = [metalLayer nextDrawable];
    
    // If the current drawable is nil, skip rendering this frame
    if(!currentDrawable)
    {
        return;
    }
    
    _drawableRenderDescriptor.colorAttachments[0].texture = currentDrawable.texture;
    
    id <MTLRenderCommandEncoder> renderEncoder =
    [commandBuffer renderCommandEncoderWithDescriptor:_drawableRenderDescriptor];
    
    
    [renderEncoder setRenderPipelineState:_pipelineState];
    
    {
        MyUniforms uniforms;
        
        uniforms.position_transform = matrix_float3x2{{
            {2.0f / _viewportSize.x, 0.0f},
            {0.0f, -2.0f / _viewportSize.y},
            {-1.0f, +1.0f} // (0, 0) -> top left
            // {0.0f, 0.0f} // (0, 0) -> center
        }};
        
        [renderEncoder setVertexBytes:&uniforms
                               length:sizeof(uniforms)
                              atIndex:MyVertexInputIndexUniforms ];
         
    }
    
    // [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    
    // _atlas->push_sprite(_font->charmap['a'].sprite_ + gl::vec2{100,100});
    
    // _atlas->push_sprite(gl::sprite{{{0,0},{0,0}},{{20480,20480},{1,1}}}+gl::vec2{256,256});
    
    auto draw_text = [=](gl::rect<float> x, wry::string_view v) {

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
                    
                gl::sprite s = q->second.sprite_;
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
        gl::vec2 z;
        bool first = true;
        for (auto p = _model->_console.end(); (y >= 0) && (p != _model->_console.begin());) {
            --p;
            y -= _font->height;
            z = draw_text({0, y, 1920, 1080}, *p);
            if (first) {
                draw_text(gl::rect<float>{z.x, z.y, 1920, 1080 }, (_frameNum & 0x40) ? "_" : " ");
                first = false;
            }
        }
        
    }
    
    

    _atlas->commit(renderEncoder);
    
    [renderEncoder endEncoding];
    
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
        dispatch_semaphore_signal(self->_atlas->_semaphore);
    }];
    
    [commandBuffer presentDrawable:currentDrawable];
    
    [commandBuffer commit];
    
    _frameNum++;
    
}

- (void)drawableResize:(CGSize)drawableSize
{
    _viewportSize.x = drawableSize.width;
    _viewportSize.y = drawableSize.height;
    
}

@end
