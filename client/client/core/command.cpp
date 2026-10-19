//
//  command.cpp
//  client
//
//  Created by Antony Searle on 2026-10-12.
//

#include <charconv>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>

#include "command.hpp"
#include "test.hpp"

namespace wry::command {

    // ================================================================
    // Errors
    // ================================================================

    Error make_error(size_t offset, StringView message) {
        return Error{String(message), offset};
    }

    // ================================================================
    // Stage 1: tokens
    // ================================================================

    namespace {

        bool is_space(char ch) {
            return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'
                || ch == '\v' || ch == '\f';
        }

        bool is_delimiter(char ch) {
            return ch == '(' || ch == ')' || ch == ',' || ch == '='
                || ch == '"' || ch == '#';
        }

        bool is_word_byte(char ch) {
            return !is_space(ch) && !is_delimiter(ch);
        }

        struct Token {
            enum Kind : uint8_t {
                END,
                WORD,
                STRING,
                LPAREN,
                RPAREN,
                COMMA,
                EQUALS,
            };
            Kind kind = END;
            size_t offset = 0;
            String text;                // WORD and STRING only
        };

        const char* describe(Token::Kind kind) {
            switch (kind) {
                case Token::END:    return "end of line";
                case Token::WORD:   return "a word";
                case Token::STRING: return "a string";
                case Token::LPAREN: return "'('";
                case Token::RPAREN: return "')'";
                case Token::COMMA:  return "','";
                case Token::EQUALS: return "'='";
            }
            return "?";
        }

        // Tokenizer with one token of lookahead.  Operates on bytes: every
        // byte it splits at is ASCII, so the slices it takes of valid UTF-8
        // are themselves valid UTF-8.
        struct Lexer {

            const char* _begin;
            const char* _cursor;
            const char* _end;
            std::optional<Token> _peeked;

            explicit Lexer(StringView text)
            : _begin(text.chars.begin())
            , _cursor(text.chars.begin())
            , _end(text.chars.end()) {
            }

            size_t offset_of(const char* p) const {
                return (size_t)(p - _begin);
            }

            std::expected<Token, Error> _lex() {
                while (_cursor != _end && is_space(*_cursor))
                    ++_cursor;
                Token token;
                token.offset = offset_of(_cursor);
                if (_cursor == _end || *_cursor == '#') {
                    _cursor = _end;
                    token.kind = Token::END;
                    return token;
                }
                switch (*_cursor) {
                    case '(': ++_cursor; token.kind = Token::LPAREN; return token;
                    case ')': ++_cursor; token.kind = Token::RPAREN; return token;
                    case ',': ++_cursor; token.kind = Token::COMMA; return token;
                    case '=': ++_cursor; token.kind = Token::EQUALS; return token;
                    case '"': return _lex_string(token);
                    default: break;
                }
                const char* first = _cursor;
                while (_cursor != _end && is_word_byte(*_cursor))
                    ++_cursor;
                token.kind = Token::WORD;
                token.text = String(trusted, first, (size_t)(_cursor - first));
                return token;
            }

