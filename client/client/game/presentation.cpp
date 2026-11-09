//
//  presentation.cpp
//  client
//
//  Created by Claude on 2026-11-10.
//

#include <cstdio>
#include <cstring>
#include <exception>
#include <optional>
#include <unistd.h>   // mkdtemp (test)

#include "json.hpp"
#include "platform.hpp"
#include "presentation.hpp"
#include "test.hpp"

namespace wry {

    namespace {

        void _warn(std::vector<String>* warnings,
                   std::initializer_list<StringView> parts) {
            if (!warnings)
                return;
            String s;
            for (StringView const& part : parts)
                s.append(part);
            warnings->push_back(std::move(s));
        }

        // "IRON_ORE" -> "Iron Ore": the compiled-in display name
        String _title_case(const char* bare) {
            String s;
            bool start = true;
            for (const char* p = bare; *p; ++p) {
                char c = *p;
                if (c == '_') {
                    s.push_back(' ');
                    start = true;
                    continue;
                }
                if (start && c >= 'a' && c <= 'z')
                    c = (char)(c - 'a' + 'A');
                else if (!start && c >= 'A' && c <= 'Z')
                    c = (char)(c - 'A' + 'a');
                s.push_back(c);
                start = false;
            }
            return s;
        }

        // "MATTER_IRON_ORE" -> "IRON_ORE"
        template<typename E, std::size_t N>
        std::array<const char*, N> _bare_names(std::pair<E, const char*> const (&names)[N],
                                               std::size_t prefix) {
            std::array<const char*, N> out;
            for (std::size_t i = 0; i != N; ++i) {
                assert((std::size_t)names[i].first == i);   // codes are dense from 0
                out[i] = names[i].second + prefix;
            }
            return out;
        }

        auto const& _terrain_names() {
            static std::array<const char*, TERRAIN_KIND_COUNT> names = [] {
                std::array<const char*, TERRAIN_KIND_COUNT> a;
                for (int i = 0; i != TERRAIN_KIND_COUNT; ++i)
                    a[(std::size_t)i] = TERRAIN_NAMES[i];
                return a;
            }();
            return names;
        }

        auto const& _matter_names() {
            static auto names = _bare_names(MATTER_NAMES, std::strlen("MATTER_"));
            return names;
        }

        auto const& _opcode_names() {
            static auto names = _bare_names(OPCODE_NAMES, std::strlen("OPCODE_"));
            return names;
        }

