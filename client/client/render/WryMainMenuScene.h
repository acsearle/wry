//
//  WryMainMenuScene.h
//  client
//
//  Created by Antony Searle on 2026-06-25.
//

#ifndef WryMainMenuScene_h
#define WryMainMenuScene_h

#import "WryRenderContext.h"
#import "WryScene.h"

#include "model.hpp"

// The top-level main menu, shown when no game is loaded.  Distinct from the
// in-game ESC menu (gui::MainMenuOverlay): this one *creates* games.
//
// For now it is a placeholder that clears to a solid color and, on the first
// mouse-down or key-down, transitions to the game world it builds via the
// supplied factory.  Title text and the real NEW / LOAD / JOIN / SETTINGS
// choices arrive once per-scene widgets and input are fleshed out.

@interface WryMainMenuScene : NSObject <WryScene>

// `nextFactory` builds the scene to switch to when the player starts a game.
// It is invoked lazily (the world's asset load is heavy) on the first input.
- (nonnull instancetype)initWithContext:(nonnull WryRenderContext*)context
                                  model:(std::shared_ptr<wry::model>)model
                                   next:(nonnull id<WryScene> (^)(void))nextFactory;

@end

#endif /* WryMainMenuScene_h */
