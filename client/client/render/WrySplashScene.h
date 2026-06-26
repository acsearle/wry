//
//  WrySplashScene.h
//  client
//
//  Created by Antony Searle on 2026-06-25.
//

#ifndef WrySplashScene_h
#define WrySplashScene_h

#import "WryRenderContext.h"
#import "WryScene.h"

// A minimal timed splash: clears the screen to a solid color for a fixed
// number of frames, then transitions to the scene it was handed.  It uses no
// world assets, so it can show while a heavier scene is built lazily.
//
// For now the splash is purely timed; gating it on real background asset
// loading is a later refactor, kept deliberately separate from the scene
// machinery so the Objective-C / Metal firewalling and the async work don't
// get tangled together.

@interface WrySplashScene : NSObject <WryScene>

- (nonnull instancetype)initWithContext:(nonnull WryRenderContext*)context
                        durationSeconds:(double)durationSeconds
                                   next:(nonnull id<WryScene>)next;

@end

#endif /* WrySplashScene_h */
