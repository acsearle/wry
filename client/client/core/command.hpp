//
//  command.hpp
//  client
//
//  Created by Antony Searle on 2026-10-12.
//
//  The command registry and its literal syntax.  Design and rationale:
//  core/docs/commands.md.  This header is the platform- and game-neutral
//  machinery:
//
//    - stage 1: a context-free tokenizer and parser from a command line
//      to a syntax tree (LineSyntax, Node);
//    - stage 2: per-type readers and canonical printers (Type<T>), which
//      read a Node as the type its parameter declares;
//    - the registry: named commands with typed parameters, bound from a
//      LineSyntax, run against an application-supplied Context.
//
//  The game's types (Coordinate, EntityID, the Term enums, Term itself)
//  are provided by game/command_types.hpp.
//

#ifndef command_hpp
#define command_hpp

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "assert.hpp"
#include "string.hpp"

namespace wry::command {

    // ================================================================
    // Errors
    // ================================================================

    // A failure to parse or read, with the byte offset into the line it
    // concerns (for a caret under the console input).
    struct Error {
        String message;
        size_t offset = 0;
    };

    Error make_error(size_t offset, StringView message);

    // ================================================================
    // Stage 1: syntax
    // ================================================================
    //
    // Tokens: words (maximal runs of anything but whitespace and the
    // delimiters ( ) , = " #), strings ("..." with escapes), and the
    // punctuation ( ) , =.  # outside a string comments out the rest of
    // the line.  Whitespace separates tokens and is otherwise
    // insignificant.
    //
    //   line  := word arg*
    //   arg   := word '=' node | node
    //   node  := word | string | word '(' [node (',' node)*] ')'

    struct Node {
        enum Kind : uint8_t {
            WORD,
            STRING,
            TAGGED,
        };
        Kind kind = WORD;
        size_t offset = 0;              // byte offset of the node in its line
        String text;                    // WORD: the word; STRING: decoded
                                        // contents; TAGGED: the tag
        std::vector<Node> children;     // TAGGED only
    };

    struct ArgumentSyntax {
        String name;                    // empty for a positional argument
        size_t name_offset = 0;
        Node node;
    };

    struct LineSyntax {
        String command;                 // empty for a blank or comment-only line
        size_t command_offset = 0;
        std::vector<ArgumentSyntax> arguments;
    };

    // Structural equality, ignoring offsets.
    bool equivalent(Node const& a, Node const& b);
    bool equivalent(LineSyntax const& a, LineSyntax const& b);

    std::expected<LineSyntax, Error> parse_line(StringView line);

    // A single node with nothing after it: the literal-text form used by
    // JSON arguments and by tests.
    std::expected<Node, Error> parse_node(StringView text);

    // Canonical text of syntax: parse_line(print_line(s)) is equivalent
    // to s.
    void print_node(String& out, Node const& node);
    String print_line(LineSyntax const& line);

    // A string literal, quoted and escaped: \" \\ \n \t \r, and \u{X} for
    // the remaining C0 controls and DEL.  Everything else is raw UTF-8.
    void print_quoted(String& out, StringView text);

    // Whether `text` would tokenize as exactly one word (non-empty, no
    // whitespace, no delimiters).  Printers that emit words assert it.
    bool is_word(StringView text);

    // Names of commands and parameters: [a-z0-9-]+.  No underscores, so
    // kebab-case <-> snake_case is a bijection (commands.md 4.1).
    bool is_valid_name(StringView text);

    // ================================================================
    // Stage 2: types
    // ================================================================
    //
    // Type<T> is specialized for each parameter type, providing
    //
    //     static constexpr const char* name;      // "Coordinate"
    //     static std::expected<T, Error> read(Node const&);
    //     static void print(String&, T const&);    // canonical form
    //
    // and optionally
    //
    //     using stored_type = ...;                 // how a bound argument
    //     static T const& unstore(stored_type const&);  //   is held
    //     static bool printable(T const&);         // default: always
    //
    // `read` is contextual: it is called only where a T is expected, so it
    // may accept abbreviations (a bare enumerator, a bare word as a
    // String).  `print` always emits the canonical form, which reads
    // correctly in every context, and read(print(x)) == x.

    template<typename T>
    struct Type;

    template<typename T>
    concept CommandType = requires(Node const& node, String& out, T const& x) {
        { Type<T>::name } -> std::convertible_to<const char*>;
        { Type<T>::read(node) } -> std::same_as<std::expected<T, Error>>;
        { Type<T>::print(out, x) } -> std::same_as<void>;
    };

    namespace detail {

        template<typename T>
        struct stored {
            using type = T;
            static T const& unstore(T const& x) { return x; }
        };

