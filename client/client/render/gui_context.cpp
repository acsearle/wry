//
//  gui_context.cpp
//  client
//
//  Created by Antony Searle on 2026-10-12.
//

#include "gui_context.hpp"
#include "test.hpp"

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

    Outcome GuiContext::issue(Origin origin, StringView text) {
        CommandContext context{*this, world};
        Outcome outcome = commands.run(context, text);

        // History records issued invocations: those that bound.  A line
        // that failed to parse or bind issued nothing.
        if (!outcome.line.empty())
            history.push_back(command::HistoryEntry{
                next_seq++, frame, origin, outcome.line, outcome.reply});

        // Echo.  Console input is echoed as typed; everything else as the
        // canonical line it issued, tagged with its origin -- so the
        // console shows what the mouse and keys did in its own language.
        if (origin == Origin::console) {
            String echo("> ");
            echo.append(text);
            console_overlay.print(echo);
        } else if (!outcome.line.empty()) {
            String echo("[");
            echo.append(StringView(command::name_from_origin(origin)));
            echo.append(StringView("] "));
            echo.append(outcome.line);
            console_overlay.print(echo);
        }
        const bool failed = outcome.reply.status == Reply::ERROR;
        for_each_line(outcome.reply.text, [&](StringView line) {
            if (failed) {
                String message("! ");
                message.append(line);
                console_overlay.print(message);
            } else {
                console_overlay.print(line);
            }
        });
        // A failed gesture or binding would otherwise fail silently while
        // the console is closed.
        if (failed && origin != Origin::console)
            append_log(outcome.reply.text);
        return outcome;
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

} // namespace wry
