//
//  settings.cpp
//  client
//
//  Created by Antony Searle on 2026-08-11.
//

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "filesystem.hpp"
#include "json.hpp"
#include "settings.hpp"
#include "test.hpp"

namespace wry {

    Settings default_settings() {
        Settings s;
        s.keymap = gui::default_keymap();
        return s;
    }

    // ----------------------------------------------------------------
    // Emission.
    //
    // A dedicated ordered emitter rather than the Json DOM: the file
    // wants actions in declaration order, and the DOM's object Table
    // iterates in hash order.  (The DOM handles the read side, where
    // order is irrelevant.)

    namespace {

        void _append_json_string(String& s, StringView v) {
            s.push_back('"');
            for (char32_t c : v) {
                switch (c) {
                    case '"':  s.append("\\\""); break;
                    case '\\': s.append("\\\\"); break;
                    case '\n': s.append("\\n");  break;
                    case '\t': s.append("\\t");  break;
                    case '\r': s.append("\\r");  break;
                    default:
                        if (c < 0x20) {
                            char buffer[8];
                            std::snprintf(buffer, sizeof buffer,
                                          "\\u%04x", (unsigned)c);
                            s.append(buffer);
                        } else {
                            s.push_back(c);
                        }
                        break;
                }
            }
            s.push_back('"');
        }

        void _warn(std::vector<String>* warnings,
                   std::initializer_list<StringView> parts) {
            if (!warnings)
                return;
            String s;
            for (StringView const& part : parts)
                s.append(part);
            warnings->push_back(std::move(s));
        }

    } // anonymous namespace

    String json_from_settings(Settings const& settings) {
        String s;
        s.append("{\n");
        s.append("    \"version\": 1,\n");
        s.append("    \"key_bindings\": {\n");
        for (size_t i = 0; i != gui::action_count; ++i) {
            gui::Action a = (gui::Action)i;
            s.append("        ");
            _append_json_string(s, gui::id_from_action(a));
            s.append(": [");
            std::vector<gui::KeyCombo> combos =
                settings.keymap.combos_from_action(a);
            for (size_t j = 0; j != combos.size(); ++j) {
                if (j)
                    s.append(", ");
                _append_json_string(s, gui::string_from_combo(combos[j]));
            }
            s.append((i + 1 != gui::action_count) ? "],\n" : "]\n");
        }
        s.append("    }\n");
        s.append("}\n");
        return s;
    }

    // ----------------------------------------------------------------
    // Parsing.

    Settings settings_from_json(StringView text,
                                std::vector<String>* warnings) {
        using json::Json;

        Settings settings = default_settings();

        Json document = Json::from(StringView{text});
        if (!document.is_object())
            throw json::JsonParseError("settings root is not an object");
        Table<String, Json> const& root = document.as_object();

        if (root.contains(StringView("version"))) {
            Json const& v = document[StringView("version")];
            if (!v.is_number() || v.as_number() != 1.0)
                _warn(warnings,
                      {"settings.json: unrecognized version; "
                       "loading best-effort"});
        }

        // Unknown top-level keys are ignored silently: a file written
        // by a newer binary may carry setting groups this one doesn't
        // know, and that must not warn-spam.  Unknown entries *inside*
        // key_bindings warn, though: there the vocabulary is ours.

        if (!root.contains(StringView("key_bindings")))
            return settings;
        Json const& kb = document[StringView("key_bindings")];
        if (!kb.is_object()) {
            _warn(warnings,
                  {"settings.json: 'key_bindings' is not an object; "
                   "using default bindings"});
            return settings;
        }
        Table<String, Json> const& table = kb.as_object();

        for (auto const& [id, value] : table) {
            if (!gui::action_from_id(id))
                _warn(warnings, {"settings.json: unknown action '", id,
                                 "' ignored"});
        }

        // Apply in action declaration order (the order of
        // settings-default.json), not file order: the DOM's Table
        // iterates in hash order, so this is the only deterministic
        // choice, and it makes conflict resolution reproducible.
        for (size_t i = 0; i != gui::action_count; ++i) {
            gui::Action a = (gui::Action)i;
            StringView id = gui::id_from_action(a);
            if (!table.contains(id))
                continue;   // absent: keep the compiled-in default
            Json const& entry = table.at(id);
            if (!entry.is_array()) {
                _warn(warnings, {"settings.json: '", id,
                                 "' should be an array of key combos; "
                                 "keeping its default"});
                continue;
            }
            // Present-and-well-formed replaces the defaults, including
            // the explicit-unbind case of [].
            settings.keymap.clear_action(a);
            for (size_t j = 0; j != entry.size(); ++j) {
                Json const& element = entry[j];
                if (!element.is_string()) {
                    _warn(warnings, {"settings.json: non-string key combo "
                                     "for '", id, "' ignored"});
                    continue;
                }
                StringView combo_text = element.as_string();
                std::optional<gui::KeyCombo> combo =
                    gui::combo_from_string(combo_text);
                if (!combo) {
                    _warn(warnings, {"settings.json: unrecognized key combo "
                                     "'", combo_text, "' for '", id,
                                     "' ignored"});
                    continue;
                }
                std::optional<gui::Action> previous =
                    settings.keymap.action_from_combo(*combo);
                if (previous && *previous != a)
                    _warn(warnings, {"settings.json: '", combo_text,
                                     "' moved from '",
                                     gui::id_from_action(*previous),
                                     "' to '", id, "'"});
                settings.keymap.bind(*combo, a);
            }
        }

        return settings;
    }