            std::expected<Token, Error> _lex_string(Token& token) {
                assert(*_cursor == '"');
                ++_cursor;
                token.kind = Token::STRING;
                for (;;) {
                    // Copy the run of raw bytes up to the next quote or
                    // backslash.
                    const char* first = _cursor;
                    while (_cursor != _end && *_cursor != '"' && *_cursor != '\\')
                        ++_cursor;
                    token.text.append(StringView(trusted, first, _cursor));
                    if (_cursor == _end)
                        return std::unexpected(make_error(token.offset, "unterminated string"));
                    if (*_cursor == '"') {
                        ++_cursor;
                        return std::move(token);
                    }
                    // Escape.
                    const char* escape = _cursor++;
                    if (_cursor == _end)
                        return std::unexpected(make_error(offset_of(escape), "unterminated string"));
                    switch (*_cursor++) {
                        case '"':  token.text.push_back('"');  break;
                        case '\\': token.text.push_back('\\'); break;
                        case 'n':  token.text.push_back('\n'); break;
                        case 't':  token.text.push_back('\t'); break;
                        case 'r':  token.text.push_back('\r'); break;
                        case 'u': {
                            if (_cursor == _end || *_cursor != '{')
                                return std::unexpected(make_error(offset_of(escape), "expected \\u{hex}"));
                            const char* digits = ++_cursor;
                            while (_cursor != _end && *_cursor != '}')
                                ++_cursor;
                            if (_cursor == _end)
                                return std::unexpected(make_error(offset_of(escape), "expected \\u{hex}"));
                            uint32_t code = 0;
                            auto [ptr, ec] = std::from_chars(digits, _cursor, code, 16);
                            if (digits == _cursor || ptr != _cursor || ec != std::errc{}
                                || code > 0x10FFFF
                                || (code >= 0xD800 && code <= 0xDFFF))
                                return std::unexpected(make_error(offset_of(escape), "invalid \\u{hex} escape"));
                            ++_cursor;
                            token.text.push_back((char32_t)code);
                            break;
                        }
                        default:
                            return std::unexpected(make_error(offset_of(escape), "unknown escape"));
                    }
                }
            }

            std::expected<Token, Error> next() {
                if (_peeked)
                    return std::move(*std::exchange(_peeked, std::nullopt));
                return _lex();
            }

            std::expected<Token const*, Error> peek() {
                if (!_peeked) {
                    auto token = _lex();
                    if (!token)
                        return std::unexpected(std::move(token.error()));
                    _peeked = std::move(*token);
                }
                return &*_peeked;
            }

        }; // struct Lexer

        Error unexpected_token(Token const& token, const char* wanted) {
            String message("expected ");
            message.append(StringView(wanted));
            message.append(StringView(", found "));
            message.append(StringView(describe(token.kind)));
            return Error{std::move(message), token.offset};
        }

        // node := word | string | word '(' [node (',' node)*] ')'
        std::expected<Node, Error> parse_node_from(Lexer& lexer, Token first) {
            Node node;
            node.offset = first.offset;
            if (first.kind == Token::STRING) {
                node.kind = Node::STRING;
                node.text = std::move(first.text);
                return node;
            }
            if (first.kind != Token::WORD)
                return std::unexpected(unexpected_token(first, "an argument"));
            node.text = std::move(first.text);
            auto peeked = lexer.peek();
            if (!peeked)
                return std::unexpected(std::move(peeked.error()));
            if ((*peeked)->kind != Token::LPAREN) {
                node.kind = Node::WORD;
                return node;
            }
            node.kind = Node::TAGGED;
            (void) lexer.next();
            peeked = lexer.peek();
            if (!peeked)
                return std::unexpected(std::move(peeked.error()));
            if ((*peeked)->kind == Token::RPAREN) {
                (void) lexer.next();
                return node;
            }
            for (;;) {
                auto token = lexer.next();
                if (!token)
                    return std::unexpected(std::move(token.error()));
                auto child = parse_node_from(lexer, std::move(*token));
                if (!child)
                    return child;
                node.children.push_back(std::move(*child));
                auto separator = lexer.next();
                if (!separator)
                    return std::unexpected(std::move(separator.error()));
                if (separator->kind == Token::RPAREN)
                    return node;
                if (separator->kind != Token::COMMA)
                    return std::unexpected(unexpected_token(*separator, "',' or ')'"));
            }
        }

    } // namespace

