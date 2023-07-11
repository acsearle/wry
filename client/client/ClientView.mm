//
//  ClientView.mm
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#include <iostream>
#include <thread>

#import <AVFoundation/AVFoundation.h>

#include "ClientView.h"

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

@implementation ClientView
{
    CVDisplayLinkRef _displayLink;
    std::shared_ptr<wry::model> _model;
    // AVAudioPlayer* _audio_player;
    
    AVAudioEngine* _audio_engine;
    AVAudioPCMBuffer* _audio_buffer;
    NSMutableArray<AVAudioPlayerNode*>* _audio_players;
    
}

- (CALayer *)makeBackingLayer
{
    return _metalLayer = [CAMetalLayer layer];
}

- (nonnull instancetype) initWithFrame:(CGRect)frame model:(std::shared_ptr<wry::model>)model_
{
    if ((self = [super initWithFrame:frame])) {
        _model = model_;
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
        self.layer.delegate = self;
        
        {
            NSURL* url = [[NSBundle mainBundle]
                          URLForResource:@"Keyboard-Button-Click-07-c-FesliyanStudios.com2"
                          withExtension:@"mp3"];
            
            NSError* err = nil;
            
            AVAudioFile* file = [[AVAudioFile alloc]
                                 initForReading:url error:&err];
            
            if (err)
                NSLog(@"%@", [err localizedDescription]);
            
            _audio_buffer = [[AVAudioPCMBuffer alloc]
                             initWithPCMFormat:file.processingFormat
                             frameCapacity:(int) file.length];
            
            [file readIntoBuffer:_audio_buffer error:&err];
            
            /*
            {
                auto* p = _audio_buffer.floatChannelData[0];
                auto n = _audio_buffer.frameLength;
                for (int i = 0; i != n; ++i) {
                    if (p[i]) {
                        printf("%d\n", i);
                        break;
                    }
                }
            }
             */

            if (err)
                NSLog(@"%@", [err localizedDescription]);
            

            
            _audio_engine = [[AVAudioEngine alloc] init];
                        
            if (err)
                NSLog(@"%@", [err localizedDescription]);
            
            _audio_players = [[NSMutableArray<AVAudioPlayerNode*> alloc] init];
            
        }
        
        
        
        /*
        NSError* e = nil;
        _audio_player = [[AVAudioPlayer alloc]
                         initWithContentsOfURL:u
                         error:&e];
        if (e) {
            NSLog(@"%@", [e localizedDescription]);
        }
        //[_audio_player play];
        [_audio_player prepareToPlay];
         */
        





        //AVAudioPlayerNode* player = [[AVAudioPlayerNode alloc] init];
        
    }
    return self;
}

- (void)dealloc
{
    printf("~ClientView\n");
    [self stopRenderLoop];
    // ARC calls [super dealloc];
}


- (void)resizeDrawable:(CGFloat)scaleFactor
{
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
    printf("viewDidMoveToWindow\n");
    if (self.window) {
        [super viewDidMoveToWindow];
        [self setupCVDisplayLinkForScreen:self.window.screen];
        [self resizeDrawable:self.window.screen.backingScaleFactor];
    }
}

