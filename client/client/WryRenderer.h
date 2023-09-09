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

@interface WryRenderer : NSObject <AVCapturePhotoCaptureDelegate, AVCaptureVideoDataOutputSampleBufferDelegate>

- (nonnull instancetype)initWithMetalDevice:(nonnull id<MTLDevice>)device
                        drawablePixelFormat:(MTLPixelFormat)drawablePixelFormat
                                      model:(std::shared_ptr<wry::model>)model_;

- (void)renderToMetalLayer:(nonnull CAMetalLayer*)metalLayer;

- (void)drawableResize:(CGSize)drawableSize;

@end

#endif /* WryRenderer_h */