    // ----------------------------------------------------------------
    // Files.

    SettingsPaths settings_paths(std::filesystem::path directory) {
        SettingsPaths p;
        p.settings = directory / "settings.json";
        p.defaults = directory / "settings-default.json";
        p.directory = std::move(directory);
        return p;
    }

    namespace {

        // Read a whole text file; nullopt (not a crash) on any failure,
        // since a missing or unreadable settings file is an expected
        // condition.  Validates UTF-8 via the String constructor.
        std::optional<String> _text_from_file(
                std::filesystem::path const& path) {
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
            std::vector<char> buffer((size_t)n);
            size_t m = std::fread(buffer.data(), 1, (size_t)n, f);
            std::fclose(f);
            if (m != (size_t)n)
                return std::nullopt;
            try {
                return String(buffer.data(), m);   // validates UTF-8
            } catch (...) {
                return std::nullopt;
            }
        }

        // Write text to `target` via mkstemp + rename in the same
        // directory (the save-file idiom), so a failure mid-write can
        // never leave a truncated target.
        bool _text_to_file_atomic(std::filesystem::path const& target,
                                  StringView text) {
            std::filesystem::path pattern =
                target.parent_path() / ".settingstmp.XXXXXX";
            std::string pattern_chars = pattern.string();
            std::vector<char> name(pattern_chars.begin(),
                                   pattern_chars.end());
            name.push_back('\0');
            int fd = mkstemp(name.data());
            if (fd < 0)
                return false;
            char const* p = text.chars.data();
            size_t remaining = text.chars.size();
            bool ok = true;
            while (remaining) {
                ssize_t written = write(fd, p, remaining);
                if (written <= 0) {
                    ok = false;
                    break;
                }
                p += written;
                remaining -= (size_t)written;
            }
            if (close(fd) != 0)
                ok = false;
            std::error_code ec;
            if (ok) {
                std::filesystem::rename(
                    std::filesystem::path(name.data()), target, ec);
                ok = !ec;
            }
            if (!ok)
                std::filesystem::remove(
                    std::filesystem::path(name.data()), ec);
            return ok;
        }

    } // anonymous namespace

    bool save_settings(Settings const& settings, SettingsPaths const& paths) {
        std::error_code ec;
        std::filesystem::create_directories(paths.directory, ec);
        return _text_to_file_atomic(paths.settings,
                                    json_from_settings(settings));
    }

    Settings load_settings(SettingsPaths const& paths,
                           std::vector<String>* warnings) {
        std::error_code ec;
        std::filesystem::create_directories(paths.directory, ec);

        // Refresh the machine-written defaults image so it always
        // documents this binary's defaults (a new binary may have new
        // actions).
        Settings defaults = default_settings();
        if (!_text_to_file_atomic(paths.defaults,
                                  json_from_settings(defaults)))
            _warn(warnings, {"settings: could not write "
                             "settings-default.json"});

        // First run (or a deleted file): install settings.json as a
        // copy of the defaults file.
        if (!std::filesystem::exists(paths.settings, ec)) {
            std::filesystem::copy_file(paths.defaults, paths.settings, ec);
            if (ec) {
                _warn(warnings, {"settings: could not create settings.json; "
                                 "running on defaults"});
                return defaults;
            }
        }

        std::optional<String> text = _text_from_file(paths.settings);
        if (!text) {
            _warn(warnings, {"settings: could not read settings.json; "
                             "running on defaults"});
            return defaults;
        }
        try {
            return settings_from_json(*text, warnings);
        } catch (std::exception const& e) {
            String message;
            message.append("settings: settings.json did not parse (");
            message.append(e.what());
            message.append("); running on defaults; file left in place "
                           "for repair");
            if (warnings)
                warnings->push_back(std::move(message));
            return defaults;
        }
    }

