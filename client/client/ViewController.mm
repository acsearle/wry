//
//  ViewController.m
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#include <simd/simd.h>
#import <Metal/Metal.h>

#include "ClientRenderer.h"
#include "ViewController.h"
#include "ShaderTypes.h"

// Todo:
// - separate the drawing code from the setup and teardown of the drawing
//   context
// - how to get state in and out of ViewController without singletons
//   - (possibly the AppDelegate is the root of all state?)
//
// [2] https://developer.apple.com/documentation/metal/onscreen_presentation/creating_a_custom_metal_view?language=objc


// ViewController seems almost unnecessary when we don't use storyboards, can
// we just install the view directly in the window and move the did-load
// contents into ClientView init?

@implementation ViewController
{
    ClientRenderer *_renderer;
    std::shared_ptr<wry::model> _model;
}

-(void) dealloc {
    printf("~ViewController\n");
}


-(nonnull instancetype) initWithModel:(std::shared_ptr<wry::model>)model_
{
    if ((self = [super init])) {
        _model = model_;
    }
    return self;
}


-(void)loadView {
    [self setView:[[ClientView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 1920/2, 1080/2) model:_model]];
};

- (void)viewDidLoad {
    
    [super viewDidLoad];
    
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    ClientView *view = (ClientView *)self.view;
    
    // Set the device for the layer so the layer can create drawable textures that can be rendered to
    // on this device.
    view.metalLayer.device = device;
    
    // Set this class as the delegate to receive resize and render callbacks.
    view.delegate = self;
    
    view.metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    
    _renderer = [[ClientRenderer alloc] initWithMetalDevice:device
                                        drawablePixelFormat:view.metalLayer.pixelFormat
                                                      model:_model];
    
}

- (void)drawableResize:(CGSize)size
{
    [_renderer drawableResize:size];
}

- (void)renderToMetalLayer:(nonnull CAMetalLayer *)layer
{
    [_renderer renderToMetalLayer:layer];
}

@end