    std::expected<LineSyntax, Error> parse_line(StringView text) {
        Lexer lexer(text);
        LineSyntax line;
        auto head = lexer.next();
        if (!head)
            return std::unexpected(std::move(head.error()));
        if (head->kind == Token::END)
            return line;
        if (head->kind != Token::WORD)
            return std::unexpected(unexpected_token(*head, "a command name"));
        line.command = std::move(head->text);
        line.command_offset = head->offset;
        for (;;) {
            auto token = lexer.next();
            if (!token)
                return std::unexpected(std::move(token.error()));
            if (token->kind == Token::END)
                return line;
            ArgumentSyntax argument;
            if (token->kind == Token::WORD) {
                auto peeked = lexer.peek();
                if (!peeked)
                    return std::unexpected(std::move(peeked.error()));
                if ((*peeked)->kind == Token::EQUALS) {
                    (void) lexer.next();
                    argument.name = std::move(token->text);
                    argument.name_offset = token->offset;
                    token = lexer.next();
                    if (!token)
                        return std::unexpected(std::move(token.error()));
                }
            }
            auto node = parse_node_from(lexer, std::move(*token));
            if (!node)
                return std::unexpected(std::move(node.error()));
            argument.node = std::move(*node);
            line.arguments.push_back(std::move(argument));
        }
    }

    std::expected<Node, Error> parse_node(StringView text) {
        Lexer lexer(text);
        auto token = lexer.next();
        if (!token)
            return std::unexpected(std::move(token.error()));
        auto node = parse_node_from(lexer, std::move(*token));
        if (!node)
            return node;
        auto rest = lexer.next();
        if (!rest)
            return std::unexpected(std::move(rest.error()));
        if (rest->kind != Token::END)
            return std::unexpected(unexpected_token(*rest, "end of literal"));
        return node;
    }

    // ================================================================
    // Stage 1: printing
    // ================================================================

    bool is_word(StringView text) {
        if (text.empty())
            return false;
        for (char ch : text.chars)
            if (!is_word_byte(ch))
                return false;
        return true;
    }

    bool is_valid_name(StringView text) {
        if (text.empty())
            return false;
        for (char ch : text.chars)
            if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-'))
                return false;
        return true;
    }

    void print_quoted(String& out, StringView text) {
        static constexpr char hex[] = "0123456789ABCDEF";
        out.push_back('"');
        const char* first = text.chars.begin();
        const char* last = text.chars.end();
        const char* run = first;
        auto flush = [&](const char* p) {
            out.append(StringView(trusted, run, p));
        };
        for (const char* p = first; p != last; ++p) {
            const unsigned char ch = (unsigned char)*p;
            const char* escape = nullptr;
            switch (ch) {
                case '"':  escape = "\\\""; break;
                case '\\': escape = "\\\\"; break;
                case '\n': escape = "\\n";  break;
                case '\t': escape = "\\t";  break;
                case '\r': escape = "\\r";  break;
                default: break;
            }
            if (escape) {
                flush(p);
                out.append(StringView(escape));
                run = p + 1;
            } else if (ch < 0x20 || ch == 0x7F) {
                flush(p);
                out.append(StringView("\\u{"));
                if (ch >= 0x10)
                    out.push_back(hex[ch >> 4]);
                out.push_back(hex[ch & 0xF]);
                out.push_back('}');
                run = p + 1;
            }
        }
        flush(last);
        out.push_back('"');
    }

    void print_node(String& out, Node const& node) {
        switch (node.kind) {
            case Node::WORD:
                assert(is_word(node.text));
                out.append(node.text);
                break;
            case Node::STRING:
                print_quoted(out, node.text);
                break;
            case Node::TAGGED:
                assert(is_word(node.text));
                out.append(node.text);
                out.push_back('(');
                for (size_t i = 0; i != node.children.size(); ++i) {
                    if (i)
                        out.append(StringView(", "));
                    print_node(out, node.children[i]);
                }
                out.push_back(')');
                break;
        }
    }

    String print_line(LineSyntax const& line) {
        String out;
        if (line.command.empty())
            return out;
        out.append(line.command);
        for (ArgumentSyntax const& argument : line.arguments) {
            out.push_back(' ');
            if (!argument.name.empty()) {
                out.append(argument.name);
                out.push_back('=');
            }
            print_node(out, argument.node);
        }
        return out;
    }