        int _hex_digit(char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        // "#rrggbb" or "#rrggbbaa"
        std::optional<Rgba8> _color_from_string(StringView v) {
            const char* p = v.chars.data();
            const std::size_t n = v.chars.size();
            if ((n != 7 && n != 9) || p[0] != '#')
                return std::nullopt;
            Rgba8 c = { 0, 0, 0, 255 };
            for (std::size_t i = 0; i != (n - 1) / 2; ++i) {
                int hi = _hex_digit(p[1 + 2 * i]);
                int lo = _hex_digit(p[2 + 2 * i]);
                if (hi < 0 || lo < 0)
                    return std::nullopt;
                c[i] = (uint8_t)(hi * 16 + lo);
            }
            return c;
        }

        // Read a whole text file; nullopt on any failure (a missing file
        // is an expected condition).  Validates UTF-8 via String.
        std::optional<String> _text_from_file(std::filesystem::path const& path) {
            FILE* f = std::fopen(path.c_str(), "rb");
            if (!f)
                return std::nullopt;
            std::fseek(f, 0, SEEK_END);
            long n = std::ftell(f);
            if (n < 0) {
                std::fclose(f);
                return std::nullopt;
            }
            std::fseek(f, 0, SEEK_SET);
            std::vector<char> buffer((std::size_t)n);
            std::size_t m = std::fread(buffer.data(), 1, (std::size_t)n, f);
            std::fclose(f);
            if (m != (std::size_t)n)
                return std::nullopt;
            try {
                return String(buffer.data(), m);
            } catch (...) {
                return std::nullopt;
            }
        }

        using json::Json;

        // Warn about any field of `record` not in `allowed` ("_comment"
        // is always allowed)
        void _check_fields(Json const& record, std::initializer_list<const char*> allowed,
                           StringView file, StringView key, std::vector<String>* warnings) {
            for (auto const& [field, value] : record.as_object()) {
                if (field == StringView("_comment"))
                    continue;
                bool known = false;
                for (const char* a : allowed)
                    known = known || (field == StringView(a));
                if (!known)
                    _warn(warnings, {file, ": ", key, ": unknown field '", field, "' ignored"});
            }
        }

        // A string field, if present and a string; warns if present and
        // not a string
        void _string_field(Json const& record, const char* field, String& out,
                           StringView file, StringView key, std::vector<String>* warnings) {
            Table<String, Json> const& table = record.as_object();
            if (!table.contains(StringView(field)))
                return;
            Json const& v = table.at(StringView(field));
            if (!v.is_string()) {
                _warn(warnings, {file, ": ", key, ": '", field, "' is not a string; ignored"});
                return;
            }
            out = String(v.as_string());
        }

        void _color_field(Json const& record, Rgba8& out,
                          StringView file, StringView key, std::vector<String>* warnings) {
            Table<String, Json> const& table = record.as_object();
            if (!table.contains(StringView("color_srgb")))
                return;
            Json const& v = table.at(StringView("color_srgb"));
            std::optional<Rgba8> c;
            if (v.is_string())
                c = _color_from_string(v.as_string());
            if (!c) {
                _warn(warnings, {file, ": ", key,
                                 ": 'color_srgb' is not \"#rrggbb\" or \"#rrggbbaa\"; "
                                 "keeping the compiled-in colour"});
                return;
            }
            out = *c;
        }

        // The walk every file shares: parse, require an object root,
        // warn once about unknown keys and once about missing ones, and
        // hand each present, object-valued record to apply(i, record, key).
        // A malformed document warns and applies nothing.
        template<std::size_t N, typename F>
        void _overlay(StringView text, std::array<const char*, N> const& names,
                      StringView file, std::vector<String>* warnings, F&& apply) {
            Json document;
            try {
                document = Json::from(StringView{text});
            } catch (std::exception const& e) {
                _warn(warnings, {file, ": does not parse (", StringView(e.what()),
                                 "); using the compiled-in values"});
                return;
            }
            if (!document.is_object()) {
                _warn(warnings, {file, ": the root is not an object; "
                                 "using the compiled-in values"});
                return;
            }
            Table<String, Json> const& root = document.as_object();

            String unknown;
            for (auto const& [key, value] : root) {
                if (key == StringView("_comment"))
                    continue;
                bool known = false;
                for (const char* name : names)
                    known = known || (key == StringView(name));
                if (!known) {
                    if (!unknown.empty())
                        unknown.append(StringView(", "));
                    unknown.append(key);
                }
            }
            if (!unknown.empty())
                _warn(warnings, {file, ": unknown keys ignored: ", unknown});

            String missing;
            for (std::size_t i = 0; i != N; ++i) {
                StringView key(names[i]);
                if (!root.contains(key)) {
                    if (!missing.empty())
                        missing.append(StringView(", "));
                    missing.append(key);
                    continue;
                }
                Json const& record = root.at(key);
                if (!record.is_object()) {
                    _warn(warnings, {file, ": ", key, " is not an object; ignored"});
                    continue;
                }
                apply(i, record, key);
            }
            if (!missing.empty())
                _warn(warnings, {file, ": missing keys (compiled-in values used): ", missing});
        }

        Presentation& _instance() {
            static Presentation p = default_presentation();
            return p;
        }

    } // anonymous namespace

    Presentation default_presentation() {
        Presentation p;
        for (int i = 0; i != TERRAIN_KIND_COUNT; ++i) {
            KindPresentation& k = p.terrain[(std::size_t)i];
            k.name = _title_case(TERRAIN_NAMES[i]);
            std::memcpy(k.color.data(), TERRAIN_COLOR_SRGB[i], 4);
        }
        for (std::size_t i = 0; i != MATTER_KIND_COUNT; ++i) {
            KindPresentation& k = p.matter[i];
            k.name = _title_case(_matter_names()[i]);
            std::memcpy(k.color.data(), MATTER_COLOR_SRGB[i], 4);
        }
        return p;
    }

    void terrain_presentation_from_json(Presentation& p, StringView text,
                                        std::vector<String>* warnings, StringView file) {
        _overlay(text, _terrain_names(), file, warnings,
                 [&](std::size_t i, Json const& record, StringView key) {
            _check_fields(record, {"name", "color_srgb"}, file, key, warnings);
            _string_field(record, "name", p.terrain[i].name, file, key, warnings);
            _color_field(record, p.terrain[i].color, file, key, warnings);
        });
    }

