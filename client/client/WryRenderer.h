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
#include "gc.hpp"

@interface WryRenderer : NSObject <CAMetalDisplayLinkDelegate>

- (nonnull instancetype)initWithMetalDevice:(nonnull id<MTLDevice>)device
                        drawablePixelFormat:(MTLPixelFormat)drawablePixelFormat
                                      model:(std::shared_ptr<wry::model>)model_;

- (void)renderToMetalLayer:(nonnull CAMetalDisplayLinkUpdate*)update;

- (void)drawableResize:(CGSize)drawableSize;

-(void)resetCursor;

@end

#endif /* WryRenderer_h */
