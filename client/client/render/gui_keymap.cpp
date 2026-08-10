//
//  gui_keymap.cpp
//  client
//
//  Created by Antony Searle on 2026-08-11.
//

#include <algorithm>

#include "gui_keymap.hpp"
#include "test.hpp"

namespace wry::gui {

    // ----------------------------------------------------------------
    // Action tables.

    namespace {

        const StringView _action_ids[] = {
#define WRY_X(SYM, ID, NAME, COMBO, REPEATS) StringView(ID),
            WRY_ACTION_LIST(WRY_X)
#undef WRY_X
        };

        const StringView _action_display_names[] = {
#define WRY_X(SYM, ID, NAME, COMBO, REPEATS) StringView(NAME),
            WRY_ACTION_LIST(WRY_X)
#undef WRY_X
        };

        const StringView _action_default_combos[] = {
#define WRY_X(SYM, ID, NAME, COMBO, REPEATS) StringView(COMBO),
            WRY_ACTION_LIST(WRY_X)
#undef WRY_X
        };

        constexpr bool _action_repeats[] = {
#define WRY_X(SYM, ID, NAME, COMBO, REPEATS) REPEATS,
            WRY_ACTION_LIST(WRY_X)
#undef WRY_X
        };

    } // anonymous namespace

    StringView id_from_action(Action a) {
        assert((size_t)a < action_count);
        return _action_ids[(size_t)a];
    }

    StringView display_name_from_action(Action a) {
        assert((size_t)a < action_count);
        return _action_display_names[(size_t)a];
    }

    bool action_fires_on_repeat(Action a) {
        assert((size_t)a < action_count);
        return _action_repeats[(size_t)a];
    }

    std::optional<Action> action_from_id(StringView v) {
        for (size_t i = 0; i != action_count; ++i)
            if (_action_ids[i] == v)
                return (Action)i;
        return std::nullopt;
    }

    // ----------------------------------------------------------------
    // Key names.

    namespace {

        struct _named_key {
            uint32_t code;
            StringView name;
        };

        // Non-printable keys only; printables are named by their keycap
        // below.  F2..F12 are generated arithmetically from F1.
        const _named_key _named_keys[] = {
            { key::Backspace,  StringView("Backspace") },
            { key::Tab,        StringView("Tab")       },
            { key::Enter,      StringView("Enter")     },
            { key::Escape,     StringView("Escape")    },
            { key::Delete,     StringView("Delete")    },
            { key::ArrowUp,    StringView("Up")        },
            { key::ArrowDown,  StringView("Down")      },
            { key::ArrowLeft,  StringView("Left")      },
            { key::ArrowRight, StringView("Right")     },
            { key::Home,       StringView("Home")      },
            { key::End,        StringView("End")       },
            { key::PageUp,     StringView("PageUp")    },
            { key::PageDown,   StringView("PageDown")  },
        };

        constexpr uint32_t _function_key_count = 12;   // F1..F12

        constexpr char32_t _ascii_tolower(char32_t c) {
            return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
        }

        bool _equals_ci(StringView a, StringView b) {
            auto i = a.begin(), ie = a.end();
            auto j = b.begin(), je = b.end();
            for (; i != ie && j != je; ++i, ++j)
                if (_ascii_tolower(*i) != _ascii_tolower(*j))
                    return false;
            return i == ie && j == je;
        }

    } // anonymous namespace

    String name_from_key(uint32_t k) {
        String s;
        if (k == ' ') {
            s.append("Space");
        } else if (k > 0x20 && k <= 0x7E) {
            // Printable: uppercase keycap for letters, the character
            // itself for digits and punctuation.
            char32_t c = (char32_t)k;
            if (c >= 'a' && c <= 'z')
                c -= ('a' - 'A');
            s.push_back(c);
        } else if (k >= key::F1 && k < key::F1 + _function_key_count) {
            s.push_back('F');
            uint32_t n = k - key::F1 + 1;
            if (n >= 10) {
                s.push_back((char32_t)('0' + n / 10));
                n %= 10;
            }
            s.push_back((char32_t)('0' + n));
        } else {
            for (auto const& nk : _named_keys) {
                if (nk.code == k) {
                    s.append(nk.name);
                    break;
                }
            }
        }
        return s;
    }