    void matter_presentation_from_json(Presentation& p, StringView text,
                                       std::vector<String>* warnings, StringView file) {
        _overlay(text, _matter_names(), file, warnings,
                 [&](std::size_t i, Json const& record, StringView key) {
            // price_compiled_in is a read-only reference to MATTER_PRICE
            // (a simulation rule): known, and deliberately not read
            _check_fields(record, {"name", "color_srgb", "price_compiled_in"}, file, key, warnings);
            _string_field(record, "name", p.matter[i].name, file, key, warnings);
            _color_field(record, p.matter[i].color, file, key, warnings);
        });
    }

    void opcode_presentation_from_json(Presentation& p, StringView text,
                                       std::vector<String>* warnings, StringView file) {
        _overlay(text, _opcode_names(), file, warnings,
                 [&](std::size_t i, Json const& record, StringView key) {
            _check_fields(record, {"name", "summary", "help", "stack"}, file, key, warnings);
            OpcodeHelp& h = p.opcode[i];
            _string_field(record, "name", h.name, file, key, warnings);
            _string_field(record, "summary", h.summary, file, key, warnings);
            _string_field(record, "help", h.help, file, key, warnings);
            _string_field(record, "stack", h.stack, file, key, warnings);
            if (h.name.empty())
                _warn(warnings, {file, ": ", key, ": no 'name'; the entry is ignored"});
        });
    }

    std::filesystem::path presentation_directory() {
        std::error_code ec;
        std::filesystem::path bundle = bundle_resource_directory();
        if (!bundle.empty()) {
            std::filesystem::path data = bundle / "data";
            if (std::filesystem::is_directory(data, ec))
                return data;
        }
        // <repo>/client/client/game/presentation.cpp -> <repo>/assets
        std::filesystem::path source(__FILE__);
        if (source.is_absolute()) {
            std::filesystem::path assets =
                source.parent_path().parent_path().parent_path().parent_path() / "assets";
            if (std::filesystem::is_directory(assets, ec))
                return assets;
        }
        return {};
    }

    Presentation load_presentation(std::filesystem::path const& directory,
                                   std::vector<String>* warnings) {
        Presentation p = default_presentation();
        struct File {
            const char* name;
            void (*overlay)(Presentation&, StringView, std::vector<String>*, StringView);
        };
        const File files[] = {
            { "terrain.json", &terrain_presentation_from_json },
            { "matter.json",  &matter_presentation_from_json },
            { "opcodes.json", &opcode_presentation_from_json },
        };
        for (File const& f : files) {
            std::optional<String> text;
            if (!directory.empty())
                text = _text_from_file(directory / f.name);
            if (!text) {
                String where(StringView(directory.c_str()));
                _warn(warnings, {StringView(f.name), ": not found in '", where,
                                 "'; using the compiled-in values"});
                continue;
            }
            f.overlay(p, *text, warnings, StringView(f.name));
        }
        return p;
    }

    Presentation const& presentation() {
        return _instance();
    }

    void presentation_install(Presentation p) {
        _instance() = std::move(p);
    }

    // ----------------------------------------------------------------
    // Tests.

    namespace {

        bool _same(Presentation const& a, Presentation const& b) {
            for (std::size_t i = 0; i != a.terrain.size(); ++i)
                if (!(a.terrain[i].name == b.terrain[i].name) || a.terrain[i].color != b.terrain[i].color)
                    return false;
            for (std::size_t i = 0; i != a.matter.size(); ++i)
                if (!(a.matter[i].name == b.matter[i].name) || a.matter[i].color != b.matter[i].color)
                    return false;
            for (std::size_t i = 0; i != a.opcode.size(); ++i)
                if (!(a.opcode[i].name == b.opcode[i].name)
                    || !(a.opcode[i].summary == b.opcode[i].summary)
                    || !(a.opcode[i].help == b.opcode[i].help)
                    || !(a.opcode[i].stack == b.opcode[i].stack))
                    return false;
            return true;
        }

        bool _mentions(std::vector<String> const& warnings, const char* needle) {
            for (String const& w : warnings)
                if (std::string_view(w.chars.data(), w.chars.size()).find(needle) != std::string_view::npos)
                    return true;
            return false;
        }

    } // anonymous namespace

