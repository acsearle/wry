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
#include "world_state.hpp"

namespace wry {

    HeadlessHost::HeadlessHost(GuiContext& gui)
    : _gui(gui) {
        assert(_gui.scope);
        _menu = make_main_menu(_gui, MainMenuActions{
            .on_world = [this](std::shared_ptr<WorldState> world) {
                _next_world = std::move(world);
            },
            .on_quit = [this] {
                _done = true;
            },
        });
        // The `quit` command ends the host the same way
        _gui.on_quit = [this] { _done = true; };
    }

    HeadlessHost::~HeadlessHost() {
        _gui.on_quit = nullptr;
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
            gui::MeasureContext context{};
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

            // Run frames until the issued pending replies have completed,
            // waiting for the session's input as run() would
            auto settle = [&] {
                for (int i = 0; i != 2000 && !gc.detached.empty(); ++i) {
                    if (!gc.idle())
                        host.frame();
                    else
                        host.wait(std::chrono::milliseconds(10));
                }
                assert(gc.detached.empty());
            };

            // The menu: laid out by the first frame, so its buttons can be
            // pressed in the second; the world shows from the one after
            host.frame();
            assert(host.world() == nullptr);
            [[maybe_unused]] auto pressed = gc.issue(Origin::gui, "press \"New Game\"");
            assert(pressed.reply.status == Reply::OK);
            host.frame();
            assert(host.world() != nullptr);
            assert(gc.world == host.world());

            // The relay's JOIN rides a step once the clock has run: until
            // then this client has no Player and its actions wait (the
            // smoke scripts' `frame 30` after NEW GAME allows for the same)
            auto joined = [&] {
                String text = gc.issue(Origin::gui, "session").reply.text;
                return std::string_view(text.data(), text.chars.size()).starts_with("host joined");
            };
            for (int i = 0; i != 2000 && !joined(); ++i) {
                if (!gc.idle())
                    host.frame();
                else
                    host.wait(std::chrono::milliseconds(10));
            }
            assert(joined());

            // A write, stepped in: `step` is pending until the relay's
            // steps have arrived and been applied
            [[maybe_unused]] auto paused = gc.issue(Origin::gui, "pause");
            assert(paused.reply.status == Reply::OK);
            [[maybe_unused]] auto written = gc.issue(Origin::gui, "write Coordinate(1, 1) OPCODE::SWAP");
            assert(written.reply.status == Reply::OK);
            [[maybe_unused]] auto before = gc.issue(Origin::gui, "get Coordinate(1, 1)");
            assert(before.reply.text == StringView("null"));
            [[maybe_unused]] auto stepped = gc.issue(Origin::gui, "step 2");
            assert(stepped.reply.status == Reply::PENDING);
            settle();
            [[maybe_unused]] auto after = gc.issue(Origin::gui, "get Coordinate(1, 1)");
            assert(after.reply.text == StringView("OPCODE::SWAP"));

            // A screenshot is refused by the next frame
            [[maybe_unused]] auto shot = gc.issue(Origin::gui, "screenshot \"/dev/null\"");
            assert(shot.reply.status == Reply::PENDING);
            host.frame();
            host.frame();
            assert(gc.detached.empty());
            assert(gc.history.back().status == Reply::ERROR);

            // quit ends the loop
            assert(!host.done());
            [[maybe_unused]] auto quit_issued = gc.issue(Origin::gui, "quit");
            assert(quit_issued.reply.status == Reply::OK);
            assert(host.done());

            co_await host.world()->end_session(std::chrono::milliseconds(200));
        }
        (void) co_await scope.join();
    };

} // namespace wry
