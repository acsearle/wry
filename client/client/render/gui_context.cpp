//
//  gui_context.cpp
//  client
//
//  Created by Antony Searle on 2026-10-12.
//

#include <cstdio>
#include <cstring>
#include <filesystem>

#include "gui_context.hpp"
#include "gui_widget.hpp"
#include "image.hpp"
#include "test.hpp"
#include "world_state.hpp"

namespace wry::command {

    // Key combos, as the keymap spells them: KeyCombo("Shift+R"); a bare
    // word or a string where a KeyCombo is expected (`key Shift+R`; a
    // combo containing a delimiter must be quoted, `key "="`).
    template<> struct Type<gui::KeyCombo> {
        static constexpr const char* name = "KeyCombo";

        static std::expected<gui::KeyCombo, Error> read(Node const& node) {
            StringView text;
            if (node.kind == Node::TAGGED) {
                if (auto e = expect_tagged(node, StringView(name), 1))
                    return std::unexpected(std::move(*e));
                if (node.children[0].kind != Node::STRING)
                    return std::unexpected(make_error(node.children[0].offset, "expected a string"));
                text.reset(node.children[0].text);
            } else {
                text.reset(node.text);
            }
            if (auto combo = gui::combo_from_string(text))
                return *combo;
            String message("unknown key combo ");
            message.append(text);
            return std::unexpected(Error{std::move(message), node.offset});
        }

        static void print(String& out, gui::KeyCombo const& x) {
            out.append(StringView("KeyCombo("));
            print_quoted(out, gui::string_from_combo(x));
            out.push_back(')');
        }
    };

} // namespace wry::command

namespace wry {

    using command::Category;
    using command::Origin;
    using command::Outcome;
    using command::Param;
    using command::Reply;
    using command::make_command;

    namespace {

        // Output lines of a reply, split at newlines.
        template<typename F>
        void for_each_line(StringView text, F&& f) {
            const char* first = text.chars.begin();
            const char* last = text.chars.end();
            while (first != last) {
                const char* p = first;
                while (p != last && *p != '\n')
                    ++p;
                f(StringView(trusted, first, p));
                first = (p == last) ? p : p + 1;
            }
        }

    } // namespace

    // ----------------------------------------------------------------
    // Issue and execution (commands.md sections 6 and 8).

    void GuiContext::_echo_input(StringView text) {
        String echo("> ");
        echo.append(text);
        console_overlay.print(echo);
    }

    namespace {

        // Echo a reply's text: errors marked, and failures from gestures
        // and bindings also floated in the log, since the console is
        // usually closed.
        void echo_reply(GuiContext& gui, Origin origin, Reply const& reply) {
            const bool failed = reply.status == Reply::ERROR;
            for_each_line(reply.text, [&](StringView line) {
                if (failed) {
                    String message("! ");
                    message.append(line);
                    gui.console_overlay.print(message);
                } else {
                    gui.console_overlay.print(line);
                }
            });
            if (failed && origin != Origin::console)
                gui.append_log(reply.text);
        }

    } // namespace

    Outcome GuiContext::_issue_now(Origin origin, StringView text) {
        CommandContext context{*this, world};
        Outcome outcome = commands.run(context, text);

        // History records issued invocations: those that bound.  A line
        // that failed to parse or bind issued nothing.  Text only.
        if (!outcome.line.empty()) {
            command::HistoryEntry entry;
            entry.seq = next_seq++;
            entry.frame = frame;
            entry.origin = origin;
            entry.line = outcome.line;
            entry.status = outcome.reply.status;
            if (outcome.reply.status != Reply::PENDING) {
                entry.reply = outcome.reply.text;
                entry.completed_frame = frame;
            }
            history.push_back(std::move(entry));
        }

        // Echo everything but console input (echoed as typed, when it was
        // submitted) as the canonical line it issued, tagged with its
        // origin -- so the console shows what the mouse and keys did in
        // its own language.
        if (origin != Origin::console && !outcome.line.empty()) {
            String echo("[");
            echo.append(StringView(command::name_from_origin(origin)));
            echo.append(StringView("] "));
            echo.append(outcome.line);
            console_overlay.print(echo);
        }
        if (outcome.reply.status != Reply::PENDING)
            echo_reply(*this, origin, outcome.reply);
        return outcome;
    }

