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
// in-game ESC menu (gui::MainMenuOverlay): this one *creates* games.  It hosts
// a column of buttons -- NEW GAME, LOAD GAME, JOIN GAME, QUIT TO DESKTOP --
// drawn with the shared 2D services (atlas / font / UI pipeline), and owns its
// own input (it dispatches events to the button column itself).

@interface WryMainMenuScene : NSObject <WryScene>

// `nextFactory` builds the scene to switch to once a game starts; it is invoked
// lazily (after new_game / load) because the world's asset load is heavy.
// `quit` is invoked by QUIT TO DESKTOP to ask the host to exit.
- (nonnull instancetype)initWithContext:(nonnull WryRenderContext*)context
                                  model:(std::shared_ptr<wry::model>)model
                                   next:(nonnull id<WryScene> (^)(void))nextFactory
                                   quit:(nonnull void (^)(void))quit;

@end

#endif /* WryMainMenuScene_h */
