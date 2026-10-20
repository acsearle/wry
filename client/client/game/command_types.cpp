//
//  command_types.cpp
//  client
//
//  Created by Antony Searle on 2026-10-12.
//

#include <charconv>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>

#include "command_types.hpp"
#include "HeapString.hpp"
#include "test.hpp"

namespace wry::command {

    namespace {

        // Enumerator tables pair each value with its C++ spelling,
        // "OPCODE_SWAP"; the literal's bare name drops the "OPCODE_".
        template<typename E, size_t N>
        std::expected<E, Error> read_enum(Node const& node,
                                          const char* type_name,
                                          std::pair<E, const char*> const (&names)[N]) {
            auto word = enumerator_word(node, StringView(type_name));
            if (!word)
                return std::unexpected(std::move(word.error()));
            const size_t prefix = std::strlen(type_name) + 1;
            for (auto const& [value, spelling] : names)
                if (StringView(spelling + prefix) == *word)
                    return value;
            String message("unknown ");
            message.append(StringView(type_name));
            message.append(StringView(" enumerator "));
            message.append(*word);
            return std::unexpected(Error{std::move(message), node.offset});
        }

        void print_enum(String& out, const char* type_name, const char* spelling) {
            assert(spelling);
            const size_t prefix = std::strlen(type_name) + 1;
            assert(std::strncmp(spelling, type_name, prefix - 1) == 0
                   && spelling[prefix - 1] == '_');
            out.append(StringView(type_name));
            out.append(StringView("::"));
            out.append(StringView(spelling + prefix));
        }

        // Whether `word` names an enumerator of any Term enum, for the
        // suggestion attached to a bare word in Term context.
        template<typename E, size_t N>
        bool names_enumerator(StringView word, const char* type_name,
                              std::pair<E, const char*> const (&names)[N]) {
            const size_t prefix = std::strlen(type_name) + 1;
            for (auto const& [value, spelling] : names)
                if (StringView(spelling + prefix) == word)
                    return true;
            return false;
        }

        bool is_heap(Term x, uint64_t save_type_tag) {
            HeapTerm const* p = _term_as_nullable_pointer(x);
            return p && p->_save_type_tag() == save_type_tag;
        }

        bool is_string(Term x) {
            return _term_is_short_string(x) || is_heap(x, HeapString::SAVE_TYPE_TAG);
        }

    } // namespace

    // ----------------------------------------------------------------
    // Coordinate
    // ----------------------------------------------------------------

    std::expected<Coordinate, Error> Type<Coordinate>::read(Node const& node) {
        if (auto e = expect_tagged(node, StringView(name), 2))
            return std::unexpected(std::move(*e));
        i32 xy[2];
        for (size_t i = 0; i != 2; ++i) {
            auto z = read_int64(node.children[i]);
            if (!z)
                return std::unexpected(std::move(z.error()));
            if (*z < std::numeric_limits<i32>::min() || *z > std::numeric_limits<i32>::max())
                return std::unexpected(make_error(node.children[i].offset,
                                                  "coordinate out of range"));
            xy[i] = (i32)*z;
        }
        return Coordinate{xy[0], xy[1]};
    }

    void Type<Coordinate>::print(String& out, Coordinate const& x) {
        out.append(StringView("Coordinate("));
        Type<int64_t>::print(out, x.x);
        out.append(StringView(", "));
        Type<int64_t>::print(out, x.y);
        out.push_back(')');
    }

    // ----------------------------------------------------------------
    // EntityID
    // ----------------------------------------------------------------

    std::expected<EntityID, Error> Type<EntityID>::read(Node const& node) {
        if (auto e = expect_tagged(node, StringView(name), 1))
            return std::unexpected(std::move(*e));
        auto z = read_int64(node.children[0]);
        if (!z)
            return std::unexpected(std::move(z.error()));
        // EntityIDs fit the 60-bit Term packing by construction (term.hpp).
        if (*z < 0 || (*z >> 60))
            return std::unexpected(make_error(node.children[0].offset,
                                              "entity id out of range"));
        return EntityID{(uint64_t)*z};
    }

    void Type<EntityID>::print(String& out, EntityID const& x) {
        out.append(StringView("EntityID("));
        Type<int64_t>::print(out, (int64_t)x.data);
        out.push_back(')');
    }

    // ----------------------------------------------------------------
    // Hash
    // ----------------------------------------------------------------

    std::expected<Hash, Error> Type<Hash>::read(Node const& node) {
        if (auto e = expect_tagged(node, StringView(name), 1))
            return std::unexpected(std::move(*e));
        Node const& child = node.children[0];
        // Exactly the printed form: 0x then 1..16 hex digits (read_int64
        // would refuse the top bit).
        const char* p = child.text.chars.begin();
        const char* end = child.text.chars.end();
        uint64_t value = 0;
        if (child.kind == Node::WORD && end - p > 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            auto [ptr, ec] = std::from_chars(p + 2, end, value, 16);
            if (ptr == end && ec == std::errc{})
                return Hash{value};
        }
        return std::unexpected(make_error(child.offset, "expected a hash, 0x followed by up to 16 hex digits"));
    }

