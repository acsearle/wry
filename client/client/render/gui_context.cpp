//
//  gui_context.cpp
//  client
//
//  Created by Antony Searle on 2026-10-12.
//

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "gui_context.hpp"
#include "gui_widget.hpp"
#include "image.hpp"
#include "json.hpp"
#include "execution.hpp"
#include "kqueue_reactor.hpp"
#include "test.hpp"
#include "world_state.hpp"

namespace wry::command {

    // Key combos, as the keymap spells them: KeyCombo("Shift+R"); a bare
    // word or a string where a KeyCombo is expected (`key Shift+R`; a
    // combo containing a delimiter must be quoted, `key "="`).
    template<> struct Type<gui::KeyCombo> {
        static constexpr const char* name = "KeyCombo";
        static constexpr const char* example = "Shift+R";

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

    Outcome GuiContext::_issue_now(Origin origin, StringView text, StringView peer) {
        CommandContext context{*this, world};
        Outcome outcome = commands.run(context, text);

        // History records issued invocations: those that bound.  A line
        // that failed to parse or bind issued nothing.  Text only.
        if (!outcome.line.empty()) {
            command::HistoryEntry entry;
            entry.seq = next_seq++;
            entry.frame = frame;
            entry.origin = origin;
            entry.peer = String(peer);
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
            if (!peer.empty()) {
                echo.append(StringView(" "));
                echo.append(peer);
            }
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

    void GuiContext::submit(CommandSource& source, StringView text, StringView tag) {
        if (source.origin == Origin::console)
            _echo_input(text);
        source.queued.push_back(CommandSource::Queued{String(text), String(tag)});
        _drain(source);
    }

    void GuiContext::_drain(CommandSource& source) {
        while (!source.pending && !source.queued.empty()) {
            CommandSource::Queued next = std::move(source.queued.front());
            source.queued.pop_front();
            Outcome outcome = _issue_now(source.origin, next.line, source.peer);
            if (outcome.reply.status == Reply::PENDING) {
                source.pending = outcome.reply.pending;
                source.pending_seq = history.back().seq;
                source.pending_tag = std::move(next.tag);
            } else if (source.on_reply) {
                source.on_reply(next.tag, outcome.line, outcome.reply);
            }
        }
    }

    namespace {

        // The canonical line of a history entry, for a completed reply.
        StringView line_of(GuiContext const& gui, uint64_t seq) {
            for (auto it = gui.history.rbegin(); it != gui.history.rend(); ++it)
                if (it->seq == seq)
                    return it->line;
            return StringView();
        }

    } // namespace

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
        _service_control_socket();
        auto service = [this](CommandSource& source) {
            if (source.pending) {
                if (auto reply = source.pending->poll()) {
                    const uint64_t seq = source.pending_seq;
                    source.pending.reset();
                    _complete(seq, source.origin, *reply);
                    if (source.on_reply)
                        source.on_reply(source.pending_tag, line_of(*this, seq), *reply);
                }
            }
            _drain(source);
        };
        service(console_source);
        for (auto& [id, source] : socket_sources)
            service(*source);
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

    bool GuiContext::idle() const {
        if (console_source.busy() || !detached.empty() || !events.empty()
            || !screenshot_requests.empty())
            return false;
        // Finalized steps waiting to be applied are a frame's work too
        if (world && world->has_server_input())
            return false;
        for (auto const& [id, source] : socket_sources)
            if (source->busy())
                return false;
        // Lines on the socket not yet taken (wait_for_input takes nothing;
        // the frame's service_commands does), or taken and not serviced
        if (control_socket && control_socket->events().size() != 0)
            return false;
        return socket_events.empty();
    }

    void GuiContext::cancel(CommandSource& source) {
        // Every request is answered, even the ones that never issued.
        if (source.pending) {
            source.pending->cancel();
            const uint64_t seq = source.pending_seq;
            source.pending.reset();
            _complete(seq, source.origin, Reply::cancelled());
            if (source.on_reply)
                source.on_reply(source.pending_tag, line_of(*this, seq), Reply::cancelled());
        }
        if (source.on_reply)
            for (auto const& q : source.queued)
                source.on_reply(q.tag, StringView(), Reply::cancelled());
        source.queued.clear();
    }

    // ----------------------------------------------------------------
    // The control socket (commands.md section 9).  Requests are lines:
    //
    //     {"id": 1, "line": "step 10"}
    //     {"id": 2, "command": "write", "args": {"at": "Coordinate(3, 4)", "term": "OPCODE::SWAP"}}
    //     step 10                                  (a bare command line)
    //
    // and every request gets exactly one reply line, in order per
    // connection:
    //
    //     {"id": 1, "status": "ok", "line": "step 10", "text": ""}
    //
    // with "image": path added when the result is an image file.  Arguments
    // in JSON: null, booleans, integers and (for String parameters)
    // strings map directly; any other type travels as a string holding its
    // literal text.

    namespace {

        void json_quote(String& out, StringView text) {
            static constexpr char hex[] = "0123456789abcdef";
            out.push_back('"');
            for (char c : text.chars) {
                const unsigned char ch = (unsigned char)c;
                switch (ch) {
                    case '"':  out.append(StringView("\\\"")); break;
                    case '\\': out.append(StringView("\\\\")); break;
                    case '\n': out.append(StringView("\\n")); break;
                    case '\r': out.append(StringView("\\r")); break;
                    case '\t': out.append(StringView("\\t")); break;
                    default:
                        if (ch < 0x20) {
                            out.append(StringView("\\u00"));
                            out.push_back(hex[ch >> 4]);
                            out.push_back(hex[ch & 15]);
                        } else {
                            // Raw byte of valid UTF-8 (text is a StringView).
                            out.chars.push_back(c);
                        }
                }
            }
            out.push_back('"');
        }

        const char* name_from_status(Reply::Status status) {
            switch (status) {
                case Reply::OK:        return "ok";
                case Reply::ERROR:     return "error";
                case Reply::PENDING:   return "pending";
                case Reply::CANCELLED: return "cancelled";
            }
            return "?";
        }

        String reply_json(StringView tag, StringView line, Reply const& reply) {
            String out("{\"id\": ");
            if (tag.empty())
                out.append(StringView("null"));
            else
                out.append(tag);
            out.append(StringView(", \"status\": "));
            json_quote(out, StringView(name_from_status(reply.status)));
            out.append(StringView(", \"line\": "));
            json_quote(out, line);
            out.append(StringView(", \"text\": "));
            json_quote(out, reply.text);
            if (!reply.image.empty()) {
                out.append(StringView(", \"image\": "));
                json_quote(out, reply.image);
            }
            out.push_back('}');
            return out;
        }

        // A decoded request: the line to run and the tag to reply with; or
        // an error to reply with at once.
        struct Request {
            String tag;
            String line;
            String error;
        };

        // A JSON number or string id, re-serialized as its reply tag.
        std::optional<String> tag_from_id(json::Json const& id) {
            String tag;
            if (id.is_string()) {
                json_quote(tag, id.as_string());
            } else if (id.is_number()) {
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%.17g", id.as_number());
                tag = String(buffer);
            } else {
                return std::nullopt;
            }
            return tag;
        }

        // One argument of a structured invocation, as literal text.
        std::expected<String, String> argument_text(json::Json const& value,
                                                    command::ParamInfo const* param) {
            String out;
            if (value.is_null()) {
                out = String("null");
            } else if (value.is_bool()) {
                out = String(value.as_bool() ? "true" : "false");
            } else if (value.is_number()) {
                const double x = value.as_number();
                if (!(x == std::floor(x) && std::fabs(x) <= 9007199254740992.0))
                    return std::unexpected(String("numeric arguments must be integers of magnitude at most 2^53 (pass larger ones as strings)"));
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%lld", (long long)x);
                out = String(buffer);
            } else if (value.is_string()) {
                StringView text = value.as_string();
                if (param && param->type_name == StringView("String")) {
                    command::print_quoted(out, text);
                } else {
                    // Literal text: exactly one node, so it cannot smuggle
                    // in further arguments.
                    auto node = command::parse_node(text);
                    if (!node) {
                        String message("bad literal ");
                        message.append(text);
                        message.append(StringView(": "));
                        message.append(node.error().message);
                        return std::unexpected(std::move(message));
                    }
                    command::print_node(out, *node);
                }
            } else {
                return std::unexpected(String("arguments must be null, booleans, integers or strings"));
            }
            return out;
        }

        Request decode_request(GuiContext const& gui, std::string const& bytes) {
            Request request;
            const char* first = bytes.data();
            const char* last = first + bytes.size();
            if (!utf8::isvalid(first, last)) {
                request.error = String("request is not valid UTF-8");
                return request;
            }
            StringView text(trusted, first, last);
            const char* p = first;
            while (p != last && (*p == ' ' || *p == '\t'))
                ++p;
            if (p == last || *p != '{') {
                request.line = String(text);
                return request;
            }
            json::Json json;
            try {
                json = json::Json::from(StringView(trusted, first, last));
            } catch (json::JsonParseError const&) {
                request.error = String("malformed JSON request");
                return request;
            }
            if (!json.is_object()) {
                request.error = String("a JSON request must be an object");
                return request;
            }
            json::Json const* line = nullptr;
            json::Json const* name = nullptr;
            json::Json const* args = nullptr;
            for (auto const& [key, value] : json.as_object()) {
                if (key == StringView("id")) {
                    if (auto tag = tag_from_id(value))
                        request.tag = std::move(*tag);
                } else if (key == StringView("line")) {
                    line = &value;
                } else if (key == StringView("command")) {
                    name = &value;
                } else if (key == StringView("args")) {
                    args = &value;
                }
            }
            if (line) {
                if (!line->is_string()) {
                    request.error = String("\"line\" must be a string");
                    return request;
                }
                request.line = String(line->as_string());
                return request;
            }
            if (!name || !name->is_string()) {
                request.error = String("a request needs a \"line\" or a \"command\"");
                return request;
            }
            StringView command_name = name->as_string();
            if (!command::is_valid_name(command_name)) {
                request.error = String("bad command name");
                return request;
            }
            auto const* spec = gui.commands.find(command_name);
            request.line = String(command_name);
            if (args) {
                if (!args->is_object()) {
                    request.error = String("\"args\" must be an object");
                    return request;
                }
                for (auto const& [key, value] : args->as_object()) {
                    if (!command::is_valid_name(key)) {
                        request.error = String("bad parameter name");
                        return request;
                    }
                    command::ParamInfo const* param = nullptr;
                    if (spec)
                        for (auto const& p : spec->params)
                            if (p.name == key)
                                param = &p;
                    auto literal = argument_text(value, param);
                    if (!literal) {
                        request.error = std::move(literal.error());
                        return request;
                    }
                    request.line.push_back(' ');
                    request.line.append(key);
                    request.line.push_back('=');
                    request.line.append(*literal);
                }
            }
            return request;
        }

    } // namespace

    bool GuiContext::open_control_socket(String const& path) {
        std::string error;
        control_socket = ControlSocket::open(std::string(path.data(), path.chars.size()), error);
        String message;
        if (control_socket) {
            // The process singleton: its accept loop lives in the process
            // scope, under the root token
            assert(scope);
            scope->soon(control_socket->serve());
            message = String("Control socket: ");
            message.append(path);
        } else {
            message = String("Control socket failed: ");
            message.append(StringView(error.c_str()));
        }
        console_overlay.print(message);
        append_log(message);
        return control_socket != nullptr;
    }

    namespace {

        using Inbox = coroutine::MultiProducerSingleConsumerQueue<Transport::Event>;

        // Woken by either inbox, taking nothing from it: the entrants are
        // the queues' non-consuming waits, so a tie's loser drops no
        // items with its (empty) value; the frame that follows takes
        // from both
        coroutine::Future<> wait_either(Inbox& a, Inbox& b) {
            auto wait_a = a.wait();
            auto wait_b = b.wait();
            (void) co_await execution::when_any(coroutine::lift_awaitable<decltype(wait_a)&>(&wait_a),
                                                coroutine::lift_awaitable<decltype(wait_b)&>(&wait_b));
        }

    } // namespace

    void GuiContext::wait_for_input(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        Inbox* commands = control_socket ? &control_socket->events() : nullptr;
        Inbox* server = world ? world->server_inbox() : nullptr;
        if (commands && server) {
            (void) coroutine::sync_wait(with_deadline(deadline, wait_either(*commands, *server)));
        } else if (commands || server) {
            auto wait = (commands ? commands : server)->wait();
            (void) coroutine::sync_wait(with_deadline(deadline, wait));
        } else {
            std::this_thread::sleep_for(timeout);
        }
    }

    void GuiContext::configure_from_arguments(int argc, const char* const* argv) {
        auto find = [&](std::string_view flag) -> int {
            for (int i = 1; i < argc; ++i)
                if (std::string_view(argv[i]) == flag)
                    return i;
            return -1;
        };
        auto value_after = [&](int i) -> const char* {
            return (i >= 0 && i + 1 < argc && argv[i + 1][0] != '-') ? argv[i + 1] : nullptr;
        };
        // The control socket (core/docs/commands.md section 9) exists only
        // when asked for: --control-socket [PATH], PATH defaulting to
        // $TMPDIR/wry-control.sock (what tools/wryctl and tools/wry_mcp.py
        // look for).  Launch the bundle with
        //     open client.app --args --control-socket
        if (int i = find("--control-socket"); i >= 0) {
            std::string path;
            if (const char* given = value_after(i)) {
                path = given;
            } else {
                std::error_code ec;
                path = (std::filesystem::temp_directory_path(ec) / "wry-control.sock").string();
            }
            open_control_socket(String(path.c_str()));
        }
        // Multiplayer (game/docs/multiplayer.md): --host [PORT] listens for
        // players once a game starts (port 7777 by default); --join
        // ADDRESS:PORT joins another process's game instead of hosting
        if (int i = find("--host"); i >= 0) {
            uint16_t port = 7777;
            if (const char* given = value_after(i))
                port = (uint16_t)std::atoi(given);
            host_port = port;
        }
        if (int i = find("--join"); i >= 0) {
            if (const char* given = (i + 1 < argc) ? argv[i + 1] : nullptr) {
                std::string where = given;
                size_t colon = where.rfind(':');
                if (colon != std::string::npos) {
                    join_address = String(where.substr(0, colon).c_str());
                    join_port = (uint16_t)std::atoi(where.c_str() + colon + 1);
                }
            }
        }
        // Load (or first-run install) settings.json.  The cwd is already
        // the assets directory (main.mm chdir), so the config/ paths in
        // settings_paths resolve next to saves/.  Any warnings (unknown
        // ids, malformed combos, unparseable file) surface in the
        // floating log once a scene is up.
        std::vector<String> warnings;
        settings = load_settings(settings_paths, &warnings);
        for (String const& w : warnings) {
            fprintf(stderr, "%.*s\n", (int)w.chars.size(), w.chars.data());
            append_log(w, std::chrono::seconds(15));
        }
    }

    void GuiContext::_service_control_socket() {
        if (!control_socket)
            return;
        for (auto& event : control_socket->events().try_take())
            socket_events.push_back(std::move(event));
        // A connection's command source, made at its Opened (or, should a
        // line arrive first, then, unlabelled)
        auto source_for = [this](uint64_t id) -> CommandSource& {
            auto& source = socket_sources[id];
            if (!source) {
                source = std::make_unique<CommandSource>(Origin::socket);
                source->on_reply = [this, id](String const& tag, StringView canonical, Reply const& reply) {
                    if (!control_socket)
                        return;
                    String json = reply_json(tag, canonical, reply);
                    control_socket->send(id, std::string_view(json.data(), json.chars.size()));
                };
            }
            return *source;
        };
        // In arrival order: a connection's Opened, its lines, then its Closed
        while (!socket_events.empty()) {
            ControlSocket::Event event = std::move(socket_events.front());
            socket_events.pop_front();
            if (auto* opened = std::get_if<ControlSocket::Opened>(&event)) {
                source_for(opened->connection).peer = String(StringView(opened->peer.c_str()));
                continue;
            }
            if (auto* closed = std::get_if<ControlSocket::Closed>(&event)) {
                auto it = socket_sources.find(closed->connection);
                if (it == socket_sources.end())
                    continue;
                cancel(*it->second);
                socket_sources.erase(it);
                continue;
            }
            ControlSocket::Line* line_event = std::get_if<ControlSocket::Line>(&event);
            if (!line_event)
                continue;               // nothing else is posted to this inbox
            ControlSocket::Line& line = *line_event;
            const uint64_t id = line.connection;
            Request request = decode_request(*this, line.bytes);
            if (!request.error.empty()) {
                String json = reply_json(request.tag, StringView(), Reply::error(request.error));
                control_socket->send(id, std::string_view(json.data(), json.chars.size()));
                continue;
            }
            // The pipelining cap (commands.md section 9): a connection may
            // have at most this many requests queued or pending; the rest
            // are refused at once, with a reply, so a client that never
            // reads its replies cannot grow the queue without bound
            CommandSource& source = source_for(id);
            if (source.queued.size() + (source.pending ? 1 : 0) >= max_pipelined_requests) {
                String json = reply_json(request.tag, StringView(),
                                         Reply::error(String("too many requests in flight: at most 64 per connection")));
                control_socket->send(id, std::string_view(json.data(), json.chars.size()));
                continue;
            }
            submit(source, request.line, request.tag);
        }
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
                return Reply::image_file(request->path);
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

        // The registry as JSON, for tools (the MCP adapter builds one tool
        // per command from it): [{name, summary, category, params: [{name,
        // type, example?, summary, default?}]}].
        registry.add(make_command<Ctx>(
            "describe", "The command registry as JSON", Category::local,
            std::tuple<>{},
            [](Ctx& c) {
                String out("[");
                bool first_command = true;
                for (auto const& spec : c.gui.commands._commands) {
                    if (!first_command)
                        out.push_back(',');
                    first_command = false;
                    out.append(StringView("{\"name\": "));
                    json_quote(out, spec.name);
                    out.append(StringView(", \"summary\": "));
                    json_quote(out, spec.summary);
                    out.append(StringView(", \"category\": "));
                    json_quote(out, StringView(command::name_from_category(spec.category)));
                    out.append(StringView(", \"params\": ["));
                    bool first_param = true;
                    for (auto const& p : spec.params) {
                        if (!first_param)
                            out.push_back(',');
                        first_param = false;
                        out.append(StringView("{\"name\": "));
                        json_quote(out, p.name);
                        out.append(StringView(", \"type\": "));
                        json_quote(out, p.type_name);
                        if (!p.type_example.empty()) {
                            out.append(StringView(", \"example\": "));
                            json_quote(out, p.type_example);
                        }
                        out.append(StringView(", \"summary\": "));
                        json_quote(out, p.summary);
                        if (p.has_default) {
                            out.append(StringView(", \"default\": "));
                            json_quote(out, p.default_text);
                        }
                        out.push_back('}');
                    }
                    out.append(StringView("]}"));
                }
                out.push_back(']');
                return Reply::ok(std::move(out));
            }));

        registry.add(make_command<Ctx>(
            "quit", "Quit the game, as QUIT TO DESKTOP does", Category::session,
            std::tuple<>{},
            [](Ctx& c) {
                if (!c.gui.on_quit)
                    return Reply::error(String("this host cannot quit"));
                c.gui.on_quit();
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

    // The control socket end to end: JSON and bare-line requests, one
    // reply each, in order per connection; errors carry the request's id;
    // a pending reply arrives when it completes; closing cancels.
    define_test("gui_context_control_socket") {
        GuiContext gc;
        std::string path = "/tmp/wry-gui-context-socket-test-" + std::to_string(::getpid()) + ".sock";
        // Opened by hand rather than through open_control_socket, whose
        // spawn goes to the wait group: there the accept loop would be
        // cancelled by the root token at quit (the test runner itself is
        // shielded from it), and this test would lose its replies.  Under
        // this frame's nursery it inherits the runner's shield
        std::string error;
        gc.control_socket = ControlSocket::open(path, error);
        assert(gc.control_socket && error.empty());
        coroutine::Nursery serving;
        serving.soon(gc.control_socket->serve());

        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un address = {};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.data(), path.size());
        const int connected = ::connect(fd, (sockaddr const*)&address, sizeof(address));
        assert(connected == 0);
        (void) connected;

        const std::string requests =
            "{\"id\": 1, \"line\": \"help help\"}\n"
            "{\"id\": \"two\", \"command\": \"click\", \"args\": {\"x\": 3, \"y\": 4, \"button\": \"right\"}}\n"
            "frame 2\n"
            "{\"id\": 4, \"command\": \"help\", \"args\": {\"command\": \"frame\"}}\n"
            "{\"id\": 5, \"command\": \"click\", \"args\": {\"x\": 1.5, \"y\": 0}}\n"
            "{\"id\": 6, \"nonsense\": true}\n"
            "{not json\n"
            "{\"id\": 8, \"command\": \"key\", \"args\": {\"combo\": \"Shift+R x=1\"}}\n";
        const ssize_t written = ::write(fd, requests.data(), requests.size());
        assert(written == (ssize_t)requests.size());
        (void) written;

        // Play the host: frames tick and commands are serviced, while the
        // client collects reply lines.
        std::vector<std::string> replies;
        std::string pending;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (replies.size() < 8 && std::chrono::steady_clock::now() < deadline) {
            ++gc.frame;
            gc.service_commands();
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, 10) > 0) {
                char buffer[65536];
                ssize_t n = ::read(fd, buffer, sizeof(buffer));
                if (n <= 0)
                    break;
                pending.append(buffer, (size_t)n);
                for (size_t nl; (nl = pending.find('\n')) != std::string::npos; ) {
                    replies.push_back(pending.substr(0, nl));
                    pending.erase(0, nl + 1);
                }
            }
        }
        assert(replies.size() == 8);

        // Replies in order, as JSON.  (Errors decoded before issue -- 6, 7
        // -- are answered at once, so may overtake the queue; check by id.)
        auto field = [](std::string const& reply, const char* key) -> std::string {
            json::Json j = json::Json::from(StringView(trusted, reply.data(), reply.data() + reply.size()));
            for (auto const& [k, v] : j.as_object())
                if (k == StringView(key)) {
                    if (v.is_string())
                        return std::string(v.as_string().chars.begin(), v.as_string().chars.end());
                    if (v.is_number())
                        return std::to_string((long long)v.as_number());
                    if (v.is_null())
                        return "null";
                }
            return "";
        };
        std::map<std::string, std::string> status_by_id;
        std::map<std::string, std::string> line_by_id;
        for (auto const& r : replies) {
            status_by_id[field(r, "id")] = field(r, "status");
            line_by_id[field(r, "id")] = field(r, "line");
        }
        assert(status_by_id["1"] == "ok" && line_by_id["1"] == "help \"help\"");
        assert(status_by_id["two"] == "ok");
        assert(line_by_id["two"] == "click 3 4 \"right\"");
        // Two replies have id null: the bare line, and the request too
        // malformed to have an id.
        size_t null_ok = 0, null_error = 0;
        for (auto const& r : replies)
            if (field(r, "id") == "null") {
                if (field(r, "status") == "ok" && field(r, "line") == "frame 2") ++null_ok;
                if (field(r, "status") == "error") ++null_error;
            }
        assert(null_ok == 1 && null_error == 1);
        assert(status_by_id["4"] == "ok");
        assert(status_by_id["5"] == "error");     // not an integer
        assert(status_by_id["6"] == "error");     // neither line nor command
        assert(status_by_id["8"] == "error");     // literal smuggling a second argument
        // The bare frame 2 was pending: 4 (queued behind it) came after.
        size_t frame_at = 0, four_at = 0;
        for (size_t i = 0; i != replies.size(); ++i) {
            if (field(replies[i], "line") == "frame 2") frame_at = i;
            if (field(replies[i], "id") == "4") four_at = i;
        }
        assert(frame_at < four_at);

        // Socket invocations are recorded with their origin and the peer's
        // label: this process, connected to itself
        bool from_socket = false;
        std::string expected_peer = "client " + std::to_string(::getpid());
        for (auto const& e : gc.history)
            if (e.line == StringView("click 3 4 \"right\""))
                from_socket = (e.origin == Origin::socket)
                           && (e.peer == StringView(expected_peer.c_str()));
        assert(from_socket);

        // Closing the connection with a command pending cancels it.
        const std::string slow = "frame 1000\n";
        const ssize_t w2 = ::write(fd, slow.data(), slow.size());
        assert(w2 == (ssize_t)slow.size());
        (void) w2;
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (gc.socket_sources.empty() || !gc.socket_sources.begin()->second->pending) {
            gc.service_commands();
            assert(std::chrono::steady_clock::now() < deadline);
        }
        ::close(fd);
        while (!gc.socket_sources.empty()) {
            gc.service_commands();
            assert(std::chrono::steady_clock::now() < deadline);
        }
        assert(gc.history.back().line == StringView("frame 1000"));
        assert(gc.history.back().status == Reply::CANCELLED);

        // The loop was spawned under the wait group, which this test cannot
        // join, so poll for its end before the destructor's assert
        co_await gc.control_socket->shutdown(std::chrono::milliseconds(100));
        (void) co_await serving.join();
        gc.control_socket.reset();
        co_return;
    };

} // namespace wry
