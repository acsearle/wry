//
//  WryMetalView.mm
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#include <iostream>
#include <thread>

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"

#include "WryMetalView.h"

// Metal view, like MTKView and CustomView[2], specialized to our use case of
// - render on a non-main thread
// - redraw for screen refresh
// - thread-safe update
//
// Todo
// - implement the round-robin triple-buffered drawables from [3]
// - make resize (and user interaction in general) non-blocking
// - disentangle the drawing code from ClientView and ViewController's
//   manipulation of the drawables
//
// [1] https://sarunw.com/posts/how-to-create-macos-app-without-storyboard/
// [2] https://developer.apple.com/documentation/metal/onscreen_presentation/creating_a_custom_metal_view?language=objc
// [3] https://developer.apple.com/documentation/metal/resource_synchronization/synchronizing_cpu_and_gpu_work?language=objc
//
// With CAMetalDisplayLink, this class becomes vestigial, merely delegating some
// notifications (which might be available by subscription anyway?)

@implementation WryMetalView
{
    NSTrackingArea* _tracking_area;
}

- (nonnull instancetype) initWithFrame:(CGRect)frame
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    if ((self = [super initWithFrame:frame])) {
    }
    return self;
}

- (BOOL)acceptsFirstResponder {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    return YES;
}

- (void)viewDidChangeBackingProperties
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [super viewDidChangeBackingProperties];
    [_delegate viewDidChangeBackingProperties];
}

- (void)setFrameSize:(NSSize)size
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [super setFrameSize:size];
    [_delegate viewDidChangeFrameSize];
}

- (void)setBoundsSize:(NSSize)size
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [super setBoundsSize:size];
    [_delegate viewDidChangeBoundsSize];
}

- (void)viewDidMoveToWindow
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [super viewDidMoveToWindow];
    [_delegate viewDidMoveToWindow];
}

-(void) updateTrackingAreas {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    _tracking_area = [[NSTrackingArea alloc] initWithRect:self.bounds
                                                  options:(NSTrackingMouseEnteredAndExited | NSTrackingActiveWhenFirstResponder)
                                                    owner:_delegate userInfo:nil];
    [self removeTrackingArea:_tracking_area];
    [self addTrackingArea:_tracking_area];
    [super updateTrackingAreas];
}


@end
