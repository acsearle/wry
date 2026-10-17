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
#include <filesystem>
#include <thread>

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
            case kVK_F1:                return k::F1;
            case kVK_F2:                return k::F1 + 1;
            case kVK_F3:                return k::F1 + 2;
            case kVK_F4:                return k::F1 + 3;
            case kVK_F5:                return k::F1 + 4;
            case kVK_F6:                return k::F1 + 5;
            case kVK_F7:                return k::F1 + 6;
            case kVK_F8:                return k::F1 + 7;
            case kVK_F9:                return k::F1 + 8;
            case kVK_F10:               return k::F1 + 9;
            case kVK_F11:               return k::F1 + 10;
            case kVK_F12:               return k::F1 + 11;
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
    // Headless (--headless): no window, view or layer; frames run when
    // commands have work to do, at a fixed dt, and are never presented.
    BOOL _headless;
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
        // The control socket (core/docs/commands.md section 9) exists only
        // when asked for: --control-socket [PATH], PATH defaulting to
        // $TMPDIR/wry-control.sock (what tools/wryctl and tools/wry_mcp.py
        // look for).  Launch the bundle with
        //     open client.app --args --control-socket
        NSArray<NSString*>* arguments = [[NSProcessInfo processInfo] arguments];
        NSUInteger flag = [arguments indexOfObject:@"--control-socket"];
        if (flag != NSNotFound) {
            std::string path;
            if (flag + 1 < arguments.count && ![arguments[flag + 1] hasPrefix:@"-"]) {
                path = [arguments[flag + 1] UTF8String];
            } else {
                std::error_code ec;
                path = (std::filesystem::temp_directory_path(ec) / "wry-control.sock").string();
            }
            _gui->open_control_socket(wry::String(path.c_str()));
        }
        // Load (or first-run install) settings.json.  The cwd is already
        // the assets directory (main.mm chdir), so the config/ paths in
        // settings_paths resolve next to saves/.  Any warnings (unknown
        // ids, malformed combos, unparseable file) surface in the
        // floating log once a scene is up.
        std::vector<wry::String> warnings;
        _gui->settings = wry::load_settings(_gui->settings_paths, &warnings);
        for (wry::String const& w : warnings) {
            NSLog(@"%.*s\n", (int)w.chars.size(), w.chars.data());
            _gui->append_log(w, std::chrono::seconds(15));
        }
    }
    return self;
}

// The shared render context and the boot scene chain, windowed or headless.
- (void)buildScenesWithDevice:(id<MTLDevice>)device
                  pixelFormat:(MTLPixelFormat)pixelFormat {
    // Build the shared render context, then the boot scene chain against it.
    // This host owns the context and the per-frame command buffer / drawable /
    // present; scenes only encode their passes (see -render below).
    _ctx = [[WryRenderContext alloc] initWithDevice:device
                                drawablePixelFormat:pixelFormat];

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
    // The `quit` command ends the app the same way.
    _gui->on_quit = [quit] { quit(); };
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
    // The present path (see -render) blits each scene's returned texture
    // into the late-acquired drawable, and a blit DESTINATION is exactly
    // what framebufferOnly = YES forbids (it restricts the drawable to
    // render-attachment use; the Metal debug layer asserts on the copy,
    // and it only happened to work with validation off).  NO trades the
    // layer's render-target-only optimizations for blit-ability, which
    // the copy-based present needs.
    _metalLayer.framebufferOnly = NO;
    _metalLayer.displaySyncEnabled = YES;
    _metalView.layer = _metalLayer;
    _metalView.wantsLayer = YES;

    [self buildScenesWithDevice:_metalLayer.device
                    pixelFormat:_metalLayer.pixelFormat];
    
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
    // -terminate: (e.g. an Apple Event quit) exit()s straight after this,
    // past main's shutdown, so the control socket is shut down here too.
    [self shutdownControlSocket];
}

// The headless host (core/docs/commands.md section 9): the same scenes,
// registry and socket, but no window, view or layer.  The drawable is a
// fixed 1920 x 1080 pixels viewed at two pixels per point (a Retina
// window's default), so coordinates and screenshots match a windowed run.
// main drives it: -render while commands have work, -waitForCommands
// otherwise.
- (void)setUpHeadless {
    _headless = YES;
    // Headless is only useful driven, so the socket is implied.
    if (!_gui->control_socket) {
        std::error_code ec;
        auto path = (std::filesystem::temp_directory_path(ec) / "wry-control.sock").string();
        _gui->open_control_socket(wry::String(path.c_str()));
    }
    [self buildScenesWithDevice:MTLCreateSystemDefaultDevice()
                    pixelFormat:MTLPixelFormatRGBA16Float];
    [_scene drawableResize:[self drawableSizePixels]];
}

- (CGSize)drawableSizePixels {
    return _headless ? CGSizeMake(1920.0, 1080.0) : _metalLayer.drawableSize;
}

- (CGSize)viewSizePoints {
    return _headless ? CGSizeMake(960.0, 540.0) : _metalView.bounds.size;
}

- (BOOL)hasCommandWork {
    return !_gui->idle();
}

// Block until a request arrives on the control socket, for at most 100 ms,
// repinning so an idle main thread never holds the collector back.
- (void)waitForCommands {
    if (_gui->control_socket)
        _gui->wait_for_commands(std::chrono::milliseconds(100));
    else
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    wry::mutator_repin();
}