    bool equivalent(Node const& a, Node const& b) {
        if (a.kind != b.kind || a.text != b.text || a.children.size() != b.children.size())
            return false;
        for (size_t i = 0; i != a.children.size(); ++i)
            if (!equivalent(a.children[i], b.children[i]))
                return false;
        return true;
    }

    bool equivalent(LineSyntax const& a, LineSyntax const& b) {
        if (a.command != b.command || a.arguments.size() != b.arguments.size())
            return false;
        for (size_t i = 0; i != a.arguments.size(); ++i)
            if (a.arguments[i].name != b.arguments[i].name
                || !equivalent(a.arguments[i].node, b.arguments[i].node))
                return false;
        return true;
    }

    // ================================================================
    // Stage 2: reading helpers
    // ================================================================

    std::expected<int64_t, Error> read_int64(Node const& node) {
        if (node.kind != Node::WORD)
            return std::unexpected(make_error(node.offset, "expected an integer"));
        const char* p = node.text.chars.begin();
        const char* end = node.text.chars.end();
        bool negative = false;
        if (p != end && *p == '-') {
            negative = true;
            ++p;
        }
        int base = 10;
        if (end - p > 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            base = 16;
            p += 2;
        }
        // Parse the magnitude unsigned, so that INT64_MIN is reachable.
        uint64_t magnitude = 0;
        auto [ptr, ec] = std::from_chars(p, end, magnitude, base);
        if (p == end || ptr != end || (ec != std::errc{} && ec != std::errc::result_out_of_range))
            return std::unexpected(make_error(node.offset, "expected an integer"));
        const uint64_t limit = negative ? (uint64_t)1 << 63 : ((uint64_t)1 << 63) - 1;
        if (ec == std::errc::result_out_of_range || magnitude > limit)
            return std::unexpected(make_error(node.offset, "integer out of range"));
        return negative ? (int64_t)(0 - magnitude) : (int64_t)magnitude;
    }