- (BOOL)setupCVDisplayLinkForScreen:(NSScreen*)screen
{
    
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
    printf("windowWillClose\n");
    // Stop the display link when the window is closing since there
    // is no point in drawing something that can't be seen
    if (notification.object == self.window)
    {
        CVReturn result = CVDisplayLinkStop(_displayLink);
        assert(result == kCVReturnSuccess);
        printf("CVDisplayLinkStop\n");
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
    printf("DispatchRenderLoop\n");
    @autoreleasepool
    {
        ClientView *clientView = (__bridge ClientView*)displayLinkContext;
        [clientView render];
    }
    printf("~DispatchRenderLoop\n");
    return kCVReturnSuccess;
}

- (void)stopRenderLoop
{
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
    [super viewDidChangeBackingProperties];
    [self resizeDrawable:self.window.screen.backingScaleFactor];
}

- (void)setFrameSize:(NSSize)size
{
    [super setFrameSize:size];
    [self resizeDrawable:self.window.screen.backingScaleFactor];
}

- (void)setBoundsSize:(NSSize)size
{
    [super setBoundsSize:size];
    [self resizeDrawable:self.window.screen.backingScaleFactor];
}


- (void)viewWillMoveToWindow:(NSWindow *)newWindow
{
    printf("viewWillMoveToWindow\n");
    [super viewWillMoveToWindow:newWindow];
}




// User interaction

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)keyDown:(NSEvent *)event {
    
    if (!event.ARepeat) {
        
    }
    
    {
        // play keydown sound
        
        AVAudioPlayerNode* player = nil;
        
        // try_pop an existing unused player
        @synchronized (_audio_players) {
            if (_audio_players.count) {
                player = _audio_players.lastObject;
                [_audio_players removeLastObject];
            }
        }

        if (!player) {
            // set up a new player
            player = [[AVAudioPlayerNode alloc] init];
            [_audio_engine attachNode:player];
            [_audio_engine connect:player
                                to:_audio_engine.mainMixerNode
                            format:_audio_buffer.format];
            NSError* err = nil;
            [_audio_engine startAndReturnError:&err];
            [player play];
            if (err)
                NSLog(@"%@", [err localizedDescription]);
            player.position = AVAudioMake3DPoint(1, 2, 3);
        }
        
        // schedule the waveform on the player
        [player scheduleBuffer:_audio_buffer
             completionHandler:^{
            // "don't stop the player in the handler, it may deadlock"
            // when playback completes, put the player back in the stack
            @synchronized (self->_audio_players) {
                [self->_audio_players addObject:player];
            }            
        }];
    }
    
    // UTF-16 code for key, such as private use 0xf700 = NSUpArrowFunctionKey
    // printf("%x\n", [event.characters characterAtIndex:0]);
    
    // _model->_console.back().append(event.characters.UTF8String);
    NSLog(@"keyDown: \"%@\"\n", event.characters);
    if (event.characters.length) {
        NSLog(@"keyDown: (%x)\n", [event.characters characterAtIndex:0]);
        auto guard = std::unique_lock{_model->_mutex};
        switch ([event.characters characterAtIndex:0]) {
            case NSCarriageReturnCharacter:
                _model->_console.emplace_back();
                break;
            case NSDeleteCharacter:
                if (!_model->_console.back().empty())
                    _model->_console.back().pop_back();
                break;
            case NSUpArrowFunctionKey:
                std::rotate(_model->_console.begin(), _model->_console.end() - 1, _model->_console.end());
                break;
            case NSDownArrowFunctionKey:
                std::rotate(_model->_console.begin(), _model->_console.begin() + 1, _model->_console.end());
                break;
            case NSLeftArrowFunctionKey:
                if (!_model->_console.back().empty()) {
                    auto ch = _model->_console.back().pop_back();
                    _model->_console.back().push_front(ch);
                }
                break;
            case NSRightArrowFunctionKey:
                if (!_model->_console.back().empty()) {
                    auto ch = _model->_console.back().pop_front();
                    _model->_console.back().push_back(ch);
                }
                break;
            default:
                _model->_console.back().append(event.characters.UTF8String);
                break;
        }
    }
}
    

- (void)keyUp:(NSEvent *)event {
    // NSLog(@"keyUp: \"%@\"\n", event.characters);
}

- (void) mouseMoved:(NSEvent *)event {}
- (void) mouseEntered:(NSEvent *)event {}
- (void) mouseExited:(NSEvent *)event {}
- (void) mouseDown:(NSEvent *)event {}
- (void) mouseDragged:(NSEvent *)event {}
- (void) mouseUp:(NSEvent *)event {}
- (void) rightMouseDown:(NSEvent *)event {}
- (void) rightMouseDragged:(NSEvent *)event {}
- (void) rightMouseUp:(NSEvent *)event {}
- (void) otherMouseDown:(NSEvent *)event {}
- (void) otherMouseDragged:(NSEvent *)event {}
- (void) otherMouseUp:(NSEvent *)event {}

@end