    std::optional<uint32_t> key_from_name(StringView v) {
        if (v.empty())
            return std::nullopt;

        // Single character: a printable keycap.  Letters fold to the
        // lowercase code the Event layer reports.
        {
            StringView u{v};
            char32_t c = u.front();
            u.pop_front();
            if (u.empty() && c > 0x20 && c <= 0x7E)
                return (uint32_t)_ascii_tolower(c);
        }

        if (_equals_ci(v, StringView("Space")))
            return (uint32_t)' ';

        for (auto const& nk : _named_keys)
            if (_equals_ci(v, nk.name))
                return nk.code;

        // "F1".."F12".
        {
            StringView u{v};
            if (_ascii_tolower(u.front()) == 'f') {
                u.pop_front();
                uint32_t n = 0;
                int digits = 0;
                while (!u.empty()) {
                    char32_t c = u.front();
                    if (c < '0' || c > '9')
                        return std::nullopt;
                    n = n * 10 + (uint32_t)(c - '0');
                    ++digits;
                    u.pop_front();
                }
                if (digits >= 1 && digits <= 2 &&
                    n >= 1 && n <= _function_key_count)
                    return key::F1 + (n - 1);
            }
        }

        return std::nullopt;
    }

    // ----------------------------------------------------------------
    // Key combos.

    KeyCombo combo_from_event(Event const& e) {
        return KeyCombo{
            e.key,
            (uint16_t)(e.mods.bits & combo_modifier_mask),
        };
    }

    bool combo_is_bindable(KeyCombo c) {
        if (c.key == key::Unknown || c.key == key::Escape)
            return false;
        if (c.mods & ~combo_modifier_mask)
            return false;
        return !name_from_key(c.key).empty();
    }

    namespace {

        struct _modifier_name {
            StringView name;
            uint16_t bit;
        };

        // Canonical names first (used for serialization); the rest are
        // parse-time aliases.
        const _modifier_name _modifier_names[] = {
            { StringView("Ctrl"),    Modifiers::Ctrl  },
            { StringView("Alt"),     Modifiers::Alt   },
            { StringView("Shift"),   Modifiers::Shift },
            { StringView("Cmd"),     Modifiers::Cmd   },
            { StringView("Control"), Modifiers::Ctrl  },
            { StringView("Ctl"),     Modifiers::Ctrl  },
            { StringView("Option"),  Modifiers::Alt   },
            { StringView("Opt"),     Modifiers::Alt   },
            { StringView("Command"), Modifiers::Cmd   },
            { StringView("Super"),   Modifiers::Cmd   },
            { StringView("Meta"),    Modifiers::Cmd   },
            { StringView("Win"),     Modifiers::Cmd   },
        };

        // If `v` starts (case-insensitively) with `name` followed by '+'
        // and at least one more character, consume through the '+' and
        // return true.  The one-more-character requirement keeps the
        // final segment -- the key name -- intact, so "Shift++" parses as
        // Shift plus the '+' key.
        bool _consume_modifier_prefix(StringView& v, StringView name) {
            StringView u{v};
            auto i = name.begin(), ie = name.end();
            for (; i != ie; ++i) {
                if (u.empty() || _ascii_tolower(u.front()) != _ascii_tolower(*i))
                    return false;
                u.pop_front();
            }
            if (u.empty() || u.front() != '+')
                return false;
            u.pop_front();
            if (u.empty())
                return false;
            v.reset(u);
            return true;
        }

    } // anonymous namespace

    String string_from_combo(KeyCombo c) {
        String key_name = name_from_key(c.key);
        if (key_name.empty())
            return key_name;
        String s;
        // Canonical order: Ctrl, Alt, Shift, Cmd (the first four table
        // entries, in table order).
        for (size_t i = 0; i != 4; ++i) {
            if (c.mods & _modifier_names[i].bit) {
                s.append(_modifier_names[i].name);
                s.push_back('+');
            }
        }
        s.append(key_name);
        return s;
    }

    std::optional<KeyCombo> combo_from_string(StringView v) {
        KeyCombo c;
        StringView u{v};
        bool progressed = true;
        while (progressed) {
            progressed = false;
            for (auto const& m : _modifier_names) {
                if (_consume_modifier_prefix(u, m.name)) {
                    c.mods |= m.bit;
                    progressed = true;
                    break;
                }
            }
        }
        std::optional<uint32_t> k = key_from_name(u);
        if (!k)
            return std::nullopt;
        c.key = *k;
        if (!combo_is_bindable(c))
            return std::nullopt;
        return c;
    }

