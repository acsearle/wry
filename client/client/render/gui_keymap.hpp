//
//  gui_keymap.hpp
//  client
//
//  Created by Antony Searle on 2026-08-11.
//
//  User-configurable key bindings: the platform-neutral vocabulary of
//  bindable actions, canonical key names, key combos, and the Keymap that
//  maps combos to actions.
//
//  Three parallel namings for actions, deliberately distinct:
//
//    - C++ enumerators (Action::reverse_rotate), for code;
//    - stable identifiers ("reverse-rotate"), for settings.json.  These
//      are the enumerator spelling in kebab-case, and they are file
//      format: never rename or reuse one that has shipped;
//    - display names ("Reverse rotate"), for the settings UI.  Free to
//      change at any time.
//
//  Key names serve both presentation and serialization: "R", "Space",
//  "PageUp".  A combo serializes as canonically-ordered modifier prefixes
//  plus a key name: "Shift+R", "Ctrl+Alt+PageUp".  Parsing accepts any
//  modifier order and any case, plus common aliases (Control, Option,
//  Command, ...); serialization always emits the canonical form.
//
//  Modifier keys are not distinguished left from right (either Shift is
//  Shift), matching the Event layer and common game practice (Factorio,
//  default SDL bindings).  Matching is exact over the Shift/Ctrl/Alt/Cmd
//  set: a combo bound as "R" does not fire on Shift+R.  Caps lock and Fn
//  never participate in bindings.
//
//  A combo activates at most one action; an action may be activated by
//  any number of combos.  Escape is reserved for the GUI (menu open /
//  close, capture cancel) and cannot be bound.
//

#ifndef gui_keymap_hpp
#define gui_keymap_hpp

#include <cstdint>
#include <optional>
#include <vector>

#include "gui_event.hpp"
#include "string.hpp"

namespace wry::gui {

    // ----------------------------------------------------------------
    // Actions.
    //
    // X-macro master list: enumerator, stable id, display name, default
    // binding ("" = unbound by default), fires-on-key-repeat.  Repeat
    // makes sense for actions that advance a cycle (holding R keeps
    // rotating); it is suppressed for toggles, where auto-repeat would
    // just flicker the state.
    //
    // Append new actions at the end; never rename or reuse a shipped id.

#define WRY_ACTION_LIST(X) \
    X(rotate,           "rotate",           "Rotate",                   "R",       true)  \
    X(reverse_rotate,   "reverse-rotate",   "Reverse rotate",           "Shift+R", true)  \
    X(flip_horizontal,  "flip-horizontal",  "Flip horizontal",          "H",       false) \
    X(flip_vertical,    "flip-vertical",    "Flip vertical",            "V",       false) \
    X(toggle_map,       "toggle-map",       "Toggle map",               "Tab",     false) \
    X(toggle_console,   "toggle-console",   "Toggle console",           "`",       false) \
    X(toggle_jacobians, "toggle-jacobians", "Toggle Jacobians (debug)", "J",       false) \
    X(toggle_points,    "toggle-points",    "Toggle points (debug)",    "P",       false) \
    X(toggle_wireframe, "toggle-wireframe", "Toggle wireframe (debug)", "W",       false)

    enum class Action : uint32_t {
#define WRY_X(SYM, ID, NAME, COMBO, REPEATS) SYM,
        WRY_ACTION_LIST(WRY_X)
#undef WRY_X
    };

    inline constexpr size_t action_count = 0
#define WRY_X(SYM, ID, NAME, COMBO, REPEATS) + 1
        WRY_ACTION_LIST(WRY_X)
#undef WRY_X
    ;

    // The stable settings.json identifier ("reverse-rotate").
    StringView id_from_action(Action);

    // The human-readable settings-UI name ("Reverse rotate").
    StringView display_name_from_action(Action);

    // Whether a held key re-fires the action on auto-repeat.
    bool action_fires_on_repeat(Action);

    // Inverse of id_from_action; nullopt for unknown ids (e.g. an id from
    // a settings.json written by a different version).
    std::optional<Action> action_from_id(StringView);

    // ----------------------------------------------------------------
    // Key names.
    //
    // Printable keys are named by their unshifted keycap: letters
    // uppercase ("R"), digits and punctuation as themselves ("7", "-",
    // "`"), except Space which is spelled out.  Named keys use their
    // conventional names: "Tab", "Enter", "Escape", "Backspace",
    // "Delete", "Up", "Down", "Left", "Right", "Home", "End", "PageUp",
    // "PageDown", "F1".."F12".  Parsing is ASCII-case-insensitive.

    // Empty for keys with no name (key::Unknown, out-of-range codes).
    String name_from_key(uint32_t k);

    std::optional<uint32_t> key_from_name(StringView);

    // ----------------------------------------------------------------
    // Key combos.

    // The modifier bits that participate in binding matching.
    inline constexpr uint16_t combo_modifier_mask =
        Modifiers::Shift | Modifiers::Ctrl | Modifiers::Alt | Modifiers::Cmd;

    struct KeyCombo {
        uint32_t key = key::Unknown;
        uint16_t mods = 0;   // subset of combo_modifier_mask

        friend constexpr bool operator==(KeyCombo const&,
                                         KeyCombo const&) = default;
    };

    // The combo a key event would activate: the event's key plus its
    // modifiers masked to combo_modifier_mask (Caps / Fn dropped).
    KeyCombo combo_from_event(Event const&);

    // Whether the combo may be bound at all: the key must be nameable and
    // must not be reserved (Escape).  Applied both by the capture UI and
    // when loading settings.json, so file surgery can't smuggle in a
    // reserved key.
    bool combo_is_bindable(KeyCombo);

    // "Ctrl+Alt+Shift+Cmd" prefixes in that canonical order (any subset),
    // then the key name.  Empty for unnameable keys.
    String string_from_combo(KeyCombo);

    // Parses the serialized / displayed form.  Case-insensitive, any
    // modifier order, aliases accepted (Control, Ctl; Option, Opt; Command,
    // Super, Meta, Win).  nullopt on unknown modifier or key name.
    std::optional<KeyCombo> combo_from_string(StringView);

    // ----------------------------------------------------------------
    // Keymap: the live combo -> action mapping.
    //
    // Deliberately a flat vector: bindings number in the tens, lookups
    // happen at keystroke rate, and a vector preserves a stable,
    // deterministic order for both the settings UI and settings.json.

    class Keymap {
    public:

        struct Binding {
            KeyCombo combo;
            Action action;
        };

        std::optional<Action> action_from_combo(KeyCombo) const;

        // All combos bound to `action`, in binding order.
        std::vector<KeyCombo> combos_from_action(Action) const;

        // Bind combo -> action, first unbinding `combo` from whatever it
        // was bound to (a combo activates at most one action).  No-ops on
        // unbindable combos.
        void bind(KeyCombo, Action);

        void unbind(KeyCombo);

        // Remove every combo bound to `action`.
        void clear_action(Action);

        void clear() { _bindings.clear(); }

        std::vector<Binding> const& bindings() const { return _bindings; }

    private:
        std::vector<Binding> _bindings;
    };

    // The compiled-in default bindings (the fourth WRY_ACTION_LIST column).
    Keymap default_keymap();

} // namespace wry::gui

#endif /* gui_keymap_hpp */
