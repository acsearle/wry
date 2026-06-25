//
//  WryScene.h
//  client
//
//  Created by Antony Searle on 2026-06-25.
//

#ifndef WryScene_h
#define WryScene_h

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>

// A WryScene is one screenful of behavior the app can show: the game world,
// a main menu, a splash screen.  The host (currently WryDelegate) drives the
// current scene each frame without knowing its concrete type:
//
//     per frame:  [scene update]; [scene render];
//     on resize:  [scene drawableResize:size];
//     to switch:  if (scene.nextScene) scene = scene.nextScene;
//
// This is the seam between scene-specific code and the shared rendering
// infrastructure: concrete scenes are built against a WryRenderContext (the
// device + 2D services every scene shares) and own only what is unique to
// them.  Today the sole scene is the game world (WryRenderer); splash and
// menu scenes come later.
//
// The protocol deliberately names no Metal types -- the frame's command
// buffer / drawable lifecycle still lives inside each scene's -render for
// now.  Hoisting present into the host (so scenes only encode into a shared
// target) is a later refactor, gated on a second scene forcing the issue and
// on preserving the world scene's late nextDrawable acquisition.

@protocol WryScene <NSObject>

// Advance scene-owned state one frame.  The world scene steps the simulation;
// a static menu or splash does nothing.
- (void)update;

// Draw one frame.
- (void)render;

// React to a drawable-size change (reallocate size-dependent targets).
- (void)drawableResize:(CGSize)size;

// Non-nil to request that the host swap to a different scene after this frame
// (mirrors gui::Overlay::wants_close).  nil means "stay".  No scene transition
// exists yet, so the world scene always returns nil.
@property (nonatomic, readonly, nullable) id<WryScene> nextScene;

@optional

// Platform cursor reset on mouse-enter.  Only scenes with a custom cursor
// (the world's palette cursor) implement it; the host guards the call with
// -respondsToSelector:.
- (void)resetCursor;

@end

#endif /* WryScene_h */
