//
//  WryDelegate.mm
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#import <AVFoundation/AVFoundation.h>
#import <Carbon/Carbon.h>           // kVK_* hardware key codes

#include <algorithm>
#include <chrono>
#include <cstring>

#include "WryRenderContext.h"
#include "WryWorldScene.h"
#include "WrySplashScene.h"
#include "WryMainMenuScene.h"
#include "WryScene.h"
#include "gui_context.hpp"
#include "WryMetalView.h"
#include "WryDelegate.h"
#include "WryAudio.h"

#include "gui_event.hpp"

// Carbon's umbrella header drags in a `typedef UInt16 EventKind` at global
// scope, which is why our event-kind enum is named WryEventKind (declared
// in gui_event.hpp) -- different name, no collision, no qualification
// gymnastics needed.

// [1] https://sarunw.com/posts/how-to-create-macos-app-without-storyboard/

// Our structurally simple application with a single window and view uses a
// single class as a responder and as a delegate for application, window and
// view to handle all external events on the main thread.

// Computation, network and render are (or will be) async; the main thread
// will be lightly loaded and spend its time waiting for messages and vsync

// NSEvent / AppKit types appear only inside this file; everything else sees
// only wry::gui::Event.  Each NSResponder callback below translates its
// NSEvent into one Event and pushes it onto the model's per-frame queue;
// [WryDelegate render] drains the queue before the renderer runs.

namespace {

    wry::gui::Modifiers modifiers_from_ns_flags(NSEventModifierFlags flags) {
        using M = wry::gui::Modifiers;
        M m;
        if (flags & NSEventModifierFlagShift)    m.bits |= M::Shift;
        if (flags & NSEventModifierFlagControl)  m.bits |= M::Ctrl;
        if (flags & NSEventModifierFlagOption)   m.bits |= M::Alt;
        if (flags & NSEventModifierFlagCommand)  m.bits |= M::Cmd;
        if (flags & NSEventModifierFlagCapsLock) m.bits |= M::Caps;
        if (flags & NSEventModifierFlagFunction) m.bits |= M::Fn;
        return m;
    }

    // Map an NSEvent to a wry::gui::key code.  Non-text keys (Escape,
    // Enter, Tab, arrows, navigation, Backspace, Delete) are identified by
    // NSEvent.keyCode against the kVK_* USB-HID constants from Carbon's
    // HIToolbox.  This is the only Apple-supported API for layout-
    // independent physical-key identification, and is robust against IME
    // state, dead-key composition, and the various ways event.characters
    // can come back empty for non-character keys.
    //
    // Text keys fall through to charactersByApplyingModifiers:0 (so 'a'
    // is reported as 'a' regardless of Shift; layout-aware so Dvorak users
    // still get Dvorak letters).
    uint32_t key_from_event(NSEvent* event) {
        namespace k = wry::gui::key;
        switch (event.keyCode) {
            case kVK_Escape:            return k::Escape;
            case kVK_Return:            return k::Enter;
            case kVK_ANSI_KeypadEnter:  return k::Enter;
            case kVK_Tab:               return k::Tab;
            case kVK_Delete:            return k::Backspace; // mac "delete" key
            case kVK_ForwardDelete:     return k::Delete;    // forward-delete
            case kVK_UpArrow:           return k::ArrowUp;
            case kVK_DownArrow:         return k::ArrowDown;
            case kVK_LeftArrow:         return k::ArrowLeft;
            case kVK_RightArrow:        return k::ArrowRight;
            case kVK_Home:              return k::Home;
            case kVK_End:               return k::End;
            case kVK_PageUp:            return k::PageUp;
            case kVK_PageDown:          return k::PageDown;
            default:
                break;
        }
        NSString* unmod = [event charactersByApplyingModifiers:0];
        if (unmod && unmod.length > 0) {
            unichar ch = [unmod characterAtIndex:0];
            if (ch >= 0x20 && ch <= 0x7E) {
                if (ch >= 'A' && ch <= 'Z') {
                    return (uint32_t)(ch - 'A' + 'a');
                }
                return (uint32_t)ch;
            }
        }
        return k::Unknown;
    }

