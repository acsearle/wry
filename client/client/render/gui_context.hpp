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

#include "gui_event.hpp"
#include "gui_overlay.hpp"
#include "simd.hpp"
#include "string.hpp"

namespace wry {

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

        // Generic overlays, available regardless of scene: the floating log and
        // the drop-down console.  (Scene-specific overlays -- the palette, the
        // in-game menu -- still live on the model.)
        gui::LogOverlay log_overlay;
        gui::ConsoleOverlay console_overlay;

        // App-tier overlay stack (host-owned): the floating log + drop-down
        // console, available to every scene.  Scenes dispatch input to it first
        // (the console swallows keystrokes when open) and paint it on top of
        // their own content.  Set up in the constructor below.
        gui::OverlayStack overlays;

        GuiContext() {
            console_overlay.set_log(&log_overlay);
            overlays.push(&log_overlay);       // bottom: floating status text
            overlays.push(&console_overlay);   // top: drop-down console
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
