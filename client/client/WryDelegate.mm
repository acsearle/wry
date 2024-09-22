//
//  WryDelegate.mm
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#import <AVFoundation/AVFoundation.h>

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"

#include "WryRenderer.h"
#include "WryMetalView.h"
#include "WryDelegate.h"
#include "WryAudio.h"

// [1] https://sarunw.com/posts/how-to-create-macos-app-without-storyboard/

// Our structurally simple application with a single window and view uses a
// single class as a responder and as a delegate for application, window and
// view to handle all external events on the main thread.

@implementation WryDelegate
{
    std::shared_ptr<wry::model> _model;
    NSWindow* _window;
    WryMetalView* _metalView;
    CAMetalLayer* _metalLayer;
    WryRenderer* _renderer;
    NSThread* _renderThread;
    WryAudio* _audio;
    NSCursor* _cursor;
}

-(nonnull instancetype) init
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    if ((self = [super init])) {
        _model = std::make_shared<wry::model>();
    }
    return self;
}

#pragma mark NSApplicationDelegate

- (void)applicationWillFinishLaunching:(NSNotification *)aNotification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    
    NSScreen* screen = [NSScreen mainScreen];
    CGFloat scaleFactor = [screen backingScaleFactor];
    NSRect contentRect = NSMakeRect(0.0, 0.0, 1920.0 / scaleFactor,
                                    1080.0 / scaleFactor);
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
    
    _metalView = [[WryMetalView alloc]
                  initWithFrame:contentRect];
    _metalView.delegate = self;
    
    _metalLayer = [[CAMetalLayer alloc] init];
    _metalLayer.device = MTLCreateSystemDefaultDevice();
    _metalLayer.pixelFormat = MTLPixelFormatRGBA16Float;
    _metalLayer.framebufferOnly = YES;
    _metalLayer.displaySyncEnabled = YES;
    _metalView.layer = _metalLayer;
    _metalView.wantsLayer = YES;
    
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;   // Disable mouse cursor reset
        
        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        //ImGui::StyleColorsLight();
        
        // Setup Renderer backend
        ImGui_ImplMetal_Init(_metalLayer.device);
        // io.Fonts->AddFontDefault();
        CGFloat framebufferScale = NSScreen.mainScreen.backingScaleFactor;
        io.Fonts->AddFontFromFileTTF("/Users/antony/Desktop/assets/Futura Medium Condensed.otf",
                                     std::floor(18.0f * framebufferScale))->Scale = 1.0f / framebufferScale;
        ImGui_ImplOSX_Init(_metalView);
    }
    
    
    _renderer = [[WryRenderer alloc] initWithMetalDevice:_metalLayer.device
                                     drawablePixelFormat:_metalLayer.pixelFormat
                                                   model:_model
                                                    view:_metalView];
    
    _audio = [[WryAudio alloc] init];

    _window.contentView = _metalView;
    // insert this delegate into the responder chain above the metalview
    _metalView.nextResponder = self;

}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
   
    // [_window toggleFullScreen:nil];
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
    self.done = YES;
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplOSX_Shutdown();
    ImGui::DestroyContext();
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
    self.done = YES;
    // [[NSApplication sharedApplication] terminate:self];
}

#pragma mark WryMetalViewDelegate

- (void)viewDidMoveToWindow {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void)viewDidChangeFrameSize {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [self drawableResize:_metalView.bounds.size];
}

- (void)viewDidChangeBoundsSize {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [self drawableResize:_metalView.bounds.size];

}

- (void)viewDidChangeBackingProperties {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [self drawableResize:_metalView.bounds.size];
}

- (void)drawableResize:(CGSize)size
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    CGFloat scaleFactor = _window.backingScaleFactor;
    CGSize newSize = size;
    newSize.width *= scaleFactor;
    newSize.height *= scaleFactor;
    _metalLayer.drawableSize = newSize;
    [_renderer drawableResize:newSize];
}

#pragma mark NSResponder

// User interaction

- (BOOL)acceptsFirstResponder
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    return YES;
}

- (BOOL)becomeFirstResponder
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    return YES;
}

- (BOOL)resignFirstResponder
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    return YES;
}