    // NSEvent.locationInWindow -> view-local logical points with top-left
    // origin.  AppKit views default to bottom-left y-up; we flip y here so
    // every event downstream agrees with the 2D overlay convention.
    // Returned as simd_float2 (the C-visible vector type) and assigned
    // implicitly into the Event::location float2 field.
    simd_float2 location_in_view_pt(WryMetalView* view, NSEvent* event) {
        NSPoint w = [event locationInWindow];
        NSPoint v = [view convertPoint:w fromView:nil];
        return simd_make_float2((float)v.x,
                                (float)(view.bounds.size.height - v.y));
    }

    void fill_text_from_characters(wry::gui::Event& ev, NSString* characters) {
        if (!characters || characters.length == 0)
            return;
        unichar first = [characters characterAtIndex:0];
        // Only forward as insertable text if it looks like text.  Reject:
        //   - ASCII control codes (0x00..0x1F) -- includes Enter, Tab, ESC
        //   - DEL (0x7F)
        //   - NSFunctionKey range (0xF700..0xF8FF) -- arrows, F-keys, etc.
        bool is_printable = (first >= 0x20)
                         && (first != 0x7F)
                         && !(first >= 0xF700 && first <= 0xF8FF);
        if (!is_printable)
            return;
        const char* utf8 = characters.UTF8String;
        if (!utf8)
            return;
        size_t cap = sizeof(ev.text) - 1;
        size_t n = strnlen(utf8, cap);
        memcpy(ev.text, utf8, n);
        ev.text[n] = '\0';
    }

} // anonymous namespace

@implementation WryDelegate
{
    // Host-owned generic GUI state (events, viewport, log / console,
    // notifications), borrowed by every scene's WorldState.  Declared before
    // _scene so it outlives the WorldStates that reference it.
    std::unique_ptr<wry::GuiContext> _gui;
    NSWindow* _window;
    WryMetalView* _metalView;
    CAMetalLayer* _metalLayer;
    // The shared render context (device + 2D services), owned by this host and
    // borrowed by whatever scene is showing.
    WryRenderContext* _ctx;
    // The current scene, driven through the WryScene protocol so this platform
    // layer doesn't depend on which scene is showing.  Boots to a splash and
    // transitions splash -> main menu -> world (see applicationWillFinishLaunching).
    id<WryScene> _scene;
    NSThread* _renderThread;
    WryAudio* _audio;
    NSCursor* _cursor;

    // Per-frame wall-clock delta, fed to the scene so its timing is in seconds
    // and frame-rate independent.
    std::chrono::steady_clock::time_point _lastFrameTime;
    BOOL _haveLastFrameTime;
}

