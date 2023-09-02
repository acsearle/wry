//
//  WryDelegate.mm
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#import <AVFoundation/AVFoundation.h>

#include "WryRenderer.h"
#include "WryMetalView.h"
#include "WryDelegate.h"
#include "WryAudio.h"


// [1] https://sarunw.com/posts/how-to-create-macos-app-without-storyboard/

// Our simple application with a single window and view uses a single class as
// application delegate, window delegate and first responder to handle all
// external events on the main thread.  Typically these are minimally processed
// and handed over to the renderer and simulation threads; we want to avoid
// doing any blocking work on the main thread.
//
// Outstanding questions:
// - does this include network events?
// - does CoreAudio do any work on the calling thread (such as buffer prep)

@interface WryDelegate ()

@end

@implementation WryDelegate
{
    std::shared_ptr<wry::model> _model;
    NSWindow* _window;
    WryRenderer *_renderer;
    WryAudio *_audio;
}

-(nonnull instancetype) initWithModel:(std::shared_ptr<wry::model>)mdl
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    if ((self = [super init])) {
        _model = mdl;
    }
    return self;
}

-(void) dealloc {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

#pragma mark NSApplicationDelegate

- (void)applicationWillFinishLaunching:(NSNotification *)aNotification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    
    NSRect contentRect = NSMakeRect(0.0, 0.0, 960, 540);
    _window = [[NSWindow alloc] initWithContentRect:contentRect
                                          styleMask:(NSWindowStyleMaskMiniaturizable
                                                     | NSWindowStyleMaskClosable
                                                     | NSWindowStyleMaskResizable
                                                     | NSWindowStyleMaskTitled)
                                            backing:NSBackingStoreBuffered
                                              defer:YES];
    _window.delegate = self;
    _window.title = @"WryApplication";
    _window.acceptsMouseMovedEvents = YES;
    [_window center];
    //[_window setContentViewController:[[ViewController alloc] initWithModel:_model]];
    // [_window setContentView:[
    
    WryMetalView* view = [[WryMetalView alloc]
                        initWithFrame:contentRect model:_model];
    
    view.metalLayer.device =  MTLCreateSystemDefaultDevice();
    view.metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    view.delegate = self;
    
    _renderer = [[WryRenderer alloc] initWithMetalDevice:view.metalLayer.device
                                        drawablePixelFormat:view.metalLayer.pixelFormat
                                                      model:_model];
    
    _audio = [[WryAudio alloc] init];
    
    _window.contentView = view;
    view.nextResponder = self;

    
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void)applicationWillBecomeActive:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void)applicationDidBecomeActive:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [_window makeKeyAndOrderFront:nil];
}

- (void)applicationWillResignActive:(NSNotification *)aNotification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void)applicationDidResignActive:(NSNotification *)aNotification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    return NSTerminateNow;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    return YES;
}

- (void)applicationWillTerminate:(NSNotification *)aNotification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

#pragma mark NSWindowDelegate

- (NSSize)windowWillResize:(NSWindow *)sender toSize:(NSSize)frameSize {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    return frameSize;
}

- (void)windowDidResize:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void)windowDidChangeScreen:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void)windowDidChangeBackingProperties:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (BOOL)windowShouldClose:(NSWindow *)sender {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    return YES;
}

- (void)windowWillClose:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

#pragma mark ClientViewDelegate

- (void)drawableResize:(CGSize)size
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [_renderer drawableResize:size];
}

- (void)renderToMetalLayer:(nonnull CAMetalLayer *)layer
{
    // NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [_renderer renderToMetalLayer:layer];
}

#pragma mark NSResponder

