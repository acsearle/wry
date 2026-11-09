//
//  headless_host.cpp
//  client
//
//  Created by Antony Searle on 2026-10-26.
//

#include "headless_host.hpp"

#include "garbage_collected.hpp"
#include "main_menu.hpp"
#include "test.hpp"
#include "test_support.hpp"
#include "world_state.hpp"

namespace wry {

    HeadlessHost::HeadlessHost(GuiContext& gui)
    : _gui(gui) {
        assert(_gui.scope);
        _menu = make_main_menu(_gui, MainMenuActions{
            .on_quit = [this] {
                _done = true;
            },
        });
        // The `quit` command ends the host the same way
        _gui.on_quit = [this] { _done = true; };
        // `new-game` and `load` at the menu (its buttons issue them) built
        // a world: shown from the next frame
        // The later of the two in one frame wins, as in the windowed host:
        // a world built after `main-menu` cancels it, and `main-menu` after
        // a world built drops that world at the end of the frame
        _gui.on_world = [this](std::shared_ptr<WorldState> world) {
            _next_world = std::move(world);
            _to_menu = false;
        };
        // `main-menu`: the world is dropped at the end of this frame
        // (nothing is destroyed here: the command may be running on the
        // very world being dropped; the end of the frame drops it)
        _gui.on_main_menu = [this] { _to_menu = true; };
    }

    HeadlessHost::~HeadlessHost() {
        _gui.on_quit = nullptr;
        _gui.on_world = nullptr;
        _gui.on_main_menu = nullptr;
        _gui.scene_widgets = nullptr;
        _next_world.reset();
        _world.reset();
    }

    void HeadlessHost::run() {
        while (!_done) {
            if (!_gui.idle()) {
                frame();
            } else {
                wait(std::chrono::milliseconds(100));
                mutator_repin();
            }
        }
    }

    void HeadlessHost::wait(std::chrono::milliseconds timeout) {
        _gui.wait_for_input(timeout);
    }

    void HeadlessHost::frame() {
        // The host's per-frame duties, as WryDelegate's -render performs
        // them: the frame counter stamps command history; background work
        // posts to the log; what injected positions and widget queries are
        // relative to; then the commands, before input, so injected events
        // are dispatched this frame
        ++_gui.frame;
        _gui.drain_notifications();
        _gui.view_size_pt = float2{view_width_pt, view_height_pt};
        _gui.viewport_size = float2{viewport_width_px, viewport_height_px};
        _gui.scene_widgets = _world ? nullptr : _menu.get();
        _gui.service_commands();

        // Input: the world's pump, or the menu's (the app-tier overlays
        // first, then the button column, locations promoted from points to
        // pixels as WryMainMenuScene does)
        if (_world) {
            _world->handle_events(_gui.view_size_pt);
        } else {
            const float sx = viewport_width_px / view_width_pt;
            const float sy = viewport_height_px / view_height_pt;
            while (!_gui.events.empty()) {
                gui::Event e = _gui.events.pop_front();
                e.location.x *= sx;
                e.location.y *= sy;
                if (!_gui.overlays.dispatch(e))
                    _menu->on_event(e);
            }
        }
        // `quit` this frame: nothing more to do
        if (_done)
            return;

        if (_world) {
            _world->update(frame_dt);
        } else {
            // Lay the menu out as the scene does, centered, so `widgets` and
            // `press` see its buttons' rects.  There is no font, so the
            // widgets measure by their fallbacks; the layout differs from a
            // rendered one, which only `widgets` can tell
            gui::MeasureContext context{nullptr, _gui.pixels_per_point()};
            gui::SizeConstraints constraints = gui::SizeConstraints::loose(_gui.viewport_size);
            gui::Size desired = _menu->measure(constraints, context);
            const float x = (viewport_width_px - desired.w) * 0.5f;
            const float y = (viewport_height_px - desired.h) * 0.5f;
            _menu->arrange(rect<float>{x, y, x + desired.w, y + desired.h});
        }

        // Screenshots: nothing is rendered to read back
        for (auto& request : _gui.screenshot_requests) {
            request->error = String("screenshot: this host renders nothing; run with --offscreen");
            request->ready.store_release(true);
        }
        _gui.screenshot_requests.clear();

        // `main-menu` this frame: the world goes (its destructor ends the
        // session, blocking, as the scene's release does in a window), and
        // the menu shows from the next frame
        if (_to_menu) {
            _to_menu = false;
            _world.reset();
            _next_world.reset();   // a world built earlier this frame goes too
        }

        // A menu button built a world this frame: show it from the next.
        // The renderer would size the viewport and the shadow map here;
        // the uniforms are regenerated for the fixed viewport, so the
        // cursor projects and the map pipeline runs as in a window
        if (_next_world) {
            _world = std::move(_next_world);
            _world->_regenerate_uniforms();
        }
    }

    // ---- Tests ----------------------------------------------------------