-(nonnull instancetype) init
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    if ((self = [super init])) {
        _gui = std::make_unique<wry::GuiContext>();
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
                                              defer:YES
                                             screen:screen];
    _window.delegate = self;
    _window.title = @"WryApplication";
    _window.acceptsMouseMovedEvents = YES;

    // TODO: The window delegate is supposed to automatically be in the
    // responder chain, so why do we need to explicitly set it as the
    // nextResponder?
    _window.nextResponder = self;
    
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

    // Build the shared render context, then the boot scene chain against it.
    // This host owns the context and the per-frame command buffer / drawable /
    // present; scenes only encode their passes (see -render below).
    _ctx = [[WryRenderContext alloc] initWithDevice:_metalLayer.device
                                drawablePixelFormat:_metalLayer.pixelFormat];

    // Boot flow: splash -> main menu -> world.  The world scene is built lazily
    // (its asset load is heavy) the moment the menu hands control on.
    WryRenderContext* ctx = _ctx;
    // The menu builds a WorldState (NEW / LOAD) and hands it here to be wrapped
    // in the world scene -- the host no longer owns the world state.
    id<WryScene> (^makeWorld)(std::shared_ptr<wry::WorldState>) =
        ^id<WryScene>(std::shared_ptr<wry::WorldState> world) {
            return [[WryWorldScene alloc] initWithContext:ctx model:world];
        };
    __weak WryDelegate* weakSelf = self;
    void (^quit)(void) = ^{
        WryDelegate* strongSelf = weakSelf;
        if (!strongSelf)
            return;
        // QUIT TO DESKTOP and the (X) button both HIDE the window (orderOut:)
        // and end main's render loop; main's shutdown (core/main.mm) then pumps
        // the run loop to commit the hide to the Window Server, drains
        // background work, deactivates the app, and exits.
        //
        // We deliberately do NOT -close.  A real close tears down the metal
        // view's CAMetalLayer, and doing that while main's hand-rolled drain
        // pump is driving CA commits collides with it ("entangle context after
        // pre-commit" / "Invalid attempt to open a new transaction during CA
        // commit").  AppKit's own teardown ([NSApp run] + terminate:) would
        // sequence it cleanly, but terminate: exit()s past our RAII teardown, so
        // we can't use it.  orderOut: is the platform's hide primitive (it's
        // what SDL's Cocoa_HideWindow uses); -close is orderOut: PLUS the
        // teardown that bites us, and formally closing buys a dying process
        // nothing.
        [strongSelf->_window orderOut:nil];
        strongSelf.done = YES;  // ends main's render loop -> shutdown
    };
    WryMainMenuScene* menu =
        [[WryMainMenuScene alloc] initWithContext:_ctx
                                              gui:_gui.get()
                                             next:makeWorld
                                             quit:quit];
    _scene = [[WrySplashScene alloc] initWithContext:_ctx
                                     durationSeconds:2.5
                                                next:menu];
    
    // _audio = [[WryAudio alloc] init];

    _window.contentView = _metalView;

    // Make the metal view the window's initial first responder so that
    // -keyDown: arrives at the view (which forwards to us) instead of at
    // NSWindow.  NSWindow's own -keyDown: special-cases ESC / Cmd-period
    // and routes them through -cancelOperation: in a way that's hard to
    // hook reliably; with the view as first responder, every key flows
    // through one well-defined path.
    _window.initialFirstResponder = _metalView;

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
    // Route the (X) button through the same shutdown as QUIT TO DESKTOP: hide
    // the window (orderOut:) and end main's render loop, but VETO AppKit's real
    // -close (return NO).  A real close tears down the CAMetalLayer and collides
    // with main's shutdown drain pump -- see the quit block in
    // -applicationWillFinishLaunching for the full rationale.  Vetoing means
    // -windowWillClose: won't fire from here; we set `done` ourselves.
    [sender orderOut:nil];
    self.done = YES;
    return NO;
}

- (void)windowWillClose:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    // Backstop only.  Both quit paths hide via orderOut: and veto the real
    // close, so this normally never fires; if some other path ever does close
    // the window, still end the render loop.
    self.done = YES;
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
    [_scene drawableResize:newSize];
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

    NSLog(@"keyDown: keyCode=%u  characters=\"%@\"  unmodified=\"%@\"\n",
          (unsigned)event.keyCode,
          event.characters,
          [event charactersByApplyingModifiers:0]);

    using namespace wry::gui;

    Event ev{};
    ev.kind = WryEventKindKeyDown;
    ev.mods = modifiers_from_ns_flags([event modifierFlags]);
    ev.is_repeat = [event isARepeat];

    // event.key: the unmodified logical key.  Named keys (Escape, arrows,
    // ...) come from event.keyCode; text keys from charactersByApplyingModifiers.
    ev.key = key_from_event(event);

    // event.text: the modifier-applied insertable text, if any.  Drives
    // text-entry handlers like the console line buffer.  Left empty for
    // navigation, function, and other non-text keys.
    fill_text_from_characters(ev, event.characters);

    _gui->events.push(ev);
}


- (void)keyUp:(NSEvent *)event {
    NSLog(@"keyUp: keyCode=%u  characters=\"%@\"\n",
          (unsigned)event.keyCode,
          event.characters);

    using namespace wry::gui;

    Event ev{};
    ev.kind = WryEventKindKeyUp;
    ev.mods = modifiers_from_ns_flags([event modifierFlags]);
    ev.key = key_from_event(event);
    _gui->events.push(ev);
}