    Settings reset_settings(SettingsPaths const& paths,
                            std::vector<String>* warnings) {
        std::error_code ec;
        std::filesystem::create_directories(paths.directory, ec);

        if (std::filesystem::exists(paths.settings, ec)) {
            // settings-backup-N.json, N = max existing + 1 (the save-id
            // idiom).
            int n = 0;
            for (auto const& entry :
                     std::filesystem::directory_iterator(paths.directory,
                                                         ec)) {
                int k = 0;
                if (std::sscanf(entry.path().filename().c_str(),
                                "settings-backup-%d.json", &k) == 1)
                    n = std::max(n, k);
            }
            char name[64];
            std::snprintf(name, sizeof name,
                          "settings-backup-%d.json", n + 1);
            std::filesystem::path backup = paths.directory / name;
            std::filesystem::rename(paths.settings, backup, ec);
            if (ec) {
                // Never destroy a settings.json we could not back up.
                _warn(warnings, {"settings: could not back up "
                                 "settings.json; reset aborted"});
                return load_settings(paths, warnings);
            }
        }

        // load_settings finds no settings.json, rewrites the defaults
        // image, and installs the copy -- exactly the reset semantics.
        return load_settings(paths, warnings);
    }

    // ----------------------------------------------------------------
    // Tests.

    define_test("settings_json_roundtrip")
    {
        using namespace gui;

        Settings a = default_settings();
        // Perturb: a second binding for rotate, an unbound action.
        a.keymap.bind(*combo_from_string(StringView("F5")),
                      Action::rotate);
        a.keymap.clear_action(Action::toggle_points);

        String text = json_from_settings(a);
        std::vector<String> warnings;
        Settings b = settings_from_json(text, &warnings);
        assert(warnings.empty());

        assert(a.keymap.bindings().size() == b.keymap.bindings().size());
        for (auto const& binding : a.keymap.bindings())
            assert(b.keymap.action_from_combo(binding.combo) ==
                   binding.action);
        assert(b.keymap.combos_from_action(Action::toggle_points).empty());
        assert(b.keymap.combos_from_action(Action::rotate).size() == 2);

        // The emitted text is well-formed JSON with the expected shape.
        json::Json document = json::Json::from(StringView(text));
        assert(document.is_object());
        assert(document[StringView("version")].as_number() == 1.0);
        json::Json const& kb = document[StringView("key_bindings")];
        assert(kb.is_object());
        assert(kb.size() == action_count);   // every action listed

        co_return;
    };

    define_test("settings_json_semantics")
    {
        using namespace gui;

        KeyCombo r = *combo_from_string(StringView("R"));
        KeyCombo tab = *combo_from_string(StringView("Tab"));

        // Absent action: compiled-in default applies.
        {
            std::vector<String> w;
            Settings s = settings_from_json(
                StringView("{\"version\": 1, \"key_bindings\": {}}"), &w);
            assert(w.empty());
            assert(s.keymap.action_from_combo(r) == Action::rotate);
        }

        // Explicit []: unbound, distinct from absent.
        {
            std::vector<String> w;
            Settings s = settings_from_json(
                StringView("{\"key_bindings\": {\"rotate\": []}}"), &w);
            assert(w.empty());
            assert(!s.keymap.action_from_combo(r));
            assert(s.keymap.combos_from_action(Action::rotate).empty());
            // Everything else still at defaults.
            assert(s.keymap.action_from_combo(tab) == Action::toggle_map);
        }

        // Unknown action id: warned, ignored.
        {
            std::vector<String> w;
            Settings s = settings_from_json(
                StringView("{\"key_bindings\": {\"frobnicate\": [\"Q\"]}}"),
                &w);
            assert(w.size() == 1);
            assert(s.keymap.action_from_combo(r) == Action::rotate);
        }

        // Unparseable combo: warned, the rest of the entry applies.
        {
            std::vector<String> w;
            Settings s = settings_from_json(
                StringView("{\"key_bindings\": "
                           "{\"rotate\": [\"NotAKey\", \"T\"]}}"), &w);
            assert(w.size() == 1);
            std::vector<KeyCombo> combos =
                s.keymap.combos_from_action(Action::rotate);
            assert(combos.size() == 1);
            assert(combos[0] == *combo_from_string(StringView("T")));
        }

        // Reserved key: warned, refused (and the entry still replaced
        // the default, leaving the action unbound).
        {
            std::vector<String> w;
            Settings s = settings_from_json(
                StringView("{\"key_bindings\": {\"rotate\": [\"Escape\"]}}"),
                &w);
            assert(w.size() == 1);
            assert(s.keymap.combos_from_action(Action::rotate).empty());
        }

        // A combo claimed away from a defaulted action: stolen, warned.
        {
            std::vector<String> w;
            Settings s = settings_from_json(
                StringView("{\"key_bindings\": {\"toggle-map\": [\"R\"]}}"),
                &w);
            assert(w.size() == 1);
            assert(s.keymap.action_from_combo(r) == Action::toggle_map);
            assert(s.keymap.combos_from_action(Action::rotate).empty());
            // toggle-map's default Tab was replaced, not appended to.
            assert(!s.keymap.action_from_combo(tab));
        }

        // Wrong entry type: warned, default kept.
        {
            std::vector<String> w;
            Settings s = settings_from_json(
                StringView("{\"key_bindings\": {\"rotate\": \"R\"}}"), &w);
            assert(w.size() == 1);
            assert(s.keymap.action_from_combo(r) == Action::rotate);
        }

        // Unknown version: warned, still loads.
        {
            std::vector<String> w;
            Settings s = settings_from_json(
                StringView("{\"version\": 2, \"key_bindings\": "
                           "{\"rotate\": [\"T\"]}}"), &w);
            assert(w.size() == 1);
            assert(s.keymap.combos_from_action(Action::rotate).size() == 1);
        }

        // Unknown top-level groups from a future binary: silently fine.
        {
            std::vector<String> w;
            Settings s = settings_from_json(
                StringView("{\"version\": 1, \"audio\": {\"volume\": 0.5}, "
                           "\"key_bindings\": {}}"), &w);
            assert(w.empty());
            assert(s.keymap.action_from_combo(r) == Action::rotate);
        }

        // Document-level failures throw.
        {
            bool threw = false;
            try {
                settings_from_json(StringView("[1, 2, 3]"), nullptr);
            } catch (json::JsonParseError const&) {
                threw = true;
            }
            assert(threw);
        }
        {
            bool threw = false;
            try {
                settings_from_json(StringView("{ garbage"), nullptr);
            } catch (json::JsonParseError const&) {
                threw = true;
            }
            assert(threw);
        }

        co_return;
    };

