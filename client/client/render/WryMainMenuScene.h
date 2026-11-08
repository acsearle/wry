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

// NEW / LOAD issue `new-game` / `load`, which build a fresh `WorldState`
// (borrowing `gui`) and hand it to the host's GuiContext::on_world, which wraps
// it in the world scene.  `quit` is invoked by QUIT TO DESKTOP to ask the host
// to exit.
- (nonnull instancetype)initWithContext:(nonnull WryRenderContext*)context
                                    gui:(nonnull wry::GuiContext*)gui
                                   quit:(nonnull void (^)(void))quit;

@end

#endif /* WryMainMenuScene_h */
