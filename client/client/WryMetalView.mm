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

@implementation WryMetalView
{
    CVDisplayLinkRef _displayLink;
    std::shared_ptr<wry::model> _model;
    // AVAudioPlayer* _audio_player;
}

- (CALayer *)makeBackingLayer
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);

    return _metalLayer = [CAMetalLayer layer];
}

- (nonnull instancetype) initWithFrame:(CGRect)frame model:(std::shared_ptr<wry::model>)model_
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);

    if ((self = [super initWithFrame:frame])) {
        _model = model_;
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
    }
    return self;
}

- (void)dealloc
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [self stopRenderLoop];
    // ARC calls [super dealloc];
}


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

- (void)render
{

    // Must synchronize if rendering on background thread to ensure resize operations from the
    // main thread are complete before rendering which depends on the size occurs.
    
    // this is way too clunky; when the main thread resizes, we're holding up
    // the main thread until the rendering is complete
    
    // we need to do the trick with a pipeline of several drawables from the other examples
    
    // we can resize the drawable from an atomic size as part of the render
    // operation
    @synchronized(_metalLayer)
    {
        [_delegate renderToMetalLayer:_metalLayer];
    }
}

- (void)viewDidMoveToWindow
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    if (self.window) {
        [super viewDidMoveToWindow];
        [self setupCVDisplayLinkForScreen:self.window.screen];
        [self resizeDrawable:self.window.screen.backingScaleFactor];
    }
}

- (BOOL)setupCVDisplayLinkForScreen:(NSScreen*)screen
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);

    CVReturn cvReturn;
    
    // Create a display link capable of being used with all active displays
    cvReturn = CVDisplayLinkCreateWithActiveCGDisplays(&_displayLink);
    
    if(cvReturn != kCVReturnSuccess)
    {
        return NO;
    }
    
    // Set DispatchRenderLoop as the callback function and
    // supply this view as the argument to the callback.
    cvReturn = CVDisplayLinkSetOutputCallback(_displayLink, &DispatchRenderLoop, (__bridge void*)self);
    
    if(cvReturn != kCVReturnSuccess)
    {
        return NO;
    }
    
    // Associate the display link with the display on which the
    // view resides
    CGDirectDisplayID viewDisplayID =
    (CGDirectDisplayID) [self.window.screen.deviceDescription[@"NSScreenNumber"] unsignedIntegerValue];;
    
    cvReturn = CVDisplayLinkSetCurrentCGDisplay(_displayLink, viewDisplayID);
    
    if(cvReturn != kCVReturnSuccess)
    {
        return NO;
    }
    
    cvReturn = CVDisplayLinkStart(_displayLink);
    if(cvReturn != kCVReturnSuccess)
    {
        return NO;
    }

    
    NSNotificationCenter* notificationCenter = [NSNotificationCenter defaultCenter];
    
    // Register to be notified when the window closes so that you
    // can stop the display link
    [notificationCenter addObserver:self
                           selector:@selector(windowWillClose:)
                               name:NSWindowWillCloseNotification
                             object:self.window];
    
    return YES;
}


- (void)windowWillClose:(NSNotification*)notification
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    // Stop the display link when the window is closing since there
    // is no point in drawing something that can't be seen
    if (notification.object == self.window)
    {
        CVReturn result = CVDisplayLinkStop(_displayLink);
        assert(result == kCVReturnSuccess);
    }
}


// This is the renderer output callback function
static CVReturn DispatchRenderLoop(CVDisplayLinkRef displayLink,
                                   const CVTimeStamp* now,
                                   const CVTimeStamp* outputTime,
                                   CVOptionFlags flagsIn,
                                   CVOptionFlags* flagsOut,
                                   void* displayLinkContext)
{
    @autoreleasepool
    {
        WryMetalView* view = (__bridge WryMetalView*)displayLinkContext;
        [view render];
    }
    return kCVReturnSuccess;
}

- (void)stopRenderLoop
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    if (_displayLink)
    {
        // Stop the display link BEFORE releasing anything in the view otherwise the display link
        // thread may call into the view and crash when it encounters something that no longer
        // exists
        CVReturn cvReturn;
        cvReturn = CVDisplayLinkStop(_displayLink);
        // assert(cvReturn == kCVReturnSuccess);
        CVDisplayLinkRelease(_displayLink);
    }
}

- (void)viewDidChangeBackingProperties
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [super viewDidChangeBackingProperties];
    [self resizeDrawable:self.window.screen.backingScaleFactor];
}

- (void)setFrameSize:(NSSize)size
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [super setFrameSize:size];
    [self resizeDrawable:self.window.screen.backingScaleFactor];
}

- (void)setBoundsSize:(NSSize)size
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [super setBoundsSize:size];
    [self resizeDrawable:self.window.screen.backingScaleFactor];
}


- (void)viewWillMoveToWindow:(NSWindow *)newWindow
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [super viewWillMoveToWindow:newWindow];
}

- (BOOL)acceptsFirstResponder {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    // we accept first responder and chain to nextResponder, the big delegate
    return YES;
}

@end
