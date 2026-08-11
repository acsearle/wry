# Settings and user-configurable key bindings

Status: landing incrementally, 2026-08-11.

This documents the design decisions behind `render/gui_keymap.{hpp,cpp}`
(actions, key names, combos, the Keymap), `render/settings.{hpp,cpp}`
(the settings.json lifecycle), and the Settings / Key bindings overlays.
The general principle applied to open questions was "do as Factorio
does", with SDL3 and common practice as fallback.

## Naming: enums vs json ids vs presentation strings

Three parallel namings, deliberately distinct:

- **C++ enumerators** (`Action::reverse_rotate`): for code.
- **Stable ids** (`"reverse-rotate"`): the settings.json vocabulary; the
  enumerator spelling in kebab-case.  These are file format: once one
  has shipped it is never renamed or reused (Factorio's control names in
  `config.ini` -- `rotate`, `reverse-rotate`, `toggle-menu` -- behave
  the same way).
- **Display names** (`"Reverse rotate"`): what the settings UI shows.
  Free to change at any time; will eventually be localizable.

Key names are the exception: the serialized name and the displayed name
are the same string ("R", "Space", "PageUp", "Shift+R").  SDL does the
same (SDL_GetKeyName output is both presentable and stable), and having
two names for the R key would help nobody.

All three action namings live in one X-macro (`WRY_ACTION_LIST`) so they
cannot drift apart; the table also carries each action's default binding
and repeat behavior.

## Keys are logical, layout-dependent, ASCII-named

A binding stores the *logical* key the Event layer reports: printable
keys as their unshifted lowercased character (layout-aware -- a Dvorak
user's R is Dvorak's R), named keys as `wry::gui::key::` codes.  This
follows SDL keycodes (not scancodes): bindings follow the keycap, not
the physical position.  Rebinding-by-capture makes the difference mostly
moot -- users press the key they want, and we record what the platform
says it was.

Names: letters are uppercase keycaps ("R"), digits and punctuation are
themselves ("7", "-", "`"), Space is spelled out, named keys use their
conventional names (Tab, Enter, Escape, Backspace, Delete, Up, Down,
Left, Right, Home, End, PageUp, PageDown, F1..F12).  Parsing is
case-insensitive.

## Modifiers: no left/right distinction

`Modifiers` has a single bit per modifier (either Shift is Shift),
matching NSEvent's device-independent flags, Factorio, and default game
practice.  A non-distinguished binding is therefore represented by ONE
entry, not two.  If we ever distinguish (SDL3 can), the plan is
additive: keep the plain bits meaning "either side" and introduce
`LShift`/`RShift` names for the distinguished cases, so existing files
keep their meaning.

Combos serialize as canonically-ordered prefixes -- Ctrl, Alt, Shift,
Cmd -- plus the key name: `"Ctrl+Shift+P"`.  Parsing accepts any order,
any case, and common aliases (Control/Ctl, Option/Opt, Command / Super /
Meta / Win); serialization always emits canonical form.  `Cmd` is the
platform-neutral spelling of Command / Windows / Super, as in
gui_event.hpp.

Matching is exact over the four binding modifiers: `R` does not fire on
`Shift+R` (they are different combos, exactly why `rotate` /
`reverse-rotate` work).  Caps lock and Fn are dropped at the event
boundary and never participate.

Modifier-only bindings (bare Shift as a control) are not supported: the
delegate's `flagsChanged:` is a stub and no current action wants one.
Mouse-button bindings are likewise future work; if added they become
key-space entries (a reserved code range) rather than a parallel system.

## Binding cardinality and dispatch

