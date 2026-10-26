//
//  WryDelegate.h
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#ifndef WryDelegate_h
#define WryDelegate_h

#import <AppKit/AppKit.h>

#include "nursery.hpp"

#include "world_state.hpp"
#include "WryMetalView.h"

// WryDelegate receives all notifications from macOS and AppKit that were
// variously directed to the Application, Window or View, and handles them
// on the main thread, typically passing them on asynchronously to the
// model or the render threads.

@interface WryDelegate : NSResponder < NSApplicationDelegate,
    NSWindowDelegate, WryMetalViewDelegate >

// `scope` is the process scope (the nursery in main) that everything the
// GuiContext hosts spawns background work into; `gui` is main's app tier
// (the registry, the control socket, the launch options already applied),
// borrowed for the delegate's life
-(nonnull instancetype) initWithScope:(nonnull wry::coroutine::Nursery*)scope
                                  gui:(nonnull wry::GuiContext*)gui;

@property (nonatomic, nullable) NSCursor* cursor;
@property (atomic) BOOL done;

// Shutdown: let the control socket's connections finish (briefly), then
// stop it and unlink its file.
- (void)shutdownControlSocket;
// After the wait group has drained: the accept loop has finished
- (void)releaseControlSocket;

// Offscreen host (--offscreen): set up without a window, rendering to an
// offscreen drawable; main then calls -render while -hasWork, and
// -waitForInput otherwise.  (The render-free host, --headless, is
// render/headless_host.hpp and never creates a delegate.)
- (void)setUpOffscreen;
- (BOOL)hasWork;
- (void)waitForInput;

-(void) render;

@end

#endif /* WryDelegate_h */
