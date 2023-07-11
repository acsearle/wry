//
//  ClientRenderer.h
//  client
//
//  Created by Antony Searle on 1/7/2023.
//

#ifndef ClientRenderer_h
#define ClientRenderer_h

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "model.hpp"

@interface ClientRenderer : NSObject

- (nonnull instancetype)initWithMetalDevice:(nonnull id<MTLDevice>)device
                        drawablePixelFormat:(MTLPixelFormat)drawabklePixelFormat
                                      model:(std::shared_ptr<wry::model>)model_;

- (void)renderToMetalLayer:(nonnull CAMetalLayer*)metalLayer;

- (void)drawableResize:(CGSize)drawableSize;

@end

#endif /* ClientRenderer_h */
