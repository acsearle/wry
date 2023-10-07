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

- (void)viewDidChangeBackingProperties;
- (void)viewDidChangeFrameSize;
- (void)viewDidChangeBoundsSize;
- (void)viewDidMoveToWindow;

@end

@interface WryMetalView : NSView

@property (nonatomic, nullable) id<WryMetalViewDelegate> delegate;

- (nonnull instancetype) initWithFrame:(CGRect)frame;

@end

#endif /* WryMetalView_h */