    // ----------------------------------------------------------------
    // Keymap.

    std::optional<Action> Keymap::action_from_combo(KeyCombo c) const {
        for (Binding const& b : _bindings)
            if (b.combo == c)
                return b.action;
        return std::nullopt;
    }

    std::vector<KeyCombo> Keymap::combos_from_action(Action a) const {
        std::vector<KeyCombo> result;
        for (Binding const& b : _bindings)
            if (b.action == a)
                result.push_back(b.combo);
        return result;
    }

    void Keymap::bind(KeyCombo c, Action a) {
        if (!combo_is_bindable(c))
            return;
        unbind(c);
        _bindings.push_back(Binding{c, a});
    }

    void Keymap::unbind(KeyCombo c) {
        std::erase_if(_bindings,
                      [c](Binding const& b) { return b.combo == c; });
    }

    void Keymap::clear_action(Action a) {
        std::erase_if(_bindings,
                      [a](Binding const& b) { return b.action == a; });
    }

    Keymap default_keymap() {
        Keymap m;
        for (size_t i = 0; i != action_count; ++i) {
            StringView v = _action_default_combos[i];
            if (v.empty())
                continue;
            std::optional<KeyCombo> c = combo_from_string(v);
            // The default table is compiled in; a default that doesn't
            // parse is a programming error, not a data error.
            assert(c);
            if (c)
                m.bind(*c, (Action)i);
        }
        return m;
    }

    // ----------------------------------------------------------------
    // Tests.

    define_test("keymap_key_names")
    {
        // Printables: letters fold to the lowercase Event code but name
        // as the uppercase keycap.
        assert(name_from_key('r') == StringView("R"));
        assert(key_from_name(StringView("R")) == (uint32_t)'r');
        assert(key_from_name(StringView("r")) == (uint32_t)'r');
        assert(name_from_key('7') == StringView("7"));
        assert(key_from_name(StringView("7")) == (uint32_t)'7');
        assert(name_from_key('`') == StringView("`"));
        assert(name_from_key('+') == StringView("+"));

        // Space is spelled out.
        assert(name_from_key(' ') == StringView("Space"));
        assert(key_from_name(StringView("Space")) == (uint32_t)' ');
        assert(key_from_name(StringView("space")) == (uint32_t)' ');

        // Named keys, case-insensitive parse.
        assert(name_from_key(key::Tab) == StringView("Tab"));
        assert(key_from_name(StringView("tab")) == key::Tab);
        assert(name_from_key(key::ArrowUp) == StringView("Up"));
        assert(key_from_name(StringView("UP")) == key::ArrowUp);
        assert(name_from_key(key::PageDown) == StringView("PageDown"));
        assert(key_from_name(StringView("pagedown")) == key::PageDown);

        // Function keys.
        assert(name_from_key(key::F1) == StringView("F1"));
        assert(name_from_key(key::F1 + 11) == StringView("F12"));
        assert(key_from_name(StringView("F12")) == key::F1 + 11);
        assert(key_from_name(StringView("f1")) == key::F1);
        assert(!key_from_name(StringView("F13")));
        assert(!key_from_name(StringView("F0")));

        // Unnameable.
        assert(name_from_key(key::Unknown).empty());
        assert(!key_from_name(StringView("NoSuchKey")));
        assert(!key_from_name(StringView("")));

        co_return;
    };