    define_test("settings_file_lifecycle")
    {
        using namespace gui;

        // A unique scratch directory: tests run concurrently, and the
        // real "config" directory belongs to the app.
        char pattern[] = "settings-test-XXXXXX";
        char* made = mkdtemp(pattern);
        assert(made);
        SettingsPaths paths = settings_paths(made);

        // First load: creates the directory contents -- a defaults
        // image plus settings.json copied from it -- and returns the
        // defaults.
        {
            std::vector<String> w;
            Settings s = load_settings(paths, &w);
            assert(w.empty());
            assert(std::filesystem::exists(paths.settings));
            assert(std::filesystem::exists(paths.defaults));
            assert(s.keymap.action_from_combo(
                *combo_from_string(StringView("R"))) == Action::rotate);
            // The two files are byte-identical (settings is a copy).
            String a = string_from_file(paths.settings);
            String b = string_from_file(paths.defaults);
            assert(StringView(a) == StringView(b));
        }

        // Rebind, save, reload.
        {
            std::vector<String> w;
            Settings s = load_settings(paths, &w);
            s.keymap.bind(*combo_from_string(StringView("F5")),
                          Action::rotate);
            assert(save_settings(s, paths));
            Settings t = load_settings(paths, &w);
            assert(w.empty());
            assert(t.keymap.action_from_combo(
                *combo_from_string(StringView("F5"))) == Action::rotate);
        }

        // Reset: the modified file lands in settings-backup-1.json and
        // settings.json returns to the defaults.
        {
            std::vector<String> w;
            Settings s = reset_settings(paths, &w);
            assert(w.empty());
            assert(s.keymap.combos_from_action(Action::rotate).size() == 1);
            std::filesystem::path backup =
                paths.directory / "settings-backup-1.json";
            assert(std::filesystem::exists(backup));
            String text = string_from_file(backup);
            Settings b = settings_from_json(text, nullptr);
            assert(b.keymap.action_from_combo(
                *combo_from_string(StringView("F5"))) == Action::rotate);
        }

        // Second reset: backup numbering advances.
        {
            std::vector<String> w;
            reset_settings(paths, &w);
            assert(std::filesystem::exists(
                paths.directory / "settings-backup-2.json"));
        }

        // A corrupt settings.json: load warns, runs on defaults, and
        // leaves the broken file in place for repair.
        {
            assert(_text_to_file_atomic(paths.settings,
                                        StringView("{ garbage")));
            std::vector<String> w;
            Settings s = load_settings(paths, &w);
            assert(!w.empty());
            assert(s.keymap.action_from_combo(
                *combo_from_string(StringView("R"))) == Action::rotate);
            String text = string_from_file(paths.settings);
            assert(StringView(text) == StringView("{ garbage"));
        }

        // Self-cleaning.
        std::error_code ec;
        std::filesystem::remove_all(paths.directory, ec);
        assert(!std::filesystem::exists(paths.directory));

        co_return;
    };

} // namespace wry