- (void)shutdownControlSocket {
    // Blocks main for up to the grace period (main is pinned, so the
    // cancellation unwinds happen on a worker, under sync_wait)
    if (_gui->control_socket)
        (void) wry::execution::sync_wait(_gui->control_socket->shutdown(std::chrono::seconds(1)));
}

- (void)releaseControlSocket {
    _gui->control_socket.reset();
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

// Trackpad pinch.  Arrives via the responder chain like scrollWheel:.
-(void) magnifyWithEvent:(NSEvent *)event {
    using namespace wry::gui;
    Event ev{};
    ev.kind = WryEventKindMagnify;
    ev.mods = modifiers_from_ns_flags([event modifierFlags]);
    ev.location = location_in_view_pt(_metalView, event);
    ev.magnification = (float)event.magnification;
    _gui->events.push(ev);
}

- (void)encodeWithCoder:(nonnull NSCoder *)coder {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

// Read the presentable back for pending `screenshot` commands (see
// wry::ScreenshotRequest): a blit into a shared buffer, encoded into this
// frame's command buffer ahead of the present blit.  The completion handler
// runs on a Metal thread: it copies the pixels out to every request and
// publishes each with a release store, and touches nothing else -- in
// particular no GC object.
- (void)encodeScreenshotsOf:(id<MTLTexture>)presentable
                       into:(id<MTLCommandBuffer>)commandBuffer {
    std::vector<std::shared_ptr<wry::ScreenshotRequest>> requests
        = std::move(_gui->screenshot_requests);
    _gui->screenshot_requests.clear();
    if (presentable.pixelFormat != MTLPixelFormatRGBA16Float) {
        for (auto& r : requests) {
            r->error = wry::String("screenshot: unsupported presentable pixel format");
            r->ready.store_release(true);
        }
        return;
    }
    const NSUInteger w = presentable.width;
    const NSUInteger h = presentable.height;
    const NSUInteger bytesPerRow = w * 4 * sizeof(uint16_t);
    id<MTLBuffer> buffer = [_ctx.device newBufferWithLength:bytesPerRow * h
                                                    options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
    [blit copyFromTexture:presentable
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(w, h, 1)
                 toBuffer:buffer
        destinationOffset:0
   destinationBytesPerRow:bytesPerRow
 destinationBytesPerImage:bytesPerRow * h];
    [blit endEncoding];
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        const bool ok = completed.status == MTLCommandBufferStatusCompleted;
        const uint16_t* p = static_cast<const uint16_t*>(buffer.contents);
        for (auto& r : requests) {
            if (ok) {
                r->width = (uint32_t)w;
                r->height = (uint32_t)h;
                r->half_rgba.assign(p, p + w * h * 4);
            } else {
                r->error = wry::String("screenshot: the frame failed to render");
            }
            r->ready.store_release(true);
        }
    }];
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
    // handle_events; the log is app-tier now).  The frame counter stamps
    // command history (core/docs/commands.md section 6).
    ++_gui->frame;
    _gui->drain_notifications();
    CGSize sz = [self viewSizePoints];

    // Commands: what injected positions and widget queries are relative
    // to, then pending replies polled and lines waiting behind them issued
    // -- before input, so injected events are dispatched this frame.
    _gui->view_size_pt = simd_make_float2((float)sz.width, (float)sz.height);
    // Every scene's widget pixels are drawable pixels; the world scene also
    // sets this on resize, but the menu scene does not, so the host does.
    CGSize drawable = [self drawableSizePixels];
    _gui->viewport_size = simd_make_float2((float)drawable.width, (float)drawable.height);
    _gui->scene_widgets = [_scene respondsToSelector:@selector(widgetRoot)]
        ? [_scene widgetRoot] : nullptr;
    _gui->service_commands();
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
    // Headless frames run only on demand, so they keep a fixed dt: the same
    // commands then see the same scene timers on every run.
    const auto now = std::chrono::steady_clock::now();
    double dt = 0.0;
    if (_headless)
        dt = 1.0 / 60.0;
    else if (_haveLastFrameTime)
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
    if (presentable && !_gui->screenshot_requests.empty())
        [self encodeScreenshotsOf:presentable into:command_buffer];
    if (!_headless) @autoreleasepool {
        id<CAMetalDrawable> drawable = [_metalLayer nextDrawable];
        if (drawable && presentable) {
            id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
            [blit copyFromTexture:presentable toTexture:drawable.texture];
            [blit endEncoding];
            [command_buffer presentDrawable:drawable];
        }
    }
    // Every transient buffer slice this frame used is reusable once the GPU
    // passes this point.
    [_ctx.arena endFrameOnCommandBuffer:command_buffer];
    [command_buffer commit];
    // With no drawable to throttle it, a headless host waits out each
    // frame; this also completes screenshots before the next frame.
    if (_headless)
        [command_buffer waitUntilCompleted];

    // Scene transition: a scene requests its successor via -nextScene (mouse /
    // key for the menu, a timer for the splash).  Swap after presenting, and
    // size the incoming scene to the current drawable before its first frame.
    id<WryScene> next = _scene.nextScene;
    if (next) {
        _scene = next;
        [_scene drawableResize:[self drawableSizePixels]];
    }
}

@end
