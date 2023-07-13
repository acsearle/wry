//
//  ClientView.h
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#ifndef ClientView_h
#define ClientView_h

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "model.hpp"

@protocol ClientViewDelegate <NSObject>

- (void)drawableResize:(CGSize)size;
- (void)renderToMetalLayer:(nonnull CAMetalLayer *)metalLayer;

@end

@interface ClientView : NSView

@property (nonatomic, nonnull, readonly) CAMetalLayer *metalLayer;
@property (nonatomic, nullable) id<ClientViewDelegate> delegate;

- (nonnull instancetype) initWithFrame:(CGRect)frame model:(std::shared_ptr<wry::model>)model_;
- (void)render;
- (void)resizeDrawable:(CGFloat)scaleFactor;
- (void)stopRenderLoop;

@end

#endif /* ClientView_h */
