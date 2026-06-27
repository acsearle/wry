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

#include "world_state.hpp"

// The top-level main menu, shown when no game is loaded.  Distinct from the
// in-game ESC menu (gui::MainMenuOverlay): this one *creates* games.  It hosts
// a column of buttons -- NEW GAME, LOAD GAME, JOIN GAME, QUIT TO DESKTOP --
// drawn with the shared 2D services (atlas / font / UI pipeline), and owns its
// own input (it dispatches events to the button column itself).

@interface WryMainMenuScene : NSObject <WryScene>

// On NEW / LOAD the menu builds a fresh `WorldState` (borrowing `gui`) and hands
// it to `nextFactory`, which wraps it in the world scene.  `quit` is invoked by
// QUIT TO DESKTOP to ask the host to exit.
- (nonnull instancetype)initWithContext:(nonnull WryRenderContext*)context
                                    gui:(nonnull wry::GuiContext*)gui
                                   next:(nonnull id<WryScene> (^)(std::shared_ptr<wry::WorldState>))nextFactory
                                   quit:(nonnull void (^)(void))quit;

@end

#endif /* WryMainMenuScene_h */
