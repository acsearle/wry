//
//  ClientView.h
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#ifndef WryMetalView_h
#define WryMetalView_h

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "model.hpp"

@protocol WryMetalViewDelegate <NSObject>

- (void)drawableResize:(CGSize)size;
- (void)renderToMetalLayer:(nonnull CAMetalLayer *)metalLayer;

@end

@interface WryMetalView : NSView

@property (nonatomic, nonnull, readonly) CAMetalLayer *metalLayer;
@property (nonatomic, nullable) id<WryMetalViewDelegate> delegate;

- (nonnull instancetype) initWithFrame:(CGRect)frame model:(std::shared_ptr<wry::model>)model_;
- (void)render;
- (void)resizeDrawable:(CGFloat)scaleFactor;
- (void)stopRenderLoop;

@end

#endif /* WryMetalView_h */