    // The compiled-in fallback is exactly the colour tables, with
    // title-cased names, and no opcode help
    define_test("presentation_defaults") {
        Presentation p = default_presentation();
        for (int t = 0; t != TERRAIN_KIND_COUNT; ++t)
            require(std::memcmp(p.terrain_color(t).data(), TERRAIN_COLOR_SRGB[t], 4) == 0);
        for (std::size_t m = 0; m != MATTER_KIND_COUNT; ++m)
            require(std::memcmp(p.matter_color((int64_t)m).data(), MATTER_COLOR_SRGB[m], 4) == 0);
        require(p.terrain[TERRAIN_IRON_ORE].name == StringView("Iron Ore"));
        require(p.matter[MATTER_SHIPPING_CONTAINER].name == StringView("Shipping Container"));
        require(p.terrain_color(-1) == p.terrain_color(TERRAIN_ROCK));
        require(p.terrain_color(TERRAIN_KIND_COUNT) == p.terrain_color(TERRAIN_ROCK));
        require(p.matter_color(-1) == p.matter_color(0));
        for (std::size_t i = 0; i != OPCODE_COUNT; ++i)
            require(p.opcode_help((int64_t)i) == nullptr);
        require(p.opcode_help(-1) == nullptr);
        require(p.opcode_help((int64_t)OPCODE_COUNT) == nullptr);
        co_return;
    };

    // Overlay: a well-formed record replaces exactly its fields; absent
    // keys keep the compiled-in values with one warning; unknown keys,
    // unknown fields and malformed fields warn and are skipped; a
    // malformed document changes nothing
    define_test("presentation_overlay") {
        Presentation const d = default_presentation();
        {
            Presentation p = d;
            std::vector<String> w;
            terrain_presentation_from_json(p, StringView(
                "{ \"_comment\": \"x\","
                "  \"WATER\": { \"name\": \"Sea\", \"color_srgb\": \"#010203\" },"
                "  \"SAND\":  { \"color_srgb\": \"#0a0B0c80\", \"_comment\": \"y\" },"
                "  \"GRASS\": { \"name\": \"Lawn\" },"
                "  \"ROCK\": {}, \"COAL\": {}, \"IRON_ORE\": {}, \"LIMESTONE\": {} }"),
                &w);
            require(w.empty());
            require(p.terrain[TERRAIN_WATER].name == StringView("Sea"));
            require(p.terrain[TERRAIN_WATER].color == (Rgba8{1, 2, 3, 255}));
            require(p.terrain[TERRAIN_SAND].name == d.terrain[TERRAIN_SAND].name);
            require(p.terrain[TERRAIN_SAND].color == (Rgba8{10, 11, 12, 128}));
            require(p.terrain[TERRAIN_GRASS].name == StringView("Lawn"));
            require(p.terrain[TERRAIN_GRASS].color == d.terrain[TERRAIN_GRASS].color);
            require(p.terrain[TERRAIN_ROCK].color == d.terrain[TERRAIN_ROCK].color);
        }
        {
            // Missing keys: one warning naming them all; values kept
            Presentation p = d;
            std::vector<String> w;
            terrain_presentation_from_json(p, StringView(
                "{ \"WATER\": { \"color_srgb\": \"#000000\" } }"), &w);
            require(w.size() == 1);
            require(_mentions(w, "missing keys"));
            require(_mentions(w, "LIMESTONE"));
            require(p.terrain[TERRAIN_WATER].color == (Rgba8{0, 0, 0, 255}));
            require(p.terrain[TERRAIN_SAND].color == d.terrain[TERRAIN_SAND].color);
        }
        {
            // Unknown keys (one warning), an unknown field, a bad colour,
            // a non-string name, a non-object record
            Presentation p = d;
            std::vector<String> w;
            matter_presentation_from_json(p, StringView(
                "{ \"GOLD\": {}, \"MATTER_COAL\": {},"
                "  \"SHIPPING_CONTAINER\": {}, \"WATER\": { \"colour\": \"#ffffff\" },"
                "  \"SAND\": { \"color_srgb\": \"#fff\" }, \"GRASS\": { \"name\": 7 },"
                "  \"ROCK\": 3, \"COAL\": { \"price_compiled_in\": 1000 },"
                "  \"IRON_ORE\": {}, \"LIMESTONE\": {} }"), &w);
            require(w.size() == 5);
            require(_mentions(w, "unknown keys"));
            require(_mentions(w, "GOLD") && _mentions(w, "MATTER_COAL"));
            require(_mentions(w, "unknown field 'colour'"));
            require(_mentions(w, "color_srgb"));
            require(_mentions(w, "'name' is not a string"));
            require(_mentions(w, "ROCK is not an object"));
            require(_same(p, d));   // nothing applied; the price is not read
        }
        {
            // Malformed documents: one warning, nothing applied
            for (const char* text : { "{ \"WATER\": ", "[]", "{} trailing", "" }) {
                Presentation p = d;
                std::vector<String> w;
                terrain_presentation_from_json(p, StringView(text), &w);
                require(w.size() == 1);
                require(_same(p, d));
            }
        }
        {
            // Opcodes: an entry needs a name
            Presentation p = d;
            std::vector<String> w;
            opcode_presentation_from_json(p, StringView(
                "{ \"ADD\": { \"name\": \"Add\", \"summary\": \"s\", \"help\": \"h\", \"stack\": \"a b -- c\" },"
                "  \"SWAP\": { \"summary\": \"orphan\" } }"), &w);
            require(_mentions(w, "missing keys"));
            require(_mentions(w, "SWAP: no 'name'"));
            OpcodeHelp const* h = p.opcode_help(OPCODE_ADD);
            require(h && h->name == StringView("Add") && h->stack == StringView("a b -- c"));
            require(p.opcode_help(OPCODE_SWAP) == nullptr);
            require(p.opcode_help(OPCODE_NOOP) == nullptr);
        }
        co_return;
    };

