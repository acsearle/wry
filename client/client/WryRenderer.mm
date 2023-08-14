//
//  WryRenderer.mm
//  client
//
//  Created by Antony Searle on 1/7/2023.
//

#include "WryRenderer.h"
#include "ShaderTypes.h"

#include "atlas.hpp"
#include "font.hpp"
#include "platform.hpp"

@implementation WryRenderer
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
    std::vector<wry::sprite> _sprites;

    std::shared_ptr<wry::model> _model;
    
    wry::subvertex _terrain_triangles[6];
    
    wry::sprite _cube_sprites[2];

}

-(void) dealloc {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
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
        {
            // auto c = wry::to_sRGB(simd_float4{0.5f, 0.5f, 0.5f, 1.0f});
            // _drawableRenderDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(c.r, c.g, c.b, c.a);
            _drawableRenderDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(1,1,1,1);
        }
        
                
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

            id <MTLFunction> vertexProgram4 = [shaderLib newFunctionWithName:@"vertexShader4"];
            if(!vertexProgram4)
            {
                NSLog(@">> ERROR: Couldn't load vertex function from default library");
                return nil;
            }

                        
            // Create a pipeline state descriptor to create a compiled pipeline state object
            MTLRenderPipelineDescriptor *pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
            
            pipelineDescriptor.label                           = @"MyPipeline";
            pipelineDescriptor.vertexFunction                  = vertexProgram4;
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
                // triangles
                auto v = img.sub(256-32, 64-32, 64+64, 160+64);
                // wry::draw_bounding_box(v);
                wry::sprite s = _atlas->place(v, simd_float2{0, 0});
                s.a.texCoord += simd_float2{32, 32} / _atlas->_size;
                _terrain_triangles[0] = s.a;
                _terrain_triangles[1] = wry::subvertex{
                    s.a.position + simd_make_float4(32, 64, 0, 0),
                    s.a.texCoord + simd_float2{32, 64} / _atlas->_size
                };
                _terrain_triangles[2] = wry::subvertex{
                    s.a.position + simd_make_float4(64, 0, 0, 0),
                    s.a.texCoord + simd_float2{64, 0} / _atlas->_size
                };
                _terrain_triangles[3] = wry::subvertex{
                    s.a.position + simd_make_float4(96, 64, 0, 0),
                    s.a.texCoord + simd_float2{96, 64} / _atlas->_size
                };
                _terrain_triangles[4] = wry::subvertex{
                    s.a.position + simd_make_float4(128, 0, 0, 0),
                    s.a.texCoord + simd_float2{128, 0} / _atlas->_size
                };
                _terrain_triangles[5] = wry::subvertex{
                    s.a.position + simd_make_float4(160, 64, 0, 0),
                    s.a.texCoord + simd_float2{160, 64} / _atlas->_size
                };

            }
            
            {
                auto jmg = wry::from_png_and_multiply_alpha_f(wry::path_for_resource("cube-top", "png"));
                while (jmg.height() > 256) {
                    halve(jmg);
                }
                img = wry::to_RGB8Unorm_sRGB(jmg);
                wry::sprite s = _atlas->place(img, simd_float2{128, 128});
                _cube_sprites[0] = s;
            }

            {
                auto jmg = wry::from_png_and_multiply_alpha_f(wry::path_for_resource("cube-side", "png"));
                while (jmg.height() > 256) {
                    halve(jmg);
                }
                img = wry::to_RGB8Unorm_sRGB(jmg);
                wry::sprite s = _atlas->place(img, simd_float2{128, 128});
                _cube_sprites[1] = s;
            }

            
            
            
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
        
        /*
         uniforms.position_transform = matrix_float3x2{{
         {2.0f / _viewportSize.x, 0.0f},
         {0.0f, -2.0f / _viewportSize.y},
         {-1.0f, +1.0f} // (0, 0) -> top left
         // {0.0f, 0.0f} // (0, 0) -> center
         }};
         */
        
        // step one:
        //
        
        {
            // when we rescale, the pixels stay anchored to
            // rhs and this looks bad; they aren't anchored at midscreen
            
            // map pixels to screen space
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

            /*
            uniforms.position_transform = matrix_float4x4{{
                {40.0f / _viewportSize.x, 0.0f, 0.0f, 0.0f},
                {0.0f, -40.0f / _viewportSize.y, 0.0f, -40.0f / _viewportSize.x},
                {0.0f, 0.0f, 1.0f, 0.0f},
                {-20.0f, +20.0f, 0.0f, 40.0f} // (0, 0) -> top left
                                              // {0.0f, 0.0f} // (0, 0) -> center
            }};
             */
            uniforms.position_transform = A;
            
        }
        
        [renderEncoder setVertexBytes:&uniforms
                               length:sizeof(uniforms)
                              atIndex:MyVertexInputIndexUniforms ];
         
    }
    
    // [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    
    // _atlas->push_sprite(_font->charmap['a'].sprite_ + gl::vec2{100,100});
    
    // _atlas->push_sprite(gl::sprite{{{0,0},{0,0}},{{2048,2048},{1,1}}}+gl::vec2{256,256});
    {
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


        for (int y = origin.y, i = -c.y; y < b.y; y += 64, ++i) {
            for (int x = origin.x, j = -c.x; x < b.x; x += 64, ++j) {
                auto s = _sprites[_model->_world(simd_make_int2(i, j)).x];
                /*
                printf("%f %f %f %f\n",
                       s.a.position.x,
                       s.a.position.y,
                       s.a.position.z,
                       s.a.position.w);
                 */

                _atlas->push_sprite(s + simd_make_float2(x, y));
                //_atlas->push_sprite(_sprites[48] + simd_make_float2(x, y));
            }
        }
        
        _atlas->push_sprite(_cube_sprites[0] + simd_make_float2(512+zz.x, 512+zz.y));

        // now laboriously draw cube
        
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
        
        
        [renderEncoder setCullMode:MTLCullModeFront];
        
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
    }
    
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
