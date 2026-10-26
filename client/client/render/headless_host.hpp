//
//  headless_host.hpp
//  client
//
//  Created by Antony Searle on 2026-10-26.
//
//  The render-free host (--headless; docs/platforms.md section 3.5, step
//  2): the game with no window, device, scene chain or AppKit at all.
//  What remains is the app tier (GuiContext: the registry, the control
//  socket, the overlays), the main menu's button panel, and the
//  WorldState a game is, with its session (the host's relay and client,
//  or a joined connection).  The UI was only ever one source of commands;
//  the server drives the step cadence; the renderer was a consumer.  With
//  the consumer gone the WorldState's own Roots keep its Worlds alive.
//
//  main drives it: run() frames while commands or the session have work
//  and waits otherwise, on the control socket and the connection's inbox
//  (GuiContext::wait_for_input), repinning so an idle main never holds
//  the collector back.  A frame is the host's per-frame duties as
//  WryDelegate performs them, minus the drawing: the frame counter, the
//  notifications, the viewport, the commands, the input, the update, and
//  the menu's layout so `widgets` and `press` see its buttons.  Frames
//  keep a fixed dt, so a script sees the same timers on every run.
//
//  Screenshots have nothing to read back and are answered with an
//  error; --offscreen is the host that renders without a window.
//

#ifndef headless_host_hpp
#define headless_host_hpp

#include <chrono>
#include <memory>

#include "gui_context.hpp"
#include "gui_widget.hpp"

namespace wry {

    struct WorldState;

    class HeadlessHost {
    public:
        // The drawable a windowed run would have: 1920 x 1080 pixels viewed
        // at two pixels per point (a Retina window's default), so injected
        // coordinates and widget rects match a windowed run
        static constexpr float viewport_width_px = 1920.0f;
        static constexpr float viewport_height_px = 1080.0f;
        static constexpr float view_width_pt = 960.0f;
        static constexpr float view_height_pt = 540.0f;
        static constexpr double frame_dt = 1.0 / 60.0;

        // Borrows `gui`, whose scope must be set; takes its on_quit.  Starts
        // at the main menu
        explicit HeadlessHost(GuiContext& gui);
        // Drops the world, if one is showing: the caller is pinned (the
        // Root destructors shade) and the session ends gracefully,
        // blocking, as the WorldState's destructor does
        ~HeadlessHost();

        // Until `quit`: frames while there is work, waits otherwise
        void run();

        // One host frame
        void frame();
        // Block until there is input to frame on, or `timeout`
        void wait(std::chrono::milliseconds timeout);

        bool done() const { return _done; }
        WorldState* world() { return _world.get(); }

        HeadlessHost(HeadlessHost const&) = delete;
        HeadlessHost& operator=(HeadlessHost const&) = delete;

    private:
        GuiContext& _gui;
        // The main menu, showing while no world is
        std::unique_ptr<gui::ButtonPanel> _menu;
        std::shared_ptr<WorldState> _world;
        // Built by a menu button this frame; shown from the next
        std::shared_ptr<WorldState> _next_world;
        bool _done = false;
    };

} // namespace wry

#endif /* headless_host_hpp */