- A combo activates at most one action (`Keymap::bind` steals the combo
  from its previous owner -- also the capture UI's semantics).
- An action may be activated by any number of combos.
- `Escape` is reserved for the GUI (menus, capture cancel) and is
  refused by `combo_is_bindable`, including when loading settings.json.
- Key auto-repeat re-fires actions that advance a cycle (rotate) but not
  toggles (map / console / debug), per the X-macro's repeats column.
  (Previously every binding fired on repeat, so holding Tab flickered
  the map.)
- Bound combos are matched before the legacy hardwired fallbacks (hex
  digit entry), so a user binding, say, `F`, predictably wins over the
  hex-digit write on F.

GUI *navigation* keys (menu Escape/Enter/arrows, console line editing)
stay hardwired: they are interface conventions, not game controls, and
Factorio likewise does not expose menu navigation for rebinding.

## settings.json

Lives in `config/` under the working directory, a sibling of `saves/`
(Factorio's user-data layout: config/ next to saves/).  The cwd is the
assets dir for now (main.mm chdir); when a real per-user data dir
arrives, saves and config move together.

```
config/settings.json           the live, user-owned settings
config/settings-default.json   machine-written image of the compiled-in
                               defaults; refreshed at every launch, so
                               after an upgrade it documents the new
                               binary's defaults.  Not user-editable
                               (edits are clobbered); exists so file
                               surgery has a reference to copy from.
config/settings-backup-N.json  made by "reset to defaults" (N = max+1)
```

Shape -- key bindings are nested one level down so future setting
groups (audio, video, ...) are siblings, and the file carries a schema
version for future migration:

```json
{
    "version": 1,
    "key_bindings": {
        "rotate": ["R"],
        "reverse-rotate": ["Shift+R"],
        "toggle-map": ["Tab"]
    }
}
```

Load semantics (all tolerant, because the file is user-edited):

- file absent: settings-default.json is copied into place (task
  requirement; also Factorio's regenerate-if-missing behavior).
- file unparseable: run on compiled-in defaults, leave the file
  untouched (so a botched hand-edit can be fixed rather than being
  clobbered), and surface a warning in the floating log.
- action id absent: that action gets its compiled-in default bindings.
- action id present with `[]`: explicitly unbound (distinct from
  absent, standard practice).
- unknown action id / unparseable combo / reserved key: skipped with a
  warning; the rest of the file still applies.
- a combo bound in the file steals itself from wherever it was before
  (the same rule bind() and the rebind UI use).  An explicit file entry
  must win over the *default* binding of some other action the file
  never mentioned -- otherwise a deliberate "bind R to flip-vertical"
  would be silently overruled by rotate keeping its default R.  If two
  actions in the file both claim the same combo, the loader applies
  actions in declaration order (not the DOM's hash order), so the
  later-declared action keeps it; the displacement is warned either way.

Writes go through temp + rename in the same directory (the save-file
idiom), so a crash mid-write cannot corrupt settings.json.  The
serializer is a dedicated ordered emitter (actions in declaration
order), not the Json DOM, whose Table iteration order is
hash-dependent; the DOM handles the read side, where order is
irrelevant.

"Reset to defaults" backs up the current settings.json to
settings-backup-N.json, then copies settings-default.json over
settings.json -- so the pre-reset state is always recoverable by hand.

## The rebind capture mode

Settings > Key bindings lists one row per action: display name, its
combos, and a CLEAR button.  Clicking the action arms capture; the
overlay is modal in both axes and sits on the app-tier overlay stack
(GuiContext::overlays), which every scene dispatches *first* -- so an
armed capture genuinely sees the next keystroke before the console, the
world, or any menu.  No deeper hook into the platform layer is needed.

While armed: the next KeyDown (repeats ignored) becomes a binding via
`Keymap::bind` (stealing it from any other action), the file is saved
immediately, and capture disarms.  Escape cancels; so does any mouse
click.  Factorio's equivalent dialog behaves the same way.

Because the settings overlays live on the app-tier stack, the same UI
works from the main menu scene and from the in-game ESC menu.

## Platform firewall

Everything above is platform-neutral C++ (render/gui_keymap,
render/settings, the overlays in gui.mm).  The Apple-specific surface
is unchanged in shape: WryDelegate.mm translates NSEvent -> Event
(kVK_* to key codes -- extended to map F1..F12), and the delegate calls
settings load/save at startup/shutdown boundaries.  A new platform
would reimplement the delegate's translation table and nothing else.