// User interaction

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)keyDown:(NSEvent *)event {
    
    if (!event.ARepeat) {
        
    }

    /*
    {
        // random location on a line in front of the listener
        AVAudio3DPoint location = AVAudioMake3DPoint(// rand() & 1 ? -1.0 : +1.0,
                                                     rand() * 2.0 / RAND_MAX - 1.0,
                                                     0.0, //rand() & 1 ? -1.0 : +1.0,
                                                     1.0 // rand() & 1 ? -1.0 : +1.0
                                                     );
        
        
        NSString* name = ((event.characters.length
                           && ([event.characters characterAtIndex:0]
                               == NSCarriageReturnCharacter))
                          ? @"mixkit-typewriter-classic-return-1381"
                          : @"Keyboard-Button-Click-07-c-FesliyanStudios.com2");
        
        [_audio play:name at:location];
    }
     */
        
    
    // UTF-16 code for key, such as private use 0xf700 = NSUpArrowFunctionKey
    // printf("%x\n", [event.characters characterAtIndex:0]);
    
    // _model->_console.back().append(event.characters.UTF8String);
    NSLog(@"keyDown: \"%@\" (\"%@\")\n",
          event.characters,
          [event charactersByApplyingModifiers:0]);
    if (event.characters.length) {
        // NSLog(@"keyDown: (%x)\n", [event.characters characterAtIndex:0]);
        auto guard = std::unique_lock{_model->_mutex};
        
        if (_model->_console_active) {
            switch ([event.characters characterAtIndex:0]) {
                case NSCarriageReturnCharacter:
                    _model->_console.emplace_back();
                    break;
                case 0x001b: // ESC
                    _model->_console_active = false;
                    _model->append_log("[ESC] Hide console");
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
        } else {
            auto toggle = [](auto& x) {
                x = !x;
            };
            unichar ch = [[event charactersByApplyingModifiers:0] characterAtIndex:0];
            char buffer[100];
            switch (ch) {
                case '`':
                    _model->_console_active = true;
                    _model->append_log("[~] Show console");
                    break;
                case 'j':
                    toggle(_model->_show_jacobian);
                    snprintf(buffer, 100, "%s [J]acobians", _model->_show_jacobian ? "Show" : "Hide");
                    _model->append_log(buffer);
                    break;
                case 'p':
                    toggle(_model->_show_points);
                    snprintf(buffer, 100, "%s [P]oints", _model->_show_points ? "Show" : "Hide");
                    _model->append_log(buffer);
                    break;
                case 'w':
                    toggle(_model->_show_wireframe);
                    snprintf(buffer, 100, "%s [W]ireframe", _model->_show_wireframe ? "Show" : "Hide");
                    _model->append_log(buffer);
                    break;
                default:
                    break;
            }
        }
    }
}


- (void)keyUp:(NSEvent *)event {
    NSLog(@"keyUp: \"%@\"\n", event.characters);
}

- (void)flagsChanged:(NSEvent *)event {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    /*
    NSEventModifierFlagCommand;
    NSEventModifierFlagOption;
    NSEventModifierFlagHelp;
    NSEventModifierFlagControl;
    NSEventModifierFlagCapsLock;
    NSEventModifierFlagFunction;
    NSEventModifierFlagNumericPad;
     */
}

- (void) mouseMoved:(NSEvent *)event {
}

- (void) mouseEntered:(NSEvent *)event {}
- (void) mouseExited:(NSEvent *)event {}
- (void) mouseDown:(NSEvent *)event {}
- (void) mouseDragged:(NSEvent *)event {
    auto lock = std::unique_lock{_model->_mutex};
    _model->_yx.x += event.deltaX * _window.screen.backingScaleFactor;
    _model->_yx.y += event.deltaY * _window.screen.backingScaleFactor;
    // NSLog(@"(%g, %g)", _model->_yx.x, _model->_yx.y);
     
}
- (void) mouseUp:(NSEvent *)event {}
- (void) rightMouseDown:(NSEvent *)event {}
- (void) rightMouseDragged:(NSEvent *)event {}
- (void) rightMouseUp:(NSEvent *)event {}
- (void) otherMouseDown:(NSEvent *)event {}
- (void) otherMouseDragged:(NSEvent *)event {}
- (void) otherMouseUp:(NSEvent *)event {}

-(void) scrollWheel:(NSEvent *)event {
    auto lock = std::unique_lock{_model->_mutex};
    _model->_yx.x += event.scrollingDeltaX * _window.screen.backingScaleFactor;
    _model->_yx.y += event.scrollingDeltaY * _window.screen.backingScaleFactor;
    // NSLog(@"(%g, %g)", _model->_yx.x, _model->_yx.y);
}

@end
