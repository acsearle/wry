//
//  gui_context.hpp
//  client
//
//  Created by Antony Searle on 2026-06-27.
//

#ifndef gui_context_hpp
#define gui_context_hpp

#include <chrono>
#include <mutex>
#include <vector>

#include "command.hpp"
#include "gui_event.hpp"
#include "gui_overlay.hpp"
#include "settings.hpp"
#include "simd.hpp"
#include "string.hpp"

namespace wry {

    struct GuiContext;
    struct WorldState;

    // What a command handler runs against (core/docs/commands.md).  The
    // registry and history are app-tier, so they live on GuiContext; the
    // world, when a world scene is showing, is reached through it.
    struct CommandContext {
        GuiContext& gui;
        WorldState* world;      // null outside the world scene
    };

    using CommandRegistry = command::Registry<CommandContext>;

    // Registration (commands.md section 5): explicit, called once by the
    // GuiContext constructor, so the command list is the same whatever
    // scene is showing.
    void register_app_commands(CommandRegistry&);      // gui_context.cpp
    void register_world_commands(CommandRegistry&);    // world_state.cpp

    // Host-owned, scene-agnostic GUI state -- the generic "app" tier, lifted out
    // of the world state so it outlives any single scene.  The host
    // (WryDelegate) owns one; the WorldState and the scenes borrow it.
    struct GuiContext {

        // Raw input: WryDelegate's NSResponder callbacks translate each NSEvent
        // into a gui::Event and push it here; the per-frame pump (or a scene's
        // own handler) drains it.  Single-threaded (main thread); no lock.
        gui::EventQueue events;

        // Drawable-pixel viewport size, set by the active scene on resize.
        float2 viewport_size = {};

        // User settings.  Constructed as the compiled-in defaults so the
        // keymap is live from the first event; the host replaces them
        // with load_settings(settings_paths, ...) at startup (after the
        // cwd is established).  Assignment replaces the contents, not
        // the address, so pointers wired to members below stay valid.
        Settings settings = default_settings();
        SettingsPaths settings_paths = wry::settings_paths();

        // Generic overlays, available regardless of scene: the floating log and
        // the drop-down console.  (Scene-specific overlays -- the palette, the
        // in-game menu -- still live on the model.)
        gui::LogOverlay log_overlay;
        gui::ConsoleOverlay console_overlay;

        // Settings UI, app-tier so the same overlays serve the main-menu
        // scene and the in-game menu.  Pushed dynamically onto `overlays`
        // by open_settings() / the KEY BINDINGS button; popped via
        // wants_close.
        gui::SettingsOverlay settings_overlay;
        gui::KeyBindingsOverlay key_bindings_overlay;

        // App-tier overlay stack (host-owned): the floating log + drop-down
        // console, available to every scene.  Scenes dispatch input to it first
        // (the console swallows keystrokes when open) and paint it on top of
        // their own content.  Set up in the constructor below.
        gui::OverlayStack overlays;

        // Commands (core/docs/commands.md).  Every input path -- key
        // bindings, GUI gestures, the console -- issues through issue().
        CommandRegistry commands;

        // Every issued invocation, in issue order across origins.  Text
        // only: holds no Terms.
        std::vector<command::HistoryEntry> history;
        uint64_t next_seq = 1;

        // Host frame counter (WryDelegate bumps it once per frame); stamps
        // history so a replay can reissue each line at its frame.
        uint64_t frame = 0;

        // The live world scene's state, for handlers; set and cleared by
        // WorldState itself.
        WorldState* world = nullptr;

        // Run a command line from `origin`, record it if it was issued,
        // and echo it to the console.
        command::Outcome issue(command::Origin origin, StringView line);

        // Issue `name` with typed arguments (GUI gestures): the same path,
        // via the canonical text.  An argument with no literal form cannot
        // be issued, and is reported instead.
        template<command::CommandType... Ts>
        command::Outcome issue_with(command::Origin origin, StringView name, Ts const&... args) {
            auto line = command::format_invocation(name, args...);
            if (!line)
                return _refuse_unprintable(name);
            return issue(origin, *line);
        }

        command::Outcome _refuse_unprintable(StringView name);

        GuiContext() {
            console_overlay.set_log(&log_overlay);
            console_overlay.set_keymap(&settings.keymap);
            console_overlay.set_issue([this](command::Origin origin, StringView line) {
                (void) issue(origin, line);
            });
            register_app_commands(commands);
            register_world_commands(commands);
            settings_overlay.set_context(this);
            key_bindings_overlay.set_context(this);
            overlays.push(&log_overlay);       // bottom: floating status text
            overlays.push(&console_overlay);   // top: drop-down console
        }

        // Open the settings UI over whatever scene is showing.  Both the
        // main-menu scene's SETTINGS button and the in-game menu's route
        // here.  Idempotent: a second call while open is a no-op.
        void open_settings() {
            if (!overlays.contains(&settings_overlay))
                overlays.push(&settings_overlay);
        }

        void append_log(StringView v,
                        std::chrono::steady_clock::duration endurance
                            = std::chrono::seconds(5)) {
            log_overlay.append(v, endurance);
        }

        // Background work (e.g. an async save completing on a worker thread)
        // can't touch the log overlay directly, so it posts a short message
        // here; the main-thread pump drains them.  Messages are string literals
        // (static lifetime), so storing the pointer is safe.
        std::mutex notifications_mutex;
        std::vector<const char*> notifications;

        void post_notification(const char* message) {  // any thread
            std::scoped_lock lock{notifications_mutex};
            notifications.push_back(message);
        }

        void drain_notifications() {  // main thread, once per frame
            std::vector<const char*> pending;
            {
                std::scoped_lock lock{notifications_mutex};
                pending.swap(notifications);
            }
            for (const char* m : pending)
                append_log(m);
        }
    };

} // namespace wry

#endif /* gui_context_hpp */
