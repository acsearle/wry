//
//  gui_context.hpp
//  client
//
//  Created by Antony Searle on 2026-06-27.
//

#ifndef gui_context_hpp
#define gui_context_hpp

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "atomic.hpp"

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

    // A sequential stream of command lines (commands.md section 8): the
    // console, and each control-socket connection.  A source issues
    // nothing further while one of its invocations is pending; later lines
    // wait in its queue.
    struct CommandSource {
        command::Origin origin;
        std::deque<String> queued;
        uint64_t pending_seq = 0;
        std::shared_ptr<command::Pending> pending;

        explicit CommandSource(command::Origin o) : origin(o) {}
        bool busy() const { return pending != nullptr || !queued.empty(); }
    };

    // A screenshot the host has yet to take.  The host blits the next
    // presentable texture (RGBA16Float, linear) into a buffer and, from the
    // command buffer's completion handler (a Metal thread), copies the
    // pixels here, then stores `ready` with release.  The main thread's
    // acquire load of `ready` == true synchronizes-with that store, so the
    // pixel writes happen-before its reads.  The completion handler touches
    // nothing else, and no GC object.
    struct ScreenshotRequest {
        String path;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint16_t> half_rgba;    // width * height * 4 IEEE halves
        String error;                       // set instead of pixels on failure
        Atomic<bool> ready{false};
    };

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

        // Set by the host each frame: the view's size in points (event
        // coordinates), and the current scene's own widget tree if it has
        // one (the main menu's).  Widget rects are in drawable pixels,
        // viewport_size; injected events are in points.
        float2 view_size_pt = {};
        gui::Widget const* scene_widgets = nullptr;

        // Where injected pointer events last put the pointer (points), for
        // injected scrolls, which carry a location.
        float2 injected_pointer_pt = {};

        // Screenshots waiting for the next presented frame; the host takes
        // them (WryDelegate) and the screenshot command's pending reply
        // writes the PNG once `ready`.
        std::vector<std::shared_ptr<ScreenshotRequest>> screenshot_requests;

        // The console's command stream.
        CommandSource console_source{command::Origin::console};

        // Pending replies of invocations issued outside a source (GUI
        // gestures, bindings, tests): polled, but sequence nothing.
        struct Detached {
            uint64_t seq;
            command::Origin origin;
            std::shared_ptr<command::Pending> pending;
        };
        std::vector<Detached> detached;

        // Queue `line` on `source`, or issue it now if the source is idle.
        void submit(CommandSource& source, StringView line);

        // Once per frame (the host, before input): poll pending replies,
        // complete those that finished, and issue lines that were waiting
        // behind them.
        void service_commands();

        // Abandon `source`'s pending invocation (completing it CANCELLED)
        // and drop the lines queued behind it.
        void cancel(CommandSource& source);

        // Run a line now, record and echo it, but leave a pending reply
        // for the caller to own.
        command::Outcome _issue_now(command::Origin origin, StringView line);
        void _echo_input(StringView line);

        // Record the final reply of invocation `seq` and echo it.
        void _complete(uint64_t seq, command::Origin origin, command::Reply const& reply);
        void _drain(CommandSource& source);

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
                // Typed lines are sequenced on the console's stream; its
                // toggle key (a binding) runs at once.
                if (origin == command::Origin::console)
                    submit(console_source, line);
                else
                    (void) issue(origin, line);
            });
            console_overlay.set_cancel([this] { cancel(console_source); });
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