    std::optional<Error> expect_tagged(Node const& node, StringView tag, size_t arity) {
        if (node.kind != Node::TAGGED || node.text != tag) {
            String message("expected ");
            message.append(tag);
            message.append(StringView("(...)"));
            return Error{std::move(message), node.offset};
        }
        if (node.children.size() != arity) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), " takes %zu argument%s",
                          arity, arity == 1 ? "" : "s");
            String message(tag);
            message.append(StringView(buffer));
            return Error{std::move(message), node.offset};
        }
        return std::nullopt;
    }

    std::optional<std::pair<StringView, StringView>> split_qualified(StringView word) {
        const char* first = word.chars.begin();
        const char* last = word.chars.end();
        for (const char* p = first; p + 1 < last; ++p)
            if (p[0] == ':' && p[1] == ':')
                return std::pair{StringView(trusted, first, p), StringView(trusted, p + 2, last)};
        return std::nullopt;
    }

    std::expected<StringView, Error> enumerator_word(Node const& node, StringView type_name) {
        if (node.kind != Node::WORD) {
            String message("expected ");
            message.append(type_name);
            message.append(StringView(" enumerator"));
            return std::unexpected(Error{std::move(message), node.offset});
        }
        StringView word = node.text;
        if (auto split = split_qualified(word)) {
            if (split->first != type_name) {
                String message("expected ");
                message.append(type_name);
                message.append(StringView("::..., found "));
                message.append(split->first);
                message.append(StringView("::..."));
                return std::unexpected(Error{std::move(message), node.offset});
            }
            word.reset(split->second);
        }
        if (word.empty())
            return std::unexpected(make_error(node.offset, "empty enumerator"));
        return word;
    }

    // ================================================================
    // Stage 2: primitive types
    // ================================================================

    std::expected<bool, Error> Type<bool>::read(Node const& node) {
        if (node.kind == Node::WORD) {
            if (node.text == StringView("true"))
                return true;
            if (node.text == StringView("false"))
                return false;
        }
        return std::unexpected(make_error(node.offset, "expected true or false"));
    }

    void Type<bool>::print(String& out, bool x) {
        out.append(StringView(x ? "true" : "false"));
    }

    std::expected<int64_t, Error> Type<int64_t>::read(Node const& node) {
        return read_int64(node);
    }

    void Type<int64_t>::print(String& out, int64_t x) {
        char buffer[24];
        auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), x);
        assert(ec == std::errc{});
        out.append(StringView(trusted, buffer, ptr));
    }

    // Contextual: a bare word is a String here.
    std::expected<String, Error> Type<String>::read(Node const& node) {
        if (node.kind == Node::TAGGED)
            return std::unexpected(make_error(node.offset, "expected a string"));
        return node.text;
    }

    void Type<String>::print(String& out, String const& x) {
        print_quoted(out, x);
    }

    // ================================================================
    // Commands
    // ================================================================

    const char* name_from_category(Category category) {
        switch (category) {
            case Category::local:   return "local";
            case Category::world:   return "world";
            case Category::clock:   return "clock";
            case Category::session: return "session";
        }
        return "?";
    }

    const char* name_from_origin(Origin origin) {
        switch (origin) {
            case Origin::gui:     return "gui";
            case Origin::binding: return "binding";
            case Origin::console: return "console";
            case Origin::socket:  return "socket";
        }
        return "?";
    }

    String format_history_entry(HistoryEntry const& entry) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "#%llu frame %llu %-8s ",
                      (unsigned long long)entry.seq,
                      (unsigned long long)entry.frame,
                      name_from_origin(entry.origin));
        String out(buffer);
        out.append(entry.line);
        switch (entry.status) {
            case Reply::OK:
                if (entry.completed_frame != entry.frame) {
                    std::snprintf(buffer, sizeof(buffer), "  -> frame %llu",
                                  (unsigned long long)entry.completed_frame);
                    out.append(StringView(buffer));
                }
                break;
            case Reply::ERROR:
                out.append(StringView("  ! "));
                out.append(entry.reply);
                break;
            case Reply::PENDING:
                out.append(StringView("  (pending)"));
                break;
            case Reply::CANCELLED:
                out.append(StringView("  (cancelled)"));
                break;
        }
        if (!entry.peer.empty()) {
            out.append(StringView("  from "));
            out.append(entry.peer);
        }
        return out;
    }

    Reply Reply::error(Error const& e) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "column %zu: ", e.offset + 1);
        String text(buffer);
        text.append(e.message);
        return Reply{ERROR, std::move(text)};
    }

    namespace detail {

        std::expected<std::vector<ArgumentSyntax const*>, Error>
        bind_arguments(LineSyntax const& line,
                       std::span<const char* const> param_names) {
            std::vector<ArgumentSyntax const*> bound(param_names.size(), nullptr);
            for (ArgumentSyntax const& argument : line.arguments) {
                size_t index = 0;
                if (!argument.name.empty()) {
                    while (index != param_names.size()
                           && argument.name != StringView(param_names[index]))
                        ++index;
                    if (index == param_names.size()) {
                        String message("unknown parameter ");
                        message.append(argument.name);
                        return std::unexpected(Error{std::move(message), argument.name_offset});
                    }
                    if (bound[index]) {
                        String message("parameter ");
                        message.append(argument.name);
                        message.append(StringView(" given twice"));
                        return std::unexpected(Error{std::move(message), argument.name_offset});
                    }
                } else {
                    while (index != param_names.size() && bound[index])
                        ++index;
                    if (index == param_names.size())
                        return std::unexpected(make_error(argument.node.offset, "too many arguments"));
                }
                bound[index] = &argument;
            }
            return bound;
        }

    } // namespace detail

    String help_line(StringView name, StringView summary, std::span<ParamInfo const> params) {
        String out(name);
        for (ParamInfo const& p : params) {
            out.push_back(' ');
            if (p.has_default)
                out.push_back('[');
            out.append(p.name);
            out.push_back(':');
            out.append(p.type_name);
            if (p.has_default)
                out.push_back(']');
        }
        out.append(StringView("  -- "));
        out.append(summary);
        return out;
    }

    String help_detail(StringView name, StringView summary, Category category,
                       std::span<ParamInfo const> params) {
        String out = help_line(name, summary, params);
        out.append(StringView("\n  category: "));
        out.append(StringView(name_from_category(category)));
        for (ParamInfo const& p : params) {
            out.append(StringView("\n  "));
            out.append(p.name);
            out.push_back(':');
            out.append(p.type_name);
            if (p.has_default) {
                out.append(StringView(" = "));
                out.append(p.default_text);
            }
            out.append(StringView("  "));
            out.append(p.summary);
        }
        return out;
    }

    // ================================================================
    // Tests
    // ================================================================

    namespace {

        // Fragments biased towards the grammar's edge cases.  Non-ASCII
        // fragments are spelled as escapes to keep this file ASCII.
        constexpr const char* string_fragments[] = {
            "a", "Z", "0", "7", "-", "_", ":", "::", ".", "+",
            " ", "\t", "\n", "\r", "\"", "\\", "(", ")", ",", "=", "#",
            "\x01", "\x1f", "\x7f",
            "\xc3\xa9",                 // U+00E9
            "\xe2\x86\x92",             // U+2192
            "\xf0\x9f\x98\x80",         // U+1F600
        };

        constexpr const char* line_fragments[] = {
            "a", "b", "Q", "1", "-2", "0x1F", "OPCODE::SWAP", "x-y", "::",
            " ", "  ", "\t",
            "(", ")", ",", "=", "#", "\"", "\\", "\\\"", "\\n", "\\u{41}",
            "\\u{", "}", "\\q", "\"hi\"", "\"a b\"",
            "\xc3\xa9",
        };

        template<size_t N>
        String random_from(std::mt19937_64& rng, const char* const (&fragments)[N], size_t max_count) {
            String out;
            size_t count = std::uniform_int_distribution<size_t>(0, max_count)(rng);
            for (size_t i = 0; i != count; ++i)
                out.append(StringView(fragments[std::uniform_int_distribution<size_t>(0, N - 1)(rng)]));
            return out;
        }

        int64_t random_int64(std::mt19937_64& rng) {
            switch (std::uniform_int_distribution<int>(0, 3)(rng)) {
                case 0: return std::uniform_int_distribution<int64_t>(-100, 100)(rng);
                case 1: return (int64_t)rng();
                case 2: return std::numeric_limits<int64_t>::min() + (int64_t)(rng() % 3);
                default: return std::numeric_limits<int64_t>::max() - (int64_t)(rng() % 3);
            }
        }

        // Read `text` as a T through a whole command line, the way an
        // argument actually arrives, and through parse_node.
        template<typename T>
        T reread(StringView text) {
            String line("probe ");
            line.append(text);
            auto parsed = parse_line(line);
            assert(parsed && parsed->arguments.size() == 1);
            auto via_line = Type<T>::read(parsed->arguments[0].node);
            assert(via_line);
            auto node = parse_node(text);
            assert(node);
            auto via_node = Type<T>::read(*node);
            assert(via_node);
            assert(*via_line == *via_node);
            return std::move(*via_line);
        }

        struct TestContext {
            String user;
        };

    } // namespace

    define_test("command_syntax") {

        // Blank and comment-only lines.
        {
            auto a = parse_line("");
            assert(a && a->command.empty());
            auto b = parse_line("   # nothing");
            assert(b && b->command.empty());
        }

        // Shapes.
        {
            auto line = parse_line("write Coordinate(12, -34) term=OPCODE::SWAP \"a b\" # comment");
            assert(line);
            assert(line->command == StringView("write"));
            assert(line->arguments.size() == 3);
            Node const& c = line->arguments[0].node;
            assert(c.kind == Node::TAGGED && c.text == StringView("Coordinate"));
            assert(c.children.size() == 2);
            assert(c.children[1].kind == Node::WORD && c.children[1].text == StringView("-34"));
            assert(line->arguments[1].name == StringView("term"));
            assert(line->arguments[1].node.text == StringView("OPCODE::SWAP"));
            assert(line->arguments[2].node.kind == Node::STRING);
            assert(line->arguments[2].node.text == StringView("a b"));
            assert(print_line(*line)
                   == StringView("write Coordinate(12, -34) term=OPCODE::SWAP \"a b\""));
        }

        // Whitespace is insignificant between tokens; nesting; empty tags.
        {
            auto a = parse_line("f  x = T ( 1 ,U( ) )");
            auto b = parse_line("f x=T(1, U())");
            assert(a && b && equivalent(*a, *b));
        }

        // Escapes.
        {
            auto line = parse_line("s \"q\\\"b\\\\n\\n\\t\\r\\u{41}\\u{e9}\"");
            assert(line);
            String expected("q\"b\\n\n\t\rA");
            expected.push_back((char32_t)0xE9);
            assert(line->arguments[0].node.text == expected);
        }

        // Malformed input is rejected with an offset, never a crash.
        {
            const char* bad[] = {
                "(", ")", "=", ",", "\"cmd\"", "f )", "f ,", "f =x", "f x=",
                "f x==y", "f T(", "f T(1", "f T(1 2)", "f T(1,)", "f T(,1)",
                "f \"open", "f \"\\", "f \"\\q\"", "f \"\\u{}\"", "f \"\\u{D800}\"",
                "f \"\\u{110000}\"", "f \"\\u41\"", "f \"\\u{41\"", "f x=y=z",
            };
            for (const char* text : bad) {
                auto line = parse_line(StringView(text));
                assert(!line);
                assert(line.error().offset <= std::strlen(text));
            }
        }

        // Fuzz: whatever parses re-prints to something that parses to the
        // same tree, and printing is idempotent.
        {
            std::mt19937_64 rng(20261012);
            size_t accepted = 0;
            for (int i = 0; i != 20000; ++i) {
                String text = random_from(rng, line_fragments, 12);
                auto line = parse_line(text);
                if (!line) {
                    assert(line.error().offset <= text.chars.size());
                    continue;
                }
                ++accepted;
                String printed = print_line(*line);
                auto again = parse_line(printed);
                assert(again);
                assert(equivalent(*line, *again));
                assert(print_line(*again) == printed);
            }
            assert(accepted > 1000);
        }

        co_return;
    };

    define_test("command_primitive_types") {

        std::mt19937_64 rng(1012);

        // Bool.
        for (bool b : {false, true})
            assert(reread<bool>(to_string(b)) == b);

        // Int: random, extremes, hex input.
        for (int i = 0; i != 10000; ++i) {
            int64_t x = random_int64(rng);
            assert(reread<int64_t>(to_string(x)) == x);
        }
        assert(reread<int64_t>("0x7fffffffffffffff") == std::numeric_limits<int64_t>::max());
        assert(reread<int64_t>("-0x8000000000000000") == std::numeric_limits<int64_t>::min());
        assert(reread<int64_t>("-0") == 0);
        for (const char* text : {"9223372036854775808", "-9223372036854775809",
                                 "0x10000000000000000", "1a", "0x", "-", "--1",
                                 "1-", "+1", "0x-1", "x"}) {
            auto node = parse_node(StringView(text));
            assert(node);
            assert(!Type<int64_t>::read(*node));
        }

        // String: random content, including every escape and non-ASCII;
        // contextual bare words.
        for (int i = 0; i != 10000; ++i) {
            String s = random_from(rng, string_fragments, 10);
            String printed = to_string(s);
            assert(reread<String>(printed) == s);
            assert(printed.chars.size() >= 2);
        }
        assert(reread<String>("probe") == StringView("probe"));
        {
            auto node = parse_node("T(1)");
            assert(node && !Type<String>::read(*node));
        }

        co_return;
    };

    define_test("command_registry") {

        Registry<TestContext> registry;
        using Ctx = TestContext;

        registry.add(make_command<Ctx>(
            "add", "Add two integers", Category::local,
            std::tuple{
                Param<Ctx, int64_t>{"a", "first"},
                Param<Ctx, int64_t>{"b", "second", int64_t{10}},
            },
            [](Ctx&, int64_t a, int64_t b) {
                return Reply::ok(to_string(a + b));
            }));

        registry.add(make_command<Ctx>(
            "greet", "Greet someone", Category::local,
            std::tuple{
                Param<Ctx, String>{
                    "who", "whom to greet",
                    [](Ctx& c) -> std::expected<String, String> {
                        if (c.user.empty())
                            return std::unexpected(String("nobody is logged in"));
                        return c.user;
                    },
                    "the current user"},
                Param<Ctx, bool>{"loud", "shout", false},
            },
            [](Ctx&, String const& who, bool loud) {
                String text(loud ? "HELLO " : "hello ");
                text.append(who);
                return Reply::ok(std::move(text));
            }));

        registry.add(make_command<Ctx>(
            "noop", "Do nothing", Category::clock,
            std::tuple<>{},
            [](Ctx&) { return Reply::ok(); }));

        TestContext context;
        context.user = String("antony");

        auto run = [&](const char* text) {
            return registry.run(context, StringView(text));
        };

        // Positional, named, defaults resolved into the canonical line.
        {
            auto o = run("add 1 2");
            assert(o.reply.status == Reply::OK && o.reply.text == StringView("3"));
            assert(o.line == StringView("add 1 2"));
        }
        {
            auto o = run("add 0x10");
            assert(o.reply.text == StringView("26"));
            assert(o.line == StringView("add 16 10"));
        }
        {
            auto o = run("add b=1 2");
            assert(o.reply.text == StringView("3"));
            assert(o.line == StringView("add 2 1"));
        }
        {
            auto o = run("greet");
            assert(o.reply.text == StringView("hello antony"));
            assert(o.line == StringView("greet \"antony\" false"));
        }
        {
            auto o = run("greet loud=true");
            assert(o.reply.text == StringView("HELLO antony"));
        }
        {
            auto o = run("greet bob");
            assert(o.line == StringView("greet \"bob\" false"));
        }
        {
            auto o = run("noop  # nothing");
            assert(o.reply.status == Reply::OK && o.line == StringView("noop"));
        }

        // The canonical line re-runs to the same canonical line.
        for (const char* text : {"add 1", "add b=-3 a=0x7", "greet", "greet x loud=true"}) {
            auto first = run(text);
            assert(first.reply.status == Reply::OK);
            auto second = registry.run(context, StringView(first.line));
            assert(second.line == first.line);
            assert(second.reply.text == first.reply.text);
        }

        // Blank lines issue nothing.
        {
            auto o = run("   ");
            assert(o.line.empty() && o.reply.status == Reply::OK && o.reply.text.empty());
        }

        // Binding errors issue nothing.
        for (const char* text : {"nope", "add", "add 1 2 3", "add c=1 2",
                                 "add a=1 a=2", "add 1 a=2", "add x", "add \"1\"",
                                 "greet loud=yes", "add T(1)", "add (1"}) {
            auto o = run(text);
            assert(o.reply.status == Reply::ERROR);
            assert(o.line.empty());
        }

        // A computed default that cannot be computed is an error...
        context.user.clear();
        assert(run("greet").reply.status == Reply::ERROR);
        // ...unless the argument is supplied.
        assert(run("greet carol").reply.status == Reply::OK);

        // Help.
        {
            String all = help_text(registry);
            assert(!all.empty());
            String one = help_text(registry, StringView("greet"));
            assert(!one.empty());
        }

        co_return;
    };

} // namespace wry::command