    Outcome GuiContext::issue(Origin origin, StringView text) {
        if (origin == Origin::console)
            _echo_input(text);
        Outcome outcome = _issue_now(origin, text);
        if (outcome.reply.status == Reply::PENDING)
            detached.push_back(Detached{history.back().seq, origin, outcome.reply.pending});
        return outcome;
    }

    void GuiContext::submit(CommandSource& source, StringView text) {
        if (source.origin == Origin::console)
            _echo_input(text);
        source.queued.push_back(String(text));
        _drain(source);
    }

    void GuiContext::_drain(CommandSource& source) {
        while (!source.pending && !source.queued.empty()) {
            String line = std::move(source.queued.front());
            source.queued.pop_front();
            Outcome outcome = _issue_now(source.origin, line);
            if (outcome.reply.status == Reply::PENDING) {
                source.pending = outcome.reply.pending;
                source.pending_seq = history.back().seq;
            }
        }
    }

    void GuiContext::_complete(uint64_t seq, Origin origin, Reply const& reply) {
        assert(reply.status != Reply::PENDING);
        // Entries are in seq order; a pending one is recent.
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (it->seq != seq)
                continue;
            it->status = reply.status;
            it->reply = reply.text;
            it->completed_frame = frame;
            break;
        }
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "#%llu %s", (unsigned long long)seq,
                      reply.status == Reply::OK ? "done"
                      : reply.status == Reply::CANCELLED ? "cancelled" : "failed");
        console_overlay.print(StringView(buffer));
        if (reply.status != Reply::CANCELLED)
            echo_reply(*this, origin, reply);
    }

    void GuiContext::service_commands() {
        auto service = [this](CommandSource& source) {
            if (source.pending) {
                if (auto reply = source.pending->poll()) {
                    const uint64_t seq = source.pending_seq;
                    source.pending.reset();
                    _complete(seq, source.origin, *reply);
                }
            }
            _drain(source);
        };
        service(console_source);
        for (size_t i = 0; i != detached.size(); ) {
            if (auto reply = detached[i].pending->poll()) {
                Detached done = std::move(detached[i]);
                detached.erase(detached.begin() + (std::ptrdiff_t)i);
                _complete(done.seq, done.origin, *reply);
            } else {
                ++i;
            }
        }
    }

    void GuiContext::cancel(CommandSource& source) {
        source.queued.clear();
        if (!source.pending)
            return;
        source.pending->cancel();
        const uint64_t seq = source.pending_seq;
        source.pending.reset();
        _complete(seq, source.origin, Reply::cancelled());
    }

    Outcome GuiContext::_refuse_unprintable(StringView name) {
        String message("cannot issue ");
        message.append(name);
        message.append(StringView(": an argument has no literal form"));
        String echo("! ");
        echo.append(message);
        console_overlay.print(echo);
        append_log(message);
        return Outcome{{}, Reply::error(std::move(message))};
    }

    // ----------------------------------------------------------------
    // Asynchronous commands, input injection, widget queries.

    namespace {

        // `frame n`: done once n more host frames have begun.
        struct FramePending final : command::Pending {
            GuiContext& gui;
            uint64_t target;
            FramePending(GuiContext& g, uint64_t t) : gui(g), target(t) {}
            std::optional<Reply> poll() override {
                if (gui.frame < target)
                    return std::nullopt;
                return Reply::ok();
            }
        };

        // `screenshot`: done once the host has read back a presented frame
        // and we have written it out.
        struct ScreenshotPending final : command::Pending {
            GuiContext& gui;
            std::shared_ptr<ScreenshotRequest> request;
            ScreenshotPending(GuiContext& g, std::shared_ptr<ScreenshotRequest> r)
            : gui(g), request(std::move(r)) {}

            std::optional<Reply> poll() override {
                // Pairs with the completion handler's store_release; see
                // ScreenshotRequest.
                if (!request->ready.load_acquire())
                    return std::nullopt;
                if (!request->error.empty())
                    return Reply::error(request->error);
                // The presentable holds linear values; encode to sRGB 8-bit.
                const size_t w = request->width;
                const size_t h = request->height;
                matrix<RGBA8Unorm_sRGB> image(h, w);
                const uint16_t* p = request->half_rgba.data();
                for (size_t i = 0; i != h; ++i) {
                    for (size_t j = 0; j != w; ++j, p += 4) {
                        float c[3];
                        for (int k = 0; k != 3; ++k) {
                            _Float16 x;
                            std::memcpy(&x, p + k, sizeof(x));
                            c[k] = (float)x;
                        }
                        image[i, j] = RGBA8Unorm_sRGB(c[0], c[1], c[2], 1.0f);
                    }
                }
                to_png(image, request->path);
                return Reply::ok(request->path);
            }

            void cancel() override {
                // Withdraw it if the host has not taken it yet.
                auto& q = gui.screenshot_requests;
                q.erase(std::remove(q.begin(), q.end(), request), q.end());
            }
        };

        // ---- Injection.  Events go on the queue in points, exactly as
        // WryDelegate puts real ones there, and so are coalesced and
        // dispatched the same way.

        void inject_pointer(GuiContext& gui, WryEventKind kind, float2 at,
                            gui::MouseButton button = gui::MouseButton::Left) {
            gui::Event e{};
            e.kind = kind;
            e.location = at;
            e.button = button;
            gui.injected_pointer_pt = at;
            gui.events.push(e);
        }

        void inject_click(GuiContext& gui, float2 at, gui::MouseButton button) {
            inject_pointer(gui, WryEventKindMouseMove, at, button);
            inject_pointer(gui, WryEventKindMouseDown, at, button);
            inject_pointer(gui, WryEventKindMouseUp, at, button);
        }

        // A key event; `text` is what the keystroke inserts (UTF-8), if
        // anything.
        void inject_key(GuiContext& gui, WryEventKind kind, uint32_t key,
                        uint16_t mods, StringView text) {
            gui::Event e{};
            e.kind = kind;
            e.key = key;
            e.mods.bits = mods;
            if (kind == WryEventKindKeyDown) {
                const size_t n = std::min(text.chars.size(), sizeof(e.text) - 1);
                std::memcpy(e.text, text.chars.begin(), n);
            }
            gui.events.push(e);
        }

        // The text a combo's keystroke inserts: printable ASCII keys
        // without Ctrl / Cmd, Shift uppercasing letters.
        String text_for_combo(gui::KeyCombo combo) {
            using gui::Modifiers;
            String text;
            if (combo.key < 0x20 || combo.key > 0x7E
                || (combo.mods & (Modifiers::Ctrl | Modifiers::Cmd)))
                return text;
            char ch = (char)combo.key;
            if ((combo.mods & Modifiers::Shift) && ch >= 'a' && ch <= 'z')
                ch = (char)(ch - 'a' + 'A');
            text.push_back(ch);
            return text;
        }

        std::expected<gui::MouseButton, String> button_from_name(StringView name) {
            if (name == StringView("left"))   return gui::MouseButton::Left;
            if (name == StringView("right"))  return gui::MouseButton::Right;
            if (name == StringView("middle")) return gui::MouseButton::Middle;
            return std::unexpected(String("button must be left, right or middle"));
        }

        // ---- Widgets.  Rects are drawable pixels; injected events are
        // points.

        std::optional<float2> pixels_to_points(GuiContext const& gui, float2 px) {
            if (!(gui.viewport_size.x > 0.0f && gui.viewport_size.y > 0.0f
                  && gui.view_size_pt.x > 0.0f && gui.view_size_pt.y > 0.0f))
                return std::nullopt;
            return px * gui.view_size_pt / gui.viewport_size;
        }

        rect<float> intersect(rect<float> a, rect<float> b) {
            return rect<float>{std::max(a.a.x, b.a.x), std::max(a.a.y, b.a.y),
                               std::min(a.b.x, b.b.x), std::min(a.b.y, b.b.y)};
        }

        bool contains(rect<float> r, float2 p) {
            return r.a.x <= p.x && p.x < r.b.x && r.a.y <= p.y && p.y < r.b.y;
        }

        // Visit every widget showing, topmost tree first (the order input
        // reaches them): app-tier overlays, then the world's overlays, then
        // the scene's own tree.  f(widget, visible clip, depth, tree name).
        template<typename F>
        void for_each_widget(GuiContext const& gui, F&& f) {
            const rect<float> screen{0.0f, 0.0f, gui.viewport_size.x, gui.viewport_size.y};
            std::function<void(gui::Widget const&, rect<float>, int, const char*)> walk =
                [&](gui::Widget const& w, rect<float> clip, int depth, const char* tree) {
                    f(w, clip, depth, tree);
                    rect<float> inner = w.clips_children() ? intersect(clip, w.bounds()) : clip;
                    w.for_each_child([&](gui::Widget const& child) {
                        walk(child, inner, depth + 1, tree);
                    });
                };
            gui.overlays.for_each_top_down([&](gui::Overlay const& o) {
                if (auto* root = o.widget_root())
                    walk(*root, screen, 0, "app overlay");
            });
            if (gui.world)
                gui.world->_stack.for_each_top_down([&](gui::Overlay const& o) {
                    if (auto* root = o.widget_root())
                        walk(*root, screen, 0, "world overlay");
                });
            if (gui.scene_widgets)
                walk(*gui.scene_widgets, screen, 0, "scene");
        }

        void append_number(String& out, float x) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", (double)x);
            out.append(StringView(buffer));
        }

    } // namespace

    void register_app_commands(CommandRegistry& registry) {
        using Ctx = CommandContext;

        registry.add(make_command<Ctx>(
            "help", "List commands, or describe one", Category::local,
            std::tuple{
                Param<Ctx, String>{"command", "the command to describe", String()},
            },
            [](Ctx& c, String const& name) {
                String text = command::help_text(c.gui.commands, name);
                // Drop the list's trailing newline.
                if (!text.empty() && text.back() == U'\n')
                    text.pop_back();
                return Reply::ok(std::move(text));
            }));

        registry.add(make_command<Ctx>(
            "history", "Show recent invocations from every origin", Category::local,
            std::tuple{
                Param<Ctx, int64_t>{"count", "how many", int64_t{20}},
            },
            [](Ctx& c, int64_t count) {
                if (count < 0)
                    return Reply::error(String("count must be non-negative"));
                auto const& h = c.gui.history;
                size_t first = h.size() - std::min<size_t>(h.size(), (size_t)count);
                String text;
                for (size_t i = first; i != h.size(); ++i) {
                    if (i != first)
                        text.push_back('\n');
                    text.append(command::format_history_entry(h[i]));
                }
                return Reply::ok(std::move(text));
            }));

        registry.add(make_command<Ctx>(
            "toggle-console", "Show or hide the console", Category::local,
            std::tuple<>{},
            [](Ctx& c) {
                auto& console = c.gui.console_overlay;
                console.set_active(!console.active());
                c.gui.append_log(console.active() ? "[~] Show console" : "[~] Hide console");
                return Reply::ok();
            }));

        // ---- Clock (frames are local; simulation steps are the world's).

        registry.add(make_command<Ctx>(
            "frame", "Wait for n host frames to begin", Category::local,
            std::tuple{
                Param<Ctx, int64_t>{"n", "how many frames", int64_t{1}},
            },
            [](Ctx& c, int64_t n) {
                if (n < 0)
                    return Reply::error(String("n must be non-negative"));
                return Reply::later(std::make_shared<FramePending>(c.gui, c.gui.frame + (uint64_t)n));
            }));

        // ---- Capture.

        registry.add(make_command<Ctx>(
            "screenshot", "Save the next presented frame as a PNG", Category::local,
            std::tuple{
                Param<Ctx, String>{"path", "where to write it (default: a temporary file)", String()},
            },
            [](Ctx& c, String const& path) {
                auto request = std::make_shared<ScreenshotRequest>();
                if (path.empty()) {
                    char name[64];
                    std::snprintf(name, sizeof(name), "wry-screenshot-%llu.png",
                                  (unsigned long long)c.gui.next_seq);
                    std::error_code ec;
                    auto dir = std::filesystem::temp_directory_path(ec);
                    if (ec)
                        return Reply::error(String("no temporary directory"));
                    request->path = String((dir / name).string().c_str());
                } else {
                    request->path = path;
                }
                c.gui.screenshot_requests.push_back(request);
                return Reply::later(std::make_shared<ScreenshotPending>(c.gui, std::move(request)));
            }));

        // ---- Input injection (commands.md section 7).  Below the command
        // layer: these exercise the GUI exactly as a user would.  Positions
        // are view points, top-left origin.

        registry.add(make_command<Ctx>(
            "key", "Inject a keystroke: press (down then up), down, or up", Category::local,
            std::tuple{
                Param<Ctx, gui::KeyCombo>{"combo", "the key and modifiers"},
                Param<Ctx, String>{"action", "press, down or up", String("press")},
            },
            [](Ctx& c, gui::KeyCombo const& combo, String const& action) {
                const bool down = action == StringView("press") || action == StringView("down");
                const bool up = action == StringView("press") || action == StringView("up");
                if (!down && !up)
                    return Reply::error(String("action must be press, down or up"));
                String text = text_for_combo(combo);
                if (down)
                    inject_key(c.gui, WryEventKindKeyDown, combo.key, combo.mods, text);
                if (up)
                    inject_key(c.gui, WryEventKindKeyUp, combo.key, combo.mods, StringView());
                return Reply::ok();
            }));

        registry.add(make_command<Ctx>(
            "type", "Inject keystrokes typing text (\\n is Enter, \\t Tab)", Category::local,
            std::tuple{
                Param<Ctx, String>{"text", "what to type"},
            },
            [](Ctx& c, String const& text) {
                for (char32_t ch : text) {
                    uint32_t key = gui::key::Unknown;
                    uint16_t mods = 0;
                    String inserted;
                    if (ch == U'\n') {
                        key = gui::key::Enter;
                    } else if (ch == U'\t') {
                        key = gui::key::Tab;
                    } else if (ch >= 0x20 && ch <= 0x7E) {
                        key = (uint32_t)ch;
                        if (ch >= U'A' && ch <= U'Z') {
                            key = (uint32_t)(ch - U'A' + U'a');
                            mods = gui::Modifiers::Shift;
                        }
                        inserted.push_back(ch);
                    } else {
                        inserted.push_back(ch);
                    }
                    inject_key(c.gui, WryEventKindKeyDown, key, mods, inserted);
                    inject_key(c.gui, WryEventKindKeyUp, key, mods, StringView());
                }
                return Reply::ok();
            }));

        registry.add(make_command<Ctx>(
            "mouse-move", "Inject a pointer move", Category::local,
            std::tuple{
                Param<Ctx, int64_t>{"x", "points from the left"},
                Param<Ctx, int64_t>{"y", "points from the top"},
            },
            [](Ctx& c, int64_t x, int64_t y) {
                inject_pointer(c.gui, WryEventKindMouseMove, float2{(float)x, (float)y});
                return Reply::ok();
            }));

        registry.add(make_command<Ctx>(
            "click", "Inject a click (move, button down, button up)", Category::local,
            std::tuple{
                Param<Ctx, int64_t>{"x", "points from the left"},
                Param<Ctx, int64_t>{"y", "points from the top"},
                Param<Ctx, String>{"button", "left, right or middle", String("left")},
            },
            [](Ctx& c, int64_t x, int64_t y, String const& name) {
                auto button = button_from_name(name);
                if (!button)
                    return Reply::error(button.error());
                inject_click(c.gui, float2{(float)x, (float)y}, *button);
                return Reply::ok();
            }));

        registry.add(make_command<Ctx>(
            "scroll", "Inject a scroll at the last injected pointer position", Category::local,
            std::tuple{
                Param<Ctx, int64_t>{"dx", "horizontal delta"},
                Param<Ctx, int64_t>{"dy", "vertical delta"},
            },
            [](Ctx& c, int64_t dx, int64_t dy) {
                gui::Event e{};
                e.kind = WryEventKindScroll;
                e.location = c.gui.injected_pointer_pt;
                e.scroll_delta = float2{(float)dx, (float)dy};
                c.gui.events.push(e);
                return Reply::ok();
            }));

        // ---- Widget queries.

        registry.add(make_command<Ctx>(
            "widgets", "List the widgets showing, topmost first", Category::local,
            std::tuple<>{},
            [](Ctx& c) {
                String out;
                const char* last_tree = nullptr;
                for_each_widget(c.gui, [&](gui::Widget const& w, rect<float> clip, int depth, const char* tree) {
                    if (tree != last_tree) {
                        if (last_tree)
                            out.push_back('\n');
                        out.append(StringView(tree));
                        out.push_back(':');
                        last_tree = tree;
                    }
                    out.push_back('\n');
                    for (int i = 0; i <= depth; ++i)
                        out.append(StringView("  "));
                    out.append(StringView(w.kind()));
                    if (!w.label().empty()) {
                        out.push_back(' ');
                        command::print_quoted(out, w.label());
                    }
                    rect<float> r = w.bounds();
                    float2 centre = (r.a + r.b) * 0.5f;
                    if (auto pt = pixels_to_points(c.gui, centre); pt && contains(clip, centre)) {
                        out.append(StringView(" at "));
                        append_number(out, std::round(pt->x));
                        out.push_back(',');
                        append_number(out, std::round(pt->y));
                    } else if (!contains(clip, centre)) {
                        out.append(StringView(" (hidden)"));
                    }
                });
                if (out.empty())
                    out = String("(no widgets showing)");
                return Reply::ok(std::move(out));
            }));

        registry.add(make_command<Ctx>(
            "press", "Click the topmost visible button with this label", Category::local,
            std::tuple{
                Param<Ctx, String>{"label", "the button's text"},
            },
            [](Ctx& c, String const& label) {
                gui::Widget const* found = nullptr;
                bool hidden = false;
                for_each_widget(c.gui, [&](gui::Widget const& w, rect<float> clip, int, const char*) {
                    if (found || std::strcmp(w.kind(), "Button") != 0 || w.label() != StringView(label))
                        return;
                    rect<float> r = w.bounds();
                    if (contains(clip, (r.a + r.b) * 0.5f))
                        found = &w;
                    else
                        hidden = true;
                });
                if (!found) {
                    String message(hidden ? "button is scrolled out of view: " : "no button labelled ");
                    message.append(label);
                    return Reply::error(std::move(message));
                }
                rect<float> r = found->bounds();
                auto at = pixels_to_points(c.gui, (r.a + r.b) * 0.5f);
                if (!at)
                    return Reply::error(String("no view to click in"));
                inject_click(c.gui, *at, gui::MouseButton::Left);
                return Reply::ok();
            }));
    }

    // ----------------------------------------------------------------
    // Tests.

    define_test("gui_context_commands") {
        GuiContext gc;

        // Every keymap action names a registered command (commands.md
        // section 5: the keymap's stable ids are command names).
        for (uint32_t i = 0; i != gui::action_count; ++i) {
            StringView id = gui::id_from_action((gui::Action)i);
            assert(gc.commands.find(id));
        }

        // Issued lines are recorded with their origin and frame; lines
        // that do not bind are not.
        gc.frame = 7;
        auto o = gc.issue(Origin::console, "help help");
        assert(o.reply.status == Reply::OK && !o.reply.text.empty());
        assert(gc.history.size() == 1);
        assert(gc.history[0].seq == 1 && gc.history[0].frame == 7);
        assert(gc.history[0].origin == Origin::console);
        assert(gc.history[0].line == StringView("help \"help\""));

        auto bad = gc.issue(Origin::console, "no-such-command");
        assert(bad.reply.status == Reply::ERROR);
        assert(gc.history.size() == 1);

        // The console's toggle is the toggle-console command.
        assert(!gc.console_overlay.active());
        gc.issue(Origin::binding, "toggle-console");
        assert(gc.console_overlay.active());
        assert(gc.history.back().origin == Origin::binding);

        // Typing into the console and pressing Enter issues the line.
        {
            auto key = [](uint32_t k, const char* text) {
                gui::Event e{};
                e.kind = WryEventKindKeyDown;
                e.key = k;
                std::strncpy(e.text, text, sizeof(e.text) - 1);
                return e;
            };
            for (char ch : std::string_view("history 1"))
                gc.console_overlay.on_event(key((uint32_t)ch, std::string(1, ch).c_str()));
            gc.console_overlay.on_event(key(gui::key::Enter, ""));
            assert(gc.history.back().line == StringView("history 1"));
            assert(gc.history.back().origin == Origin::console);
            // Up recalls it.
            gc.console_overlay.on_event(key(gui::key::ArrowUp, ""));
            assert(gc.console_overlay.lines().back() == StringView("history 1"));
        }

        // History is printable as itself.
        auto h = gc.issue(Origin::console, "history 2");
        assert(h.reply.status == Reply::OK);

        co_return;
    };

    // Sequencing, coalescing, injection, widget queries and screenshots
    // (commands.md sections 7 and 8), with the host's per-frame duties
    // played by hand.
    define_test("gui_context_execution") {
        GuiContext gc;

        // A pending command holds its source; the next line waits, then
        // issues when the first completes.
        gc.submit(gc.console_source, "frame 2");
        gc.submit(gc.console_source, "help frame");
        assert(gc.console_source.pending && gc.console_source.queued.size() == 1);
        assert(gc.history.back().line == StringView("frame 2"));
        assert(gc.history.back().status == Reply::PENDING);
        ++gc.frame;
        gc.service_commands();
        assert(gc.console_source.pending);
        ++gc.frame;
        gc.service_commands();
        assert(!gc.console_source.busy());
        assert(gc.history.back().line == StringView("help \"frame\""));
        assert(gc.history[gc.history.size() - 2].status == Reply::OK);

        // Other origins are not held up by a busy console.
        gc.submit(gc.console_source, "frame 100");
        gc.submit(gc.console_source, "help");
        auto gesture = gc.issue(Origin::gui, "help help");
        assert(gesture.reply.status == Reply::OK);

        // Cancel completes the pending one and drops the queue.
        gc.cancel(gc.console_source);
        assert(!gc.console_source.busy());
        bool cancelled = false;
        for (auto const& e : gc.history)
            if (e.line == StringView("frame 100"))
                cancelled = (e.status == Reply::CANCELLED);
        assert(cancelled);
        assert(gc.history.back().line != StringView("help \"\""));

        // A detached pending reply (a gesture's) is polled too.
        gc.issue(Origin::gui, "frame 1");
        assert(gc.detached.size() == 1);
        ++gc.frame;
        gc.service_commands();
        assert(gc.detached.empty());

        // Coalescing: within a run of continuous events one of each kind
        // survives (moves keep the last position, scrolls sum); a discrete
        // event ends the run.
        {
            gui::EventQueue q;
            auto ev = [](WryEventKind k, float x, float dy) {
                gui::Event e{};
                e.kind = k;
                e.location = float2{x, 0.0f};
                e.scroll_delta = float2{0.0f, dy};
                return e;
            };
            q.push(ev(WryEventKindMouseMove, 1, 0));
            q.push(ev(WryEventKindScroll, 2, 1));
            q.push(ev(WryEventKindMouseMove, 3, 0));
            q.push(ev(WryEventKindScroll, 4, 2));
            q.push(ev(WryEventKindMouseDown, 5, 0));
            q.push(ev(WryEventKindMouseMove, 6, 0));
            q.push(ev(WryEventKindMouseMove, 7, 0));
            assert(q.size() == 4);
            gui::Event a = q.pop_front();
            assert(a.kind == WryEventKindMouseMove && a.location.x == 3);
            gui::Event b = q.pop_front();
            assert(b.kind == WryEventKindScroll && b.scroll_delta.y == 3 && b.location.x == 4);
            assert(q.pop_front().kind == WryEventKindMouseDown);
            assert(q.pop_front().location.x == 7);
        }

        // Injection: keys, typing, clicks land on the event queue in
        // points.
        gc.events.clear();
        {
            auto o = gc.issue(Origin::console, "key Shift+R");
            assert(o.reply.status == Reply::OK);
            assert(o.line == StringView("key KeyCombo(\"Shift+R\") \"press\""));
            assert(gc.events.size() == 2);
            gui::Event down = gc.events.pop_front();
            assert(down.kind == WryEventKindKeyDown && down.key == 'r');
            assert(down.mods.has(gui::Modifiers::Shift));
            assert(std::strcmp(down.text, "R") == 0);
            assert(gc.events.pop_front().kind == WryEventKindKeyUp);

            gc.issue(Origin::console, "type \"aB\\n\"");
            assert(gc.events.size() == 6);
            gui::Event a = gc.events.pop_front();
            assert(a.key == 'a' && std::strcmp(a.text, "a") == 0);
            (void) gc.events.pop_front();
            gui::Event b = gc.events.pop_front();
            assert(b.key == 'b' && b.mods.has(gui::Modifiers::Shift) && std::strcmp(b.text, "B") == 0);
            (void) gc.events.pop_front();
            assert(gc.events.pop_front().key == gui::key::Enter);
            gc.events.clear();

            gc.issue(Origin::console, "click 10 20 right");
            assert(gc.events.size() == 3);
            gui::Event m = gc.events.pop_front();
            assert(m.kind == WryEventKindMouseMove && m.location.x == 10 && m.location.y == 20);
            gui::Event d = gc.events.pop_front();
            assert(d.kind == WryEventKindMouseDown && d.button == gui::MouseButton::Right);
            assert(gc.events.pop_front().kind == WryEventKindMouseUp);

            assert(gc.issue(Origin::console, "click 1 2 sideways").reply.status == Reply::ERROR);
            assert(gc.issue(Origin::console, "key Nonsense+Q").reply.status == Reply::ERROR);
        }

        // Widgets: a scene tree laid out in pixels at twice the point
        // scale; `press` clicks the button's centre, in points.
        {
            bool clicked = false;
            gui::Column column;
            column.add(std::make_unique<gui::Button>(StringView("OK"), [&clicked] { clicked = true; }));
            gui::MeasureContext mctx{};
            gui::SizeConstraints loose{};
            loose.max = gui::Size{400.0f, 200.0f};
            (void) column.measure(loose, mctx);
            column.arrange(rect<float>{0.0f, 0.0f, 400.0f, 100.0f});
            gc.scene_widgets = &column;
            gc.viewport_size = float2{800.0f, 400.0f};
            gc.view_size_pt = float2{400.0f, 200.0f};

            auto listing = gc.issue(Origin::console, "widgets");
            assert(listing.reply.status == Reply::OK);
            {
                StringView text = listing.reply.text;
                bool mentions = false;
                for (auto p = text.chars.begin(); p + 11 <= text.chars.end(); ++p)
                    if (std::memcmp(p, "Button \"OK\"", 11) == 0)
                        mentions = true;
                assert(mentions);
            }

            gc.events.clear();
            assert(gc.issue(Origin::console, "press OK").reply.status == Reply::OK);
            assert(gc.events.size() == 3);
            // Deliver as the pump would: points promoted to pixels.
            while (!gc.events.empty()) {
                gui::Event e = gc.events.pop_front();
                e.location *= 2.0f;
                column.on_event(e);
            }
            assert(clicked);
            assert(gc.issue(Origin::console, "press Cancel").reply.status == Reply::ERROR);
            gc.scene_widgets = nullptr;
        }

        // Screenshot: the host fills the request; the pending reply writes
        // the PNG and replies with its path.
        {
            std::error_code ec;
            auto dir = std::filesystem::temp_directory_path(ec);
            assert(!ec);
            std::string path = (dir / "wry-gui-context-test.png").string();
            String line("screenshot ");
            command::print_quoted(line, StringView(path.c_str()));
            gc.submit(gc.console_source, line);
            assert(gc.console_source.pending);
            assert(gc.screenshot_requests.size() == 1);
            auto request = gc.screenshot_requests.front();
            gc.screenshot_requests.clear();
            request->width = 2;
            request->height = 1;
            const _Float16 halves[8] = {1, 0, 0, 1, 0, 0.5f, 1, 1};
            request->half_rgba.resize(8);
            std::memcpy(request->half_rgba.data(), halves, sizeof(halves));
            request->ready.store_release(true);
            gc.service_commands();
            assert(!gc.console_source.busy());
            assert(gc.history.back().status == Reply::OK);
            assert(gc.history.back().reply == StringView(path.c_str()));
            matrix<RGBA8Unorm_sRGB> image = from_png(StringView(path.c_str()));
            assert(image.major() == 2 && image.minor() == 1);
            RGBA8Unorm_sRGB const left = image[0, 0];
            RGBA8Unorm_sRGB const right = image[0, 1];
            assert(left.r._ == 255 && left.g._ == 0);
            assert(right.b._ == 255);
            std::filesystem::remove(path, ec);
        }

        co_return;
    };

} // namespace wry
