//
//  ClientRenderer.h
//  client
//
//  Created by Antony Searle on 1/7/2023.
//

#ifndef WryRenderer_h
#define WryRenderer_h

#import <AVFoundation/AVFoundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "model.hpp"

@interface WryRenderer : NSObject

- (nonnull instancetype)initWithMetalDevice:(nonnull id<MTLDevice>)device
                        drawablePixelFormat:(MTLPixelFormat)drawablePixelFormat
                                      model:(std::shared_ptr<wry::model>)model_
                                       view:(nonnull NSView*)view;

// Advance the simulation one step.  Call once per frame before -render; the
// two are deliberately separate so a scene with no world (splash / menu) can
// skip the step while still drawing.
- (void)update;

- (void)render;

- (void)drawableResize:(CGSize)drawableSize;

-(void)resetCursor;

@end

#endif /* WryRenderer_h */
