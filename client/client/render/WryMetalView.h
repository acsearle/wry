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

// Keyboard event forwarding.  The view becomes first responder (see
// WryMetalView.mm) so that it receives -keyDown: directly for every key.
// If we left the window as first responder, NSWindow.keyDown: would
// intercept ESC and Cmd-period and route them through -cancelOperation:
// instead, which is unreliable in a custom-window setup.  The view's
// overrides forward each NSEvent to the delegate, which translates it
// into a wry::gui::Event and enqueues it.
- (void)keyDown:(NSEvent *)event;
- (void)keyUp:(NSEvent *)event;
- (void)flagsChanged:(NSEvent *)event;

@end

@interface WryMetalView : NSView

@property (nonatomic, nullable) id<WryMetalViewDelegate> delegate;

- (nonnull instancetype) initWithFrame:(CGRect)frame;

@end

#endif /* WryMetalView_h */