// Note on ESC: when WryMetalView is the first responder (which it is,
// thanks to _window.initialFirstResponder above), ESC arrives at our
// -keyDown: like any other key.  No -cancelOperation: override is needed.
// The action-method routing AppKit uses when NSWindow itself is first
// responder is sidestepped by the view-as-first-responder path.

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
    using namespace wry::gui;
    Event ev{};
    ev.kind = WryEventKindMouseMove;
    ev.mods = modifiers_from_ns_flags([event modifierFlags]);
    ev.location = location_in_view_pt(_metalView, event);
    _gui->events.push(ev);
}

- (void) mouseEntered:(NSEvent *)event {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    // Platform-side cursor reset stays here; pure AppKit concern.  The
    // logical hover-enter still flows through the queue so future widgets
    // can react.
    // resetCursor is an optional WryScene method (only the world scene's
    // palette cursor needs it); guard for scenes that don't implement it.
    if ([_scene respondsToSelector:@selector(resetCursor)])
        [_scene resetCursor];

    using namespace wry::gui;
    Event ev{};
    ev.kind = WryEventKindMouseEnter;
    ev.mods = modifiers_from_ns_flags([event modifierFlags]);
    ev.location = location_in_view_pt(_metalView, event);
    _gui->events.push(ev);
}

- (void) mouseExited:(NSEvent *)event {
    using namespace wry::gui;
    Event ev{};
    ev.kind = WryEventKindMouseExit;
    ev.mods = modifiers_from_ns_flags([event modifierFlags]);
    ev.location = location_in_view_pt(_metalView, event);
    _gui->events.push(ev);
}

- (void) mouseDown:(NSEvent *)event {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    using namespace wry::gui;
    Event ev{};
    ev.kind = WryEventKindMouseDown;
    ev.button = MouseButton::Left;
    ev.mods = modifiers_from_ns_flags([event modifierFlags]);
    ev.location = location_in_view_pt(_metalView, event);
    _gui->events.push(ev);
}

- (void) mouseDragged:(NSEvent *)event {
    // Pre-event-queue code treated drag as a position update; preserve.
    [self mouseMoved:event];
}

- (void) mouseUp:(NSEvent *)event {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    using namespace wry::gui;
    Event ev{};
    ev.kind = WryEventKindMouseUp;
    ev.button = MouseButton::Left;
    ev.mods = modifiers_from_ns_flags([event modifierFlags]);
    ev.location = location_in_view_pt(_metalView, event);
    _gui->events.push(ev);
}

- (void) rightMouseDown:(NSEvent *)event {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    using namespace wry::gui;
    Event ev{};
    ev.kind = WryEventKindMouseDown;
    ev.button = MouseButton::Right;
    ev.mods = modifiers_from_ns_flags([event modifierFlags]);
    ev.location = location_in_view_pt(_metalView, event);
    _gui->events.push(ev);
}

- (void) rightMouseDragged:(NSEvent *)event {
    using namespace wry::gui;
    Event ev{};
    ev.kind = WryEventKindMouseMove;
    ev.mods = modifiers_from_ns_flags([event modifierFlags]);
    ev.location = location_in_view_pt(_metalView, event);
    _gui->events.push(ev);
}

- (void) rightMouseUp:(NSEvent *)event {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    using namespace wry::gui;
    Event ev{};
    ev.kind = WryEventKindMouseUp;
    ev.button = MouseButton::Right;
    ev.mods = modifiers_from_ns_flags([event modifierFlags]);
    ev.location = location_in_view_pt(_metalView, event);
    _gui->events.push(ev);
}

- (void) otherMouseDown:(NSEvent *)event {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    using namespace wry::gui;
    Event ev{};
    ev.kind = WryEventKindMouseDown;
    ev.button = MouseButton::Middle;
    ev.mods = modifiers_from_ns_flags([event modifierFlags]);
    ev.location = location_in_view_pt(_metalView, event);
    _gui->events.push(ev);
}

- (void) otherMouseDragged:(NSEvent *)event {
    using namespace wry::gui;
    Event ev{};
    ev.kind = WryEventKindMouseMove;
    ev.mods = modifiers_from_ns_flags([event modifierFlags]);
    ev.location = location_in_view_pt(_metalView, event);
    _gui->events.push(ev);
}