        template<typename T>
        requires requires { typename Type<T>::stored_type; }
        struct stored<T> {
            using type = typename Type<T>::stored_type;
            static T const& unstore(type const& x) { return Type<T>::unstore(x); }
        };

    } // namespace detail

    template<CommandType T>
    bool printable(T const& x) {
        if constexpr (requires { Type<T>::printable(x); })
            return Type<T>::printable(x);
        else
            return true;
    }

    template<CommandType T>
    String to_string(T const& x) {
        String out;
        Type<T>::print(out, x);
        return out;
    }

    // The canonical line invoking `name` with `args`, for callers that
    // hold typed values (GUI gestures).  Nullopt if an argument has no
    // literal form; such an invocation cannot be recorded, so cannot be
    // issued (commands.md section 6).
    template<CommandType... Ts>
    std::optional<String> format_invocation(StringView name, Ts const&... args) {
        if (!(printable(args) && ...))
            return std::nullopt;
        String out(name);
        ((out.push_back(' '), Type<Ts>::print(out, args)), ...);
        return out;
    }

    // Reading helpers for Type implementations.

    // Integer syntax: an optional '-', then decimal digits or 0x and hex
    // digits.  Out-of-range is an error, not a wrap.
    std::expected<int64_t, Error> read_int64(Node const& node);

    // Check that a TAGGED node has tag `tag` and `arity` children.
    std::optional<Error> expect_tagged(Node const& node, StringView tag, size_t arity);

    // A word naming an enumerator, either bare ("SWAP") or qualified by
    // the enum's type name ("OPCODE::SWAP").  Returns the bare part, or
    // an error if the node is not a word or carries another qualifier.
    std::expected<StringView, Error> enumerator_word(Node const& node, StringView type_name);

    // Split "Q::N" into (Q, N); nullopt if there is no "::".
    std::optional<std::pair<StringView, StringView>> split_qualified(StringView word);

    template<> struct Type<bool> {
        static constexpr const char* name = "Bool";
        static std::expected<bool, Error> read(Node const& node);
        static void print(String& out, bool x);
    };

    template<> struct Type<int64_t> {
        static constexpr const char* name = "Int";
        static std::expected<int64_t, Error> read(Node const& node);
        static void print(String& out, int64_t x);
    };

    template<> struct Type<String> {
        static constexpr const char* name = "String";
        static std::expected<String, Error> read(Node const& node);
        static void print(String& out, String const& x);
    };

    // ================================================================
    // Commands
    // ================================================================

    enum class Category : uint8_t {
        local,      // affects only this process; runs immediately
        world,      // builds a Player::Action and submits it to the Server
        clock,      // pause / resume / step: addressed to the server role
        session,    // new game, save, load, quit
    };

    const char* name_from_category(Category);

    struct Pending;

    struct Reply {
        enum Status : uint8_t {
            OK,
            ERROR,
            PENDING,        // completes later; see Pending
            CANCELLED,      // a pending reply that was abandoned
        };
        Status status = OK;
        String text;                    // printed result, or error message
        std::shared_ptr<Pending> pending;   // PENDING only

        static Reply ok() { return Reply{}; }
        static Reply ok(String text) { return Reply{OK, std::move(text), {}}; }
        static Reply error(String text) { return Reply{ERROR, std::move(text), {}}; }
        static Reply error(Error const& e);
        static Reply later(std::shared_ptr<Pending> p) { return Reply{PENDING, {}, std::move(p)}; }
        static Reply cancelled() { return Reply{CANCELLED, String("cancelled"), {}}; }
    };

    // An asynchronous command's reply (commands.md section 8): `step 10`,
    // `frame 5` and `screenshot` cannot block the main thread.  The
    // executor polls once per frame, on the main thread, until poll
    // returns a final reply (never PENDING).  Pending state that holds
    // Terms must root them itself.
    struct Pending {
        virtual ~Pending() = default;
        virtual std::optional<Reply> poll() = 0;
        // Abandon the command, undoing what can be undone (a step budget
        // not yet spent).  The executor then completes it as CANCELLED.
        virtual void cancel() {}
    };

    // Where an invocation came from (commands.md section 3).  For logging
    // only; handlers do not branch on it.
    enum class Origin : uint8_t {
        gui,        // constructed by an overlay or the world pump
        binding,    // a key binding fired
        console,    // typed in the drop-down console
        socket,     // arrived on the control socket
    };

    const char* name_from_origin(Origin);