    // The host's loop by hand: the menu's NEW GAME through `press`, a
    // write stepped in under the host's own relay, a screenshot refused,
    // and `quit`.  The same session wry_net_smoke.py runs out of process
    define_test("headless_host") {
        using command::Origin;
        using command::Reply;
        coroutine::Nursery scope;
        GuiContext gc;
        gc.scope = &scope;
        {
            HeadlessHost host(gc);

            // Run frames until `done` holds.  A frame applies whatever the
            // relay has delivered; what it has not delivered yet arrives
            // through the pool and the reactor, so between frames with
            // nothing to apply this test, on a pool worker, sleeps on the
            // reactor rather than spinning (io/test_support.hpp: a test
            // never blocks a worker).  The bound is the hang detector, not
            // a frame count: a frame count is a timing claim in disguise
            // (2000 of them was a second, which a cold offscreen launch
            // under a sanitizer exceeded while its backdrop decoded on the
            // pool).  Note that gc.idle() is false while a detached reply
            // is pending, so it cannot be the signal to sleep on here
            auto frames_until = [&](auto done) -> coroutine::Task {
                const auto deadline = testing::soon();
                while (!done()) {
                    require(std::chrono::steady_clock::now() < deadline);
                    host.frame();
                    WorldState* world = host.world();
                    if (!(world && world->has_server_input()))
                        co_await testing::pause(std::chrono::milliseconds(1));
                }
            };
            auto settle = [&]() -> coroutine::Task {
                co_await frames_until([&] { return gc.detached.empty(); });
            };

            // The menu: laid out by the first frame, so its buttons can be
            // pressed in the second; the world shows from the one after
            host.frame();
            require(host.world() == nullptr);
            [[maybe_unused]] auto pressed = gc.issue(Origin::gui, "press \"New Game\"");
            require(pressed.reply.status == Reply::OK);
            host.frame();
            require(host.world() != nullptr);
            require(gc.world == host.world());

            // The relay's JOIN rides a step once the clock has run: until
            // then this client has no Player and its actions wait (the
            // smoke scripts' `frame 30` after NEW GAME allows for the same)
            auto joined = [&] {
                String text = gc.issue(Origin::gui, "session").reply.text;
                return std::string_view(text.data(), text.chars.size()).starts_with("host joined");
            };
            co_await frames_until(joined);
            require(joined());

            // A write, stepped in: `step` is pending until that many more
            // steps have arrived and been applied.  Which step carries the
            // write is not fixed: the pause and the steps are posted to
            // the relay while the write crosses the client's socket, so
            // the steps can be finalized before it arrives, and the steps
            // a reply counts can be ones already on their way.  So step
            // until it shows, as the smoke scripts do
            [[maybe_unused]] auto paused = gc.issue(Origin::gui, "pause");
            require(paused.reply.status == Reply::OK);
            [[maybe_unused]] auto written = gc.issue(Origin::gui, "write Coordinate(1, 1) OPCODE::SWAP");
            require(written.reply.status == Reply::OK);
            auto landed = [&] {
                return gc.issue(Origin::gui, "get Coordinate(1, 1)").reply.text == StringView("OPCODE::SWAP");
            };
            [[maybe_unused]] bool before = landed();
            require(!before);
            for (const auto deadline = testing::soon(); !landed();) {
                require(std::chrono::steady_clock::now() < deadline);
                [[maybe_unused]] auto stepped = gc.issue(Origin::gui, "step");
                require(stepped.reply.status == Reply::PENDING);
                co_await settle();
            }

            // A screenshot is refused by the next frame
            [[maybe_unused]] auto shot = gc.issue(Origin::gui, "screenshot \"/dev/null\"");
            require(shot.reply.status == Reply::PENDING);
            host.frame();
            host.frame();
            require(gc.detached.empty());
            require(gc.history.back().status == Reply::ERROR);

            // quit ends the loop
            require(!host.done());
            [[maybe_unused]] auto quit_issued = gc.issue(Origin::gui, "quit");
            require(quit_issued.reply.status == Reply::OK);
            require(host.done());

            co_await host.world()->end_session(std::chrono::milliseconds(200));
        }
        (void) co_await scope.join();
    };

    // Quit to the main menu and back: `new-game` issued at the menu shows
    // a world from the next frame; `main-menu` drops it at the end of the
    // frame it is issued in, after which the commands have no world and
    // the menu's buttons are live again.  The world's session is ended by
    // awaiting first, since the drop would end it blocking, and a test
    // never blocks a worker
    define_test("headless_host_main_menu") {
        using command::Origin;
        using command::Reply;
        coroutine::Nursery scope;
        GuiContext gc;
        gc.scope = &scope;
        {
            HeadlessHost host(gc);
            auto expect = [&](const char* line, Reply::Status want) {
                [[maybe_unused]] const Reply::Status got = gc.issue(Origin::gui, StringView(line)).reply.status;
                require(got == want);
            };
            host.frame();
            require(host.world() == nullptr);
            expect("main-menu", Reply::ERROR);           // no game to quit
            expect("new-game", Reply::OK);
            host.frame();
            require(host.world() != nullptr && gc.world == host.world());

            co_await host.world()->end_session(std::chrono::milliseconds(200));
            expect("main-menu", Reply::OK);
            require(host.world() != nullptr);                // until the frame ends
            host.frame();
            require(host.world() == nullptr && gc.world == nullptr);
            expect("save", Reply::ERROR);                   // no world to save

            // The menu again: laid out by this frame, pressed in the next
            host.frame();
            expect("press \"New Game\"", Reply::OK);
            host.frame();
            require(host.world() != nullptr && gc.world == host.world());
            co_await host.world()->end_session(std::chrono::milliseconds(200));
        }
        (void) co_await scope.join();
    };

} // namespace wry
