//
//  main_menu.cpp
//  client
//
//  Created by Antony Searle on 2026-10-26.
//

#include "main_menu.hpp"

#include "gui_context.hpp"
#include "save.hpp"
#include "world_state.hpp"

namespace wry {

    std::unique_ptr<gui::ButtonPanel> make_main_menu(GuiContext& gui, MainMenuActions actions) {
        using namespace wry::gui;

        // The actions outlive the buttons' handlers: the panel owns them
        // through this shared block, which every handler captures
        auto shared = std::make_shared<MainMenuActions>(std::move(actions));
        GuiContext* g = &gui;

        auto panel = std::make_unique<ButtonPanel>();
        auto add = [&](StringView label, auto on_click) {
            panel->add(std::make_unique<Button>(label, std::move(on_click)));
        };

        // NEW GAME: the `new-game` command, which at the menu builds a
        // WorldState (its constructor installs the starting world) for the
        // host to show (GuiContext::on_world).
        add("New Game", [g] {
            (void) g->issue(command::Origin::gui, "new-game");
        });

        // LOAD GAME: for now load the most recent save (enumerate_games
        // returns (name, id) newest first) with the `load` command; a real
        // save picker is the eventual UX.
        add("Load Game", [g] {
            auto games = enumerate_games();
            if (!games.empty())
                (void) g->issue_with(command::Origin::gui, "load",
                                     String(StringView(games.front().first.c_str())));
        });

        // JOIN GAME: joining is the `join` command and --join today; a
        // picker for the address is the eventual UX.
        add("Join Game", [] { /* TODO: an address picker */ });

        // SETTINGS: the app-tier settings UI (GuiContext::overlays), the
        // same overlays the in-game menu opens.
        add("Settings", [g] {
            g->open_settings();
        });

        // QUIT TO DESKTOP: ask the host to end its run loop (graceful
        // shutdown).
        add("Quit to Desktop", [shared] {
            if (shared->on_quit)
                shared->on_quit();
        });

        return panel;
    }

} // namespace wry