- (void) otherMouseUp:(NSEvent *)event {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    using namespace wry::gui;
    Event ev{};
    ev.kind = WryEventKindMouseUp;
    ev.button = MouseButton::Middle;
    ev.mods = modifiers_from_ns_flags([event modifierFlags]);
    ev.location = location_in_view_pt(_metalView, event);
    _gui->events.push(ev);
}

-(void) scrollWheel:(NSEvent *)event {
    using namespace wry::gui;
    Event ev{};
    ev.kind = WryEventKindScroll;
    ev.mods = modifiers_from_ns_flags([event modifierFlags]);
    ev.location = location_in_view_pt(_metalView, event);
    // The backingScaleFactor multiply preserves the units the legacy
    // `_looking_at` accumulator used; flagged for cleanup in gui_event.hpp.
    CGFloat scale = _window.screen.backingScaleFactor;
    ev.scroll_delta.x = (float)(event.scrollingDeltaX * scale);
    ev.scroll_delta.y = (float)(event.scrollingDeltaY * scale);
    _gui->events.push(ev);
}

- (void)encodeWithCoder:(nonnull NSCoder *)coder {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

-(void)render {
    // Input.  Scenes that own their input (the menu) implement -handleEvents
    // and drain the queue themselves; otherwise the world pump walks each
    // queued event through the model's overlay stack (top down) and falls back
    // to the transitional legacy bookkeeping (world click / scroll-pan paths).
    // Runs on the main thread between NSEvent dispatch and the draw, so the
    // queue is always seen consistently.
    //
    // First surface any messages background work posted since last frame into
    // the app-tier log, once per frame regardless of scene (was in the world's
    // handle_events; the log is app-tier now).
    _gui->drain_notifications();
    NSSize sz = _metalView.bounds.size;
    if ([_scene respondsToSelector:@selector(handleEventsWithViewSize:)]) {
        [_scene handleEventsWithViewSize:sz];
    } else {
        // Scenes without an input handler (the splash) discard their input so
        // it doesn't pile up and leak into the next scene.
        _gui->events.clear();
    }
    // A scene's input handler may have requested shutdown this frame: QUIT TO
    // DESKTOP hides the window from here (quit block -> orderOut: + done).  Skip
    // the encode/present -- no point drawing a final frame into a window that's
    // being hidden for shutdown; main's drain takes it from here.
    if (self.done)
        return;
    // Advance the current scene, then draw it.  Splitting update from the draw
    // is the seam scenes use: a splash / menu scene has no world to step, so
    // the step must not live inside the draw call.  Pass real elapsed seconds
    // (clamped, so a stall or the first frame doesn't jump scene timers).
    const auto now = std::chrono::steady_clock::now();
    double dt = 0.0;
    if (_haveLastFrameTime)
        dt = std::clamp(std::chrono::duration<double>(now - _lastFrameTime).count(),
                        0.0, 0.1);
    _lastFrameTime = now;
    _haveLastFrameTime = YES;
    [_scene update:dt];

    // The host owns the per-frame command buffer + drawable + present, so every
    // scene shares one present path.  The scene encodes its passes into our
    // command buffer and returns the texture to show; we acquire the drawable
    // as late as possible (holding it only across the blit + present) and blit
    // the scene's texture into it.
    id<MTLCommandBuffer> command_buffer = [_ctx.commandQueue commandBuffer];
    id<MTLTexture> presentable = [_scene encodeIntoCommandBuffer:command_buffer];
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [_metalLayer nextDrawable];
        if (drawable && presentable) {
            id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
            [blit copyFromTexture:presentable toTexture:drawable.texture];
            [blit endEncoding];
            [command_buffer presentDrawable:drawable];
        }
    }
    [command_buffer commit];

    // Scene transition: a scene requests its successor via -nextScene (mouse /
    // key for the menu, a timer for the splash).  Swap after presenting, and
    // size the incoming scene to the current drawable before its first frame.
    id<WryScene> next = _scene.nextScene;
    if (next) {
        _scene = next;
        [_scene drawableResize:_metalLayer.drawableSize];
    }
}

@end