    void Type<Hash>::print(String& out, Hash const& x) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "Hash(0x%016llx)", (unsigned long long)x.value);
        out.append(StringView(buffer));
    }

    // ----------------------------------------------------------------
    // Enums
    // ----------------------------------------------------------------

    std::expected<OPCODE, Error> Type<OPCODE>::read(Node const& node) {
        return read_enum(node, name, OPCODE_NAMES);
    }

    void Type<OPCODE>::print(String& out, OPCODE const& x) {
        print_enum(out, name, name_from_OPCODE(x));
    }

    std::expected<MATTER, Error> Type<MATTER>::read(Node const& node) {
        return read_enum(node, name, MATTER_NAMES);
    }

    void Type<MATTER>::print(String& out, MATTER const& x) {
        print_enum(out, name, name_from_MATTER(x));
    }

    // ----------------------------------------------------------------
    // Term
    // ----------------------------------------------------------------

    // Self-describing literals only: there is no expected type to read a
    // bare word against.
    std::expected<Term, Error> Type<Term>::read(Node const& node) {
        switch (node.kind) {

            case Node::STRING:
                return term_make_string_with(std::string_view(node.text.data(),
                                                              node.text.chars.size()));

            case Node::TAGGED: {
                if (node.text == StringView(Type<EntityID>::name)) {
                    auto id = Type<EntityID>::read(node);
                    if (!id)
                        return std::unexpected(std::move(id.error()));
                    return term_make_entity_id(*id);
                }
                String message(node.text);
                message.append(StringView(" is not a Term"));
                return std::unexpected(Error{std::move(message), node.offset});
            }

            case Node::WORD: {
                StringView word = node.text;
                if (word == StringView("null"))
                    return term_make_null();
                if (word == StringView("true"))
                    return term_make_true();
                if (word == StringView("false"))
                    return term_make_false();
                char first = *word.chars.begin();
                if ((first >= '0' && first <= '9') || first == '-') {
                    auto z = read_int64(node);
                    if (!z)
                        return std::unexpected(std::move(z.error()));
                    return term_make_integer_with(*z);
                }
                if (auto split = split_qualified(word)) {
                    if (split->first == StringView(Type<OPCODE>::name)) {
                        auto x = Type<OPCODE>::read(node);
                        if (!x)
                            return std::unexpected(std::move(x.error()));
                        return term_make_opcode((int)*x);
                    }
                    if (split->first == StringView(Type<MATTER>::name)) {
                        auto x = Type<MATTER>::read(node);
                        if (!x)
                            return std::unexpected(std::move(x.error()));
                        return term_make_matter((int)*x);
                    }
                    String message("unknown enum ");
                    message.append(split->first);
                    return std::unexpected(Error{std::move(message), node.offset});
                }
                // A bare word: never inferred, but say what was probably
                // meant (commands.md 4.2).
                String message("a Term needs a self-describing literal; bare word ");
                message.append(word);
                const char* qualifier = nullptr;
                if (names_enumerator(word, Type<OPCODE>::name, OPCODE_NAMES))
                    qualifier = Type<OPCODE>::name;
                else if (names_enumerator(word, Type<MATTER>::name, MATTER_NAMES))
                    qualifier = Type<MATTER>::name;
                if (qualifier) {
                    message.append(StringView(" (did you mean "));
                    message.append(StringView(qualifier));
                    message.append(StringView("::"));
                    message.append(word);
                    message.append(StringView("?)"));
                } else {
                    message.append(StringView(" (strings are quoted)"));
                }
                return std::unexpected(Error{std::move(message), node.offset});
            }
        }
        return std::unexpected(make_error(node.offset, "expected a Term"));
    }

    bool Type<Term>::printable(Term const& x) {
        if (term_is_null(x) || term_is_boolean(x) || x.is_int64_t()
            || term_is_opcode(x) || term_is_matter(x) || term_is_entity_id(x)
            || is_heap(x, HeapInt64::SAVE_TYPE_TAG))
            return true;
        if (is_string(x)) {
            std::string_view v = term_as_string_view(x);
            return utf8::isvalid(v.data(), v.data() + v.size());
        }
        return false;
    }

    void Type<Term>::print(String& out, Term const& x) {
        if (term_is_null(x)) {
            out.append(StringView("null"));
        } else if (term_is_boolean(x)) {
            Type<bool>::print(out, term_as_boolean(x));
        } else if (x.is_int64_t()) {
            Type<int64_t>::print(out, x.as_int64_t());
        } else if (is_heap(x, HeapInt64::SAVE_TYPE_TAG)) {
            Type<int64_t>::print(out, static_cast<HeapInt64 const*>(_term_as_object(x))->as_int64_t());
        } else if (term_is_opcode(x)) {
            Type<OPCODE>::print(out, (OPCODE)term_as_opcode(x));
        } else if (term_is_matter(x)) {
            Type<MATTER>::print(out, (MATTER)term_as_matter(x));
        } else if (term_is_entity_id(x)) {
            Type<EntityID>::print(out, term_as_entity_id(x));
        } else if (printable(x)) {
            std::string_view v = term_as_string_view(x);
            print_quoted(out, StringView(trusted, v.data(), v.size()));
        } else {
            // Unreachable from the registry, which refuses unprintable
            // arguments; kept for diagnostics.  Not a valid literal.
            assert(false && "unprintable Term");
            out.append(StringView("?unprintable"));
        }
    }

    // ================================================================
    // Tests
    // ================================================================

    namespace {

        bool same_term(Term a, Term b) {
            return term_eq(a, b)._data == term_make_true()._data;
        }

        // Read `text` as a T as one argument of a whole command line.
        template<typename T>
        std::expected<T, Error> read_as(StringView text) {
            String line("probe ");
            line.append(text);
            auto parsed = parse_line(line);
            if (!parsed)
                return std::unexpected(std::move(parsed.error()));
            assert(parsed->arguments.size() == 1);
            return Type<T>::read(parsed->arguments[0].node);
        }

        struct TestContext {
            std::vector<String> log;
        };

    } // namespace

    define_test("command_game_types") {

        std::mt19937_64 rng(2026);

        // Coordinate: random and extreme, through its own context.
        for (int i = 0; i != 10000; ++i) {
            Coordinate c{(i32)rng(), (i32)rng()};
            if (i == 0) c = {std::numeric_limits<i32>::min(), std::numeric_limits<i32>::max()};
            auto r = read_as<Coordinate>(to_string(c));
            assert(r && *r == c);
        }
        assert(read_as<Coordinate>("Coordinate( 1 ,-2 )").value() == (Coordinate{1, -2}));
        for (const char* bad : {"Coordinate(1)", "Coordinate(1, 2, 3)", "Coordinate(0x80000000, 0)",
                                "Coordinate(1, x)", "Coord(1, 2)", "1", "\"Coordinate(1, 2)\""})
            assert(!read_as<Coordinate>(StringView(bad)));

        // EntityID.
        for (int i = 0; i != 10000; ++i) {
            EntityID e{rng() >> 4};
            auto r = read_as<EntityID>(to_string(e));
            assert(r && *r == e);
        }
        for (const char* bad : {"EntityID(-1)", "EntityID(0x1000000000000000)", "EntityID()", "7"})
            assert(!read_as<EntityID>(StringView(bad)));

        // Hash: full 64 bits, top bit included, round-trip in canonical form.
        for (int i = 0; i != 10000; ++i) {
            Hash h{rng()};
            auto r = read_as<Hash>(to_string(h));
            assert(r && *r == h);
        }
        assert(to_string(Hash{0x9e3779b97f4a7c15ull}) == StringView("Hash(0x9e3779b97f4a7c15)"));
        assert(read_as<Hash>("Hash(0xFFFFFFFFFFFFFFFF)").value() == (Hash{~(uint64_t)0}));
        assert(read_as<Hash>("Hash(0x0)").value() == (Hash{0}));
        for (const char* bad : {"Hash(7)", "Hash(0x)", "Hash(0x10000000000000000)", "Hash(-0x1)",
                                "Hash(0x1, 0x2)", "Hash()", "0x1", "Hash(\"0x1\")"})
            assert(!read_as<Hash>(StringView(bad)));

        // Enums: every enumerator, qualified (canonical) and bare
        // (contextual); wrong qualifiers and unknown names rejected.
        for (auto const& [value, spelling] : OPCODE_NAMES) {
            String canonical = to_string(value);
            assert(read_as<OPCODE>(canonical).value() == value);
            String bare(spelling + 7);
            assert(read_as<OPCODE>(bare).value() == value);
        }
        for (auto const& [value, spelling] : MATTER_NAMES)
            assert(read_as<MATTER>(to_string(value)).value() == value);
        assert(read_as<OPCODE>("OPCODE::SWAP").value() == OPCODE_SWAP);
        for (const char* bad : {"MATTER::SWAP", "OPCODE::", "OPCODE::NOPE", "NOPE",
                                "\"SWAP\"", "OPCODE(SWAP)", "OPCODE::SWAP::X"})
            assert(!read_as<OPCODE>(StringView(bad)));

        // Term: every printable kind round-trips through the canonical
        // form, as a Term.
        {
            std::vector<Term> terms = {
                term_make_null(), term_make_true(), term_make_false(),
                term_make_integer_with(0), term_make_integer_with(-1),
                term_make_integer_with(std::numeric_limits<int64_t>::max()),   // heap
                term_make_integer_with(std::numeric_limits<int64_t>::min()),   // heap
                term_make_integer_with((int64_t)1 << 59),                      // heap
                term_make_integer_with(((int64_t)1 << 59) - 1),                // small
                term_make_string_with(""), term_make_string_with("short"),
                term_make_string_with("a longer heap string"),
                term_make_string_with(std::string_view("nul\0inside", 10)),
                term_make_string_with("quote\" back\\ nl\n tab\t \x01 \xc3\xa9"),
                term_make_opcode(OPCODE_SWAP), term_make_opcode(OPCODE_NOOP),
                term_make_matter(MATTER_SHIPPING_CONTAINER),
                term_make_entity_id(EntityID{0}),
                term_make_entity_id(EntityID{((uint64_t)1 << 60) - 1}),
            };
            for (auto const& [value, spelling] : OPCODE_NAMES)
                terms.push_back(term_make_opcode((int)value));
            for (int i = 0; i != 2000; ++i)
                terms.push_back(term_make_integer_with((int64_t)rng()));
            for (Term t : terms) {
                assert(Type<Term>::printable(t));
                String printed = to_string(t);
                auto r = read_as<Term>(printed);
                assert(r);
                assert(same_term(*r, t));
                assert(to_string(*r) == printed);
            }
        }

        // Term: unprintable kinds are refused.
        for (Term t : {term_make_error(), term_make_character_with('x'), term_make_OK(),
                       term_make_string_with(std::string_view("\xff", 1))})
            assert(!Type<Term>::printable(t));

        // Term: self-describing only.  Bare enumerators and bare words are
        // errors; non-Term tags are errors.
        for (const char* bad : {"SWAP", "probe", "Coordinate(1, 2)", "OPCODE::NOPE",
                                "FOO::BAR", "-", "0x"})
            assert(!read_as<Term>(StringView(bad)));
        {
            auto e = read_as<Term>("SWAP");
            assert(!e);
            // The suggestion names the qualified form.
            String const& message = e.error().message;
            StringView m = message;
            bool mentions = false;
            for (auto it = m.chars.begin(); it + 12 <= m.chars.end(); ++it)
                if (std::memcmp(it, "OPCODE::SWAP", 12) == 0)
                    mentions = true;
            assert(mentions);
        }

        co_return;
    };

    define_test("command_game_registry") {

        using Ctx = TestContext;
        Registry<Ctx> registry;

        registry.add(make_command<Ctx>(
            "write", "Write a Term at a coordinate", Category::world,
            std::tuple{
                Param<Ctx, Coordinate>{"at", "where"},
                Param<Ctx, Term>{"term", "what"},
            },
            [](Ctx& c, Coordinate const& at, Term const& term) {
                String entry = to_string(at);
                entry.push_back(' ');
                entry.append(to_string(term));
                c.log.push_back(std::move(entry));
                return Reply::ok();
            }));

        registry.add(make_command<Ctx>(
            "hold", "Hold an opcode", Category::local,
            std::tuple{
                Param<Ctx, OPCODE>{"opcode", "what to hold"},
            },
            [](Ctx& c, OPCODE const& x) {
                c.log.push_back(to_string(x));
                return Reply::ok();
            }));

        TestContext context;

        {
            auto o = registry.run(context, StringView("write Coordinate(3,4) term=OPCODE::SWAP"));
            assert(o.reply.status == Reply::OK);
            assert(o.line == StringView("write Coordinate(3, 4) OPCODE::SWAP"));
        }
        {
            auto o = registry.run(context, StringView("write Coordinate(-1, 0) \"hello world\""));
            assert(o.line == StringView("write Coordinate(-1, 0) \"hello world\""));
        }
        {
            // Contextual: bare SWAP where an OPCODE is expected.
            auto o = registry.run(context, StringView("hold SWAP"));
            assert(o.reply.status == Reply::OK);
            assert(o.line == StringView("hold OPCODE::SWAP"));
        }
        {
            // Not contextual: a Term parameter needs the qualifier.
            auto o = registry.run(context, StringView("write Coordinate(0, 0) SWAP"));
            assert(o.reply.status == Reply::ERROR && o.line.empty());
        }
        assert(context.log.size() == 3);
        assert(context.log[0] == StringView("Coordinate(3, 4) OPCODE::SWAP"));
        assert(context.log[2] == StringView("OPCODE::SWAP"));

        co_return;
    };

} // namespace wry::command
