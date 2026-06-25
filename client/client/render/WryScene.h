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
#import <Metal/Metal.h>

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
// them.  The scenes are the game world (WryWorldScene), the splash
// (WrySplashScene), and the main menu (WryMainMenuScene).
//
// The host owns the per-frame command buffer + drawable lifecycle: it creates
// the command buffer, the scene encodes its passes into it and returns the
// texture to show, and the host blits that into the drawable (acquired as late
// as possible) and presents + commits.  This shares one drawable / present
// path across every scene while preserving the world scene's late nextDrawable.

@protocol WryScene <NSObject>

// Advance scene-owned state one frame.  The world scene steps the simulation;
// a static menu or splash does nothing.
- (void)update;

// Encode this frame's passes into the host-supplied command buffer and return
// the texture to present (or nil to present nothing).  The host blits the
// returned texture into the drawable and presents + commits.
- (nullable id<MTLTexture>)encodeIntoCommandBuffer:(nonnull id<MTLCommandBuffer>)commandBuffer;

// React to a drawable-size change (reallocate size-dependent targets).
- (void)drawableResize:(CGSize)size;

// Non-nil to request that the host swap to a different scene after this frame
// (mirrors gui::Overlay::wants_close).  nil means "stay".  No scene transition
// exists yet, so the world scene always returns nil.
@property (nonatomic, readonly, nullable) id<WryScene> nextScene;

@optional

// Scenes that own their input drain the model's event queue here, and the
// host calls this instead of the world pump.  Scenes that don't implement it
// fall through to the host's world pump (the model overlay-stack dispatch).
- (void)handleEvents;

// Platform cursor reset on mouse-enter.  Only scenes with a custom cursor
// (the world's palette cursor) implement it; the host guards the call with
// -respondsToSelector:.
- (void)resetCursor;

@end

#endif /* WryScene_h */
