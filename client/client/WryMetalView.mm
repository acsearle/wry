//
//  WryMetalView.mm
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#include <iostream>
#include <thread>

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

@end

#if 0



- (void)resizeDrawable:(CGFloat)scaleFactor
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    
    CGSize newSize = self.bounds.size;
    newSize.width *= scaleFactor;
    newSize.height *= scaleFactor;
    
    if(newSize.width <= 0 || newSize.width <= 0)
    {
        return;
    }
    
    // All AppKit and UIKit calls which notify of a resize are called on the main thread.  Use
    // a synchronized block to ensure that resize notifications on the delegate are atomic
    
    // TODO: we lock the _metalLayer for both resize and rendering, so a resize
    // will stall the main thread until the current rendering (to the old size)
    // is complete.  Instead, should we atomically update the size and resize
    // the layer at the beginning / ending of the render loop?
    @synchronized(_metalLayer)
    {
        if(newSize.width == _metalLayer.drawableSize.width &&
           newSize.height == _metalLayer.drawableSize.height)
        {
            return;
        }
        
        _metalLayer.drawableSize = newSize; // <-- resize here
        
        [_delegate drawableResize:newSize];
    }
    
}

#endif