    define_test("keymap_combos")
    {
        using M = Modifiers;

        // Round-trip a plain key and a modified key.
        {
            std::optional<KeyCombo> c = combo_from_string(StringView("R"));
            assert(c && c->key == 'r' && c->mods == 0);
            assert(string_from_combo(*c) == StringView("R"));
        }
        {
            std::optional<KeyCombo> c =
                combo_from_string(StringView("Shift+R"));
            assert(c && c->key == 'r' && c->mods == M::Shift);
            assert(string_from_combo(*c) == StringView("Shift+R"));
        }

        // Canonical emission order regardless of parse order; alias and
        // case tolerance.
        {
            std::optional<KeyCombo> c =
                combo_from_string(StringView("shift+control+p"));
            assert(c && c->mods == (M::Shift | M::Ctrl));
            assert(string_from_combo(*c) == StringView("Ctrl+Shift+P"));
        }
        {
            std::optional<KeyCombo> c =
                combo_from_string(StringView("COMMAND+option+Tab"));
            assert(c && c->key == key::Tab &&
                   c->mods == (M::Cmd | M::Alt));
            assert(string_from_combo(*c) == StringView("Alt+Cmd+Tab"));
        }

        // The '+' key itself.
        {
            std::optional<KeyCombo> c =
                combo_from_string(StringView("Shift++"));
            assert(c && c->key == '+' && c->mods == M::Shift);
            assert(string_from_combo(*c) == StringView("Shift++"));
        }
        {
            std::optional<KeyCombo> c = combo_from_string(StringView("+"));
            assert(c && c->key == '+' && c->mods == 0);
        }

        // Rejections: unknown key, unknown modifier, reserved, empty,
        // trailing '+'.
        assert(!combo_from_string(StringView("")));
        assert(!combo_from_string(StringView("Shift+")));
        assert(!combo_from_string(StringView("Hyper+R")));
        assert(!combo_from_string(StringView("Escape")));
        assert(!combo_from_string(StringView("Shift+Escape")));
        assert(!combo_from_string(StringView("Bogus")));

        // combo_from_event masks Caps / Fn out.
        {
            Event e{};
            e.kind = WryEventKindKeyDown;
            e.key = 'r';
            e.mods.bits = M::Shift | M::Caps | M::Fn;
            KeyCombo c = combo_from_event(e);
            assert(c.key == 'r' && c.mods == M::Shift);
            assert(combo_is_bindable(c));
        }

        co_return;
    };

    define_test("keymap_bindings")
    {
        Keymap m = default_keymap();

        // Defaults: every action's default combo landed on that action.
        for (size_t i = 0; i != action_count; ++i) {
            Action a = (Action)i;
            std::vector<KeyCombo> combos = m.combos_from_action(a);
            assert(combos.size() == 1);
            assert(m.action_from_combo(combos[0]) == a);
        }

        // Ids round-trip.
        for (size_t i = 0; i != action_count; ++i) {
            Action a = (Action)i;
            assert(action_from_id(id_from_action(a)) == a);
            assert(!display_name_from_action(a).empty());
        }
        assert(!action_from_id(StringView("no-such-action")));

        // R activates rotate; Shift+R activates reverse_rotate; exact
        // modifier matching keeps them distinct.
        KeyCombo r = *combo_from_string(StringView("R"));
        KeyCombo shift_r = *combo_from_string(StringView("Shift+R"));
        assert(m.action_from_combo(r) == Action::rotate);
        assert(m.action_from_combo(shift_r) == Action::reverse_rotate);

        // Binding steals: bind R to flip_horizontal and rotate loses it.
        m.bind(r, Action::flip_horizontal);
        assert(m.action_from_combo(r) == Action::flip_horizontal);
        assert(m.combos_from_action(Action::rotate).empty());
        // flip_horizontal now has both its default H and the stolen R.
        assert(m.combos_from_action(Action::flip_horizontal).size() == 2);

        // One action, several keys: add F5 to rotate as well.
        KeyCombo f5 = *combo_from_string(StringView("F5"));
        m.bind(f5, Action::rotate);
        KeyCombo f6 = *combo_from_string(StringView("F6"));
        m.bind(f6, Action::rotate);
        assert(m.combos_from_action(Action::rotate).size() == 2);
        assert(m.action_from_combo(f5) == Action::rotate);
        assert(m.action_from_combo(f6) == Action::rotate);

        // unbind / clear_action.
        m.unbind(f5);
        assert(!m.action_from_combo(f5));
        assert(m.combos_from_action(Action::rotate).size() == 1);
        m.clear_action(Action::flip_horizontal);
        assert(m.combos_from_action(Action::flip_horizontal).empty());
        assert(!m.action_from_combo(r));

        // Unbindable combos are refused.
        KeyCombo esc{key::Escape, 0};
        m.bind(esc, Action::rotate);
        assert(!m.action_from_combo(esc));

        // Repeat policy: cycles repeat, toggles don't.
        assert(action_fires_on_repeat(Action::rotate));
        assert(action_fires_on_repeat(Action::reverse_rotate));
        assert(!action_fires_on_repeat(Action::toggle_map));
        assert(!action_fires_on_repeat(Action::toggle_console));

        co_return;
    };

} // namespace wry::gui