    // One issued invocation (commands.md section 6).  Text only: history
    // holds no Terms, so it keeps nothing alive.
    struct HistoryEntry {
        uint64_t seq = 0;
        uint64_t frame = 0;             // when issued
        Origin origin = Origin::gui;
        String line;                    // canonical, defaults resolved
        Reply::Status status = Reply::OK;   // PENDING until completed
        String reply;                   // the reply's text
        uint64_t completed_frame = 0;   // when completed (== frame if at once)
    };

    // `#41 frame 1200 console  step 10`, then the outcome if it is not a
    // plain immediate success: `  -> frame 1210`, `  ! message`,
    // `  (pending)`, `  (cancelled)`.
    String format_history_entry(HistoryEntry const& entry);

    // The result of running a line.  `line` is the canonical invocation
    // -- every argument printed, defaults resolved -- and is empty when
    // the line did not bind (parse error, unknown command, bad argument),
    // since then nothing was issued.  A blank line yields neither a line
    // nor a reply text.
    struct Outcome {
        String line;
        Reply reply;
    };

    // A parameter declaration.  The default, if any, is a constant or is
    // computed from the Context when the invocation is bound (the GUI
    // path: `pipette` with no argument takes the tile under the cursor).
    template<typename Context, CommandType T>
    struct Param {
        using value_type = T;
        using compute_type = std::function<std::expected<T, String>(Context&)>;

        const char* name;
        const char* summary;
        std::variant<std::monostate, T, compute_type> default_ = {};
        const char* default_summary = nullptr;  // describes a computed default
    };

    // Description of a parameter for help text and, later, tool schemas.
    struct ParamInfo {
        String name;
        String summary;
        String type_name;
        bool has_default = false;
        String default_text;            // canonical constant, or the
                                        // computed default's description
    };

    template<typename Context>
    struct CommandSpec {
        String name;
        String summary;
        Category category = Category::local;
        std::vector<ParamInfo> params;
        // Bind and read the arguments, resolve defaults, print the
        // canonical line, and run the handler.
        std::function<Outcome(Context&, LineSyntax const&)> invoke;
    };

    namespace detail {

        // Bind syntactic arguments to parameter positions: named ones by
        // name, positional ones to the next unfilled parameter in order.
        std::expected<std::vector<ArgumentSyntax const*>, Error>
        bind_arguments(LineSyntax const& line,
                       std::span<const char* const> param_names);

        template<typename Context, typename T>
        std::expected<typename stored<T>::type, Error>
        read_param(Context& context,
                   Param<Context, T> const& param,
                   ArgumentSyntax const* argument,
                   size_t line_end) {
            if (argument) {
                auto result = Type<T>::read(argument->node);
                if (!result)
                    return std::unexpected(std::move(result.error()));
                return typename stored<T>::type(std::move(*result));
            }
            if (auto* constant = std::get_if<T>(&param.default_))
                return typename stored<T>::type(*constant);
            if (auto* compute = std::get_if<typename Param<Context, T>::compute_type>(&param.default_)) {
                auto result = (*compute)(context);
                if (!result) {
                    String message("cannot compute default for ");
                    message.append(StringView(param.name));
                    message.append(StringView(": "));
                    message.append(result.error());
                    return std::unexpected(Error{std::move(message), line_end});
                }
                return typename stored<T>::type(std::move(*result));
            }
            String message("missing argument ");
            message.append(StringView(param.name));
            return std::unexpected(Error{std::move(message), line_end});
        }

        template<typename Context, typename T>
        ParamInfo param_info(Param<Context, T> const& param) {
            ParamInfo info;
            info.name = String(param.name);
            info.summary = String(param.summary);
            info.type_name = String(Type<T>::name);
            if (auto* constant = std::get_if<T>(&param.default_)) {
                info.has_default = true;
                Type<T>::print(info.default_text, *constant);
            } else if (std::holds_alternative<typename Param<Context, T>::compute_type>(param.default_)) {
                info.has_default = true;
                info.default_text = String(param.default_summary
                                           ? param.default_summary
                                           : "computed");
            }
            return info;
        }

    } // namespace detail

