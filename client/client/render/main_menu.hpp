//
//  main_menu.hpp
//  client
//
//  Created by Antony Searle on 2026-10-26.
//
//  The main menu's button panel: NEW GAME, LOAD GAME, JOIN GAME, SETTINGS
//  and QUIT TO DESKTOP, with what each does.  One definition for every
//  host that shows the menu: the Metal scene (render/WryMainMenuScene.mm)
//  paints and dispatches to it, the render-free host
//  (render/headless_host.hpp) lays it out and dispatches to it, and the
//  `widgets` and `press` commands see the same tree in either.
//

#ifndef main_menu_hpp
#define main_menu_hpp

#include <functional>
#include <memory>

#include "gui_widget.hpp"

namespace wry {

    struct GuiContext;
    struct WorldState;

    struct MainMenuActions {
        // NEW GAME and LOAD GAME built a WorldState (the starting scenario,
        // or the newest save loaded into it): show it
        std::function<void(std::shared_ptr<WorldState>)> on_world;
        // QUIT TO DESKTOP
        std::function<void()> on_quit;
    };

    // The panel; the actions are called from its buttons' click handlers,
    // on the thread that dispatches events to it (main), while the panel
    // lives.  `gui` is borrowed for the WorldStates built and for the
    // settings overlay
    std::unique_ptr<gui::ButtonPanel> make_main_menu(GuiContext& gui, MainMenuActions actions);

} // namespace wry

#endif /* main_menu_hpp */