- (void)keyDown:(NSEvent *)event {
        
    // Dear IMGUI installs its TextInput thing as a subview
        
    // Forward the events where?
    
    // assert([self nextResponder]);
    
    

    if (ImGui::GetIO().WantCaptureKeyboard)
        return;
    
    using namespace wry;
    
    // [self interpretKeyEvents:@[event]];
    
    //if (!event.ARepeat) {
        
    //}
    
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
        
        if (_model->_console_active) {
            switch ([event.characters characterAtIndex:0]) {
                case NSCarriageReturnCharacter:
                    _model->_console.emplace_back();
                    break;
                case 0x001b: // ESC
                    _model->_console_active = false;
                    _model->append_log(u8"[ESC] Hide console");
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
                        auto ch = _model->_console.back().back();
                        _model->_console.back().pop_back();
                        _model->_console.back().push_front(ch);
                    }
                    break;
                case NSRightArrowFunctionKey:
                    if (!_model->_console.back().empty()) {
                        auto ch = _model->_console.back().front_and_pop_front();
                        _model->_console.back().push_back(ch);
                    }
                    break;
                default: {
                    auto p = reinterpret_cast<const char8_t*>(event.characters.UTF8String);
                    _model->_console.back().append(p);
                } break;
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
                    _model->append_log((char*) buffer);
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
                    if (isxdigit(ch)) {
                        _model->_outstanding_keysdown.push_back((char32_t) ch);
                    }
                    break;
            }
        }
    }
}


- (void)keyUp:(NSEvent *)event {
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    NSLog(@"keyUp: \"%@\"\n", event.characters);
}

- (void)flagsChanged:(NSEvent *)event {
    if (ImGui::GetIO().WantCaptureKeyboard) return;
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
    if (ImGui::GetIO().WantCaptureMouse) return;
    NSPoint location_in_window = [event locationInWindow];
    NSPoint location_in_view = [_metalView convertPoint:location_in_window fromView:nil];
    _model->_mouse.x = 2.0f * location_in_view.x / _metalView.bounds.size.width - 1.0f;
    _model->_mouse.y = 2.0f * location_in_view.y / _metalView.bounds.size.height - 1.0f;
    
    //NSLog(@"(%g, %g)", _metalView.bounds.size.width, _metalView.bounds.size.height);
}

- (void) mouseEntered:(NSEvent *)event {
    if (ImGui::GetIO().WantCaptureMouse) return;
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [_renderer resetCursor];
}

- (void) mouseExited:(NSEvent *)event {
    if (ImGui::GetIO().WantCaptureMouse) return;
    // NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void) mouseDown:(NSEvent *)event {
    if (ImGui::GetIO().WantCaptureMouse) return;
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}
- (void) mouseDragged:(NSEvent *)event {
    // NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [self mouseMoved:event];
}

- (void) mouseUp:(NSEvent *)event {
    if (ImGui::GetIO().WantCaptureMouse) return;
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    NSPoint location_in_window = [event locationInWindow];
    NSPoint location_in_view = [_metalView convertPoint:location_in_window fromView:nil];
    _model->_mouse.x = 2.0f * location_in_view.x / _metalView.bounds.size.width - 1.0f;
    _model->_mouse.y = 2.0f * location_in_view.y / _metalView.bounds.size.height - 1.0f;
    _model->_outstanding_click = true;
}

- (void) rightMouseDown:(NSEvent *)event {
    if (ImGui::GetIO().WantCaptureMouse) return;
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void) rightMouseDragged:(NSEvent *)event {
    if (ImGui::GetIO().WantCaptureMouse) return;
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void) rightMouseUp:(NSEvent *)event {
    if (ImGui::GetIO().WantCaptureMouse) return;
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void) otherMouseDown:(NSEvent *)event {
    if (ImGui::GetIO().WantCaptureMouse) return;
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void) otherMouseDragged:(NSEvent *)event {
    if (ImGui::GetIO().WantCaptureMouse) return;
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void) otherMouseUp:(NSEvent *)event {    
    if (ImGui::GetIO().WantCaptureMouse) return;
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

-(void) scrollWheel:(NSEvent *)event {
    if (ImGui::GetIO().WantCaptureMouse) return;
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    //auto lock = std::unique_lock{_model->_mutex};
    _model->_looking_at.x += event.scrollingDeltaX * _window.screen.backingScaleFactor;
    _model->_looking_at.y += event.scrollingDeltaY * _window.screen.backingScaleFactor;
    // NSLog(@"(%g, %g)", _model->_yx.x, _model->_yx.y);
}

- (void)encodeWithCoder:(nonnull NSCoder *)coder {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

-(void)render {
    [_renderer render];
}

@end