    // Make a command from typed parameter declarations and a handler
    // taking those types: Reply handler(Context&, T const&...).
    template<typename Context, typename... Ts, typename Handler>
    CommandSpec<Context> make_command(const char* name,
                                      const char* summary,
                                      Category category,
                                      std::tuple<Param<Context, Ts>...> params,
                                      Handler handler) {
        assert(is_valid_name(StringView(name)));
        CommandSpec<Context> spec;
        spec.name = String(name);
        spec.summary = String(summary);
        spec.category = category;
        std::apply([&](auto const&... p) {
            (spec.params.push_back(detail::param_info(p)), ...);
            (assert(is_valid_name(StringView(p.name))), ...);
        }, params);
        spec.invoke = [name = spec.name, params = std::move(params), handler = std::move(handler)]
        (Context& context, LineSyntax const& line) -> Outcome {
            const char* names[sizeof...(Ts) + 1] = {};
            std::apply([&](auto const&... p) {
                size_t i = 0;
                ((names[i++] = p.name), ...);
            }, params);
            auto bound = detail::bind_arguments(line, std::span<const char* const>(names, sizeof...(Ts)));
            if (!bound)
                return Outcome{{}, Reply::error(bound.error())};
            // The end of the line, for errors about absent arguments.
            size_t line_end = line.command_offset + line.command.chars.size();
            for (ArgumentSyntax const& a : line.arguments)
                line_end = std::max(line_end, a.node.offset);
            // Read each parameter in declaration order, stopping at the
            // first error.
            std::optional<Error> failure;
            auto read_all = [&]<size_t... I>(std::index_sequence<I...>) {
                std::tuple<std::optional<typename detail::stored<Ts>::type>...> slots;
                auto read_one = [&]<size_t J>(std::integral_constant<size_t, J>) {
                    if (failure)
                        return;
                    auto result = detail::read_param(context, std::get<J>(params), (*bound)[J], line_end);
                    if (!result) {
                        failure = std::move(result.error());
                        return;
                    }
                    std::get<J>(slots).emplace(std::move(*result));
                };
                (read_one(std::integral_constant<size_t, I>{}), ...);
                return slots;
            };
            auto slots = read_all(std::index_sequence_for<Ts...>{});
            if (failure)
                return Outcome{{}, Reply::error(*failure)};
            // Print the canonical invocation.  An argument with no
            // literal form cannot be recorded, so cannot be issued.
            Outcome outcome;
            outcome.line = name;
            std::optional<String> unprintable;
            auto print_all = [&]<size_t... I>(std::index_sequence<I...>) {
                auto print_one = [&]<size_t J>(std::integral_constant<size_t, J>) {
                    using T = std::tuple_element_t<J, std::tuple<Ts...>>;
                    T const& x = detail::stored<T>::unstore(*std::get<J>(slots));
                    if (!printable(x)) {
                        if (!unprintable)
                            unprintable = String(std::get<J>(params).name);
                        return;
                    }
                    outcome.line.append(StringView(" "));
                    Type<T>::print(outcome.line, x);
                };
                (print_one(std::integral_constant<size_t, I>{}), ...);
            };
            print_all(std::index_sequence_for<Ts...>{});
            if (unprintable) {
                String message("argument ");
                message.append(*unprintable);
                message.append(StringView(" has no literal form"));
                return Outcome{{}, Reply::error(std::move(message))};
            }
            outcome.reply = std::apply([&](auto const&... slot) {
                return handler(context, detail::stored<Ts>::unstore(*slot)...);
            }, slots);
            return outcome;
        };
        return spec;
    }

    template<typename Context>
    struct Registry {

        std::vector<CommandSpec<Context>> _commands;

        void add(CommandSpec<Context> spec) {
            assert(!find(spec.name) && "duplicate command name");
            _commands.push_back(std::move(spec));
        }

        CommandSpec<Context> const* find(StringView name) const {
            for (auto const& c : _commands)
                if (StringView(c.name) == name)
                    return &c;
            return nullptr;
        }

        Outcome run(Context& context, LineSyntax const& line) const {
            if (line.command.empty())
                return Outcome{};
            CommandSpec<Context> const* spec = find(line.command);
            if (!spec) {
                String message("unknown command ");
                message.append(line.command);
                return Outcome{{}, Reply::error(Error{std::move(message), line.command_offset})};
            }
            return spec->invoke(context, line);
        }

        Outcome run(Context& context, StringView text) const {
            auto line = parse_line(text);
            if (!line)
                return Outcome{{}, Reply::error(line.error())};
            return run(context, *line);
        }

    };

    // One line per command, or the full description of one command.
    template<typename Context>
    String help_text(Registry<Context> const& registry, StringView name = {});

    String help_line(StringView name, StringView summary, std::span<ParamInfo const> params);
    String help_detail(StringView name, StringView summary, Category category,
                       std::span<ParamInfo const> params);

    template<typename Context>
    String help_text(Registry<Context> const& registry, StringView name) {
        if (!name.empty()) {
            auto* spec = registry.find(name);
            if (!spec) {
                String message("unknown command ");
                message.append(name);
                return message;
            }
            return help_detail(spec->name, spec->summary, spec->category, spec->params);
        }
        String out;
        for (auto const& c : registry._commands) {
            out.append(help_line(c.name, c.summary, c.params));
            out.append(StringView("\n"));
        }
        return out;
    }

} // namespace wry::command

#endif /* command_hpp */