    // Files: an empty directory (or none) is three warnings and the
    // compiled-in values; files written there round-trip through
    // load_presentation
    define_test("presentation_load_files") {
        Presentation const d = default_presentation();
        {
            std::vector<String> w;
            Presentation p = load_presentation(std::filesystem::path(), &w);
            require(w.size() == 3);
            require(_same(p, d));
        }
        char pattern[] = "/tmp/wry-presentation-test-XXXXXX";
        char* dir = mkdtemp(pattern);
        require(dir);
        std::filesystem::path directory(dir);
        {
            std::vector<String> w;
            Presentation p = load_presentation(directory, &w);
            require(w.size() == 3);
            require(_mentions(w, "terrain.json: not found"));
            require(_same(p, d));
        }
        {
            FILE* f = std::fopen((directory / "terrain.json").c_str(), "wb");
            require(f);
            const char text[] = "{ \"COAL\": { \"name\": \"Coal Seam\", \"color_srgb\": \"#102030\" } }";
            require(std::fwrite(text, 1, sizeof(text) - 1, f) == sizeof(text) - 1);
            require(std::fclose(f) == 0);
            std::vector<String> w;
            Presentation p = load_presentation(directory, &w);
            require(w.size() == 3);   // terrain's missing keys; matter, opcodes absent
            require(_mentions(w, "terrain.json: missing keys"));
            require(_mentions(w, "matter.json: not found"));
            require(p.terrain[TERRAIN_COAL].name == StringView("Coal Seam"));
            require(p.terrain_color(TERRAIN_COAL) == (Rgba8{16, 32, 48, 255}));
            require(p.terrain_color(TERRAIN_SAND) == d.terrain_color(TERRAIN_SAND));
        }
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
        co_return;
    };

    // The shipped files (assets/*.json, as the build copies them): they
    // load without a warning, so every terrain kind, matter kind and
    // opcode has an entry and every entry names a real one; each opcode
    // entry is complete; and matter.json's read-only price_compiled_in
    // agrees with the compiled-in MATTER_PRICE it documents
    define_test("presentation_shipped_files") {
        std::filesystem::path directory = presentation_directory();
        require(!directory.empty());
        std::vector<String> w;
        Presentation p = load_presentation(directory, &w);
        for (String const& s : w)
            std::fprintf(stderr, "presentation_shipped_files: %.*s\n",
                         (int)s.chars.size(), s.chars.data());
        require(w.empty());

        for (auto const& [op, spelling] : OPCODE_NAMES) {
            OpcodeHelp const* h = p.opcode_help(op);
            require(h);
            require(!h->name.empty() && !h->summary.empty() && !h->help.empty());
        }

        // Every key of opcodes.json names a real opcode (independently of
        // the loader's unknown-key warning)
        std::optional<String> text = _text_from_file(directory / "opcodes.json");
        require(text);
        Json document = Json::from(StringView(*text));
        require(document.is_object());
        std::size_t entries = 0;
        for (auto const& [key, value] : document.as_object()) {
            if (key == StringView("_comment"))
                continue;
            bool real = false;
            for (const char* name : _opcode_names())
                real = real || (key == StringView(name));
            require(real);
            ++entries;
        }
        require(entries == OPCODE_COUNT);

        text = _text_from_file(directory / "matter.json");
        require(text);
        document = Json::from(StringView(*text));
        for (std::size_t m = 0; m != MATTER_KIND_COUNT; ++m) {
            Json const& record = document[StringView(_matter_names()[m])];
            require(record.as_object().contains(StringView("price_compiled_in")));
            require(record[StringView("price_compiled_in")].as_number() == (double)matter_price((int)m));
        }
        co_return;
    };

} // namespace wry
