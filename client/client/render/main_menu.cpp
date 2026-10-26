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

        // NEW GAME: fresh starting world, then drop into it.
        add("New Game", [g, shared] {
            // Fresh WorldState (its constructor installs the starting world).
            auto world = std::make_shared<WorldState>(*g);
            if (shared->on_world)
                shared->on_world(std::move(world));
        });

        // LOAD GAME: for now load the most recent save (enumerate_games
        // returns (filename, id) newest first); a real save picker is the
        // eventual UX.
        add("Load Game", [g, shared] {
            auto games = enumerate_games();
            if (!games.empty()) {
                auto world = std::make_shared<WorldState>(*g);
                world->load_from_save(games.front().second);
                if (shared->on_world)
                    shared->on_world(std::move(world));
            }
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
