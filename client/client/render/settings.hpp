//
//  settings.hpp
//  client
//
//  Created by Antony Searle on 2026-08-11.
//
//  App-level user settings and their settings.json lifecycle.
//
//  Settings currently holds only the key bindings, but the struct and
//  the file are both shaped for growth: key bindings are one named
//  group ("key_bindings") inside the root object, so future groups
//  (audio, video, ...) become siblings, not schema breaks.
//
//  On-disk layout, all under one directory (default "config", a
//  sibling of "saves" under the working directory):
//
//    settings.json           the live, user-owned settings.  Users may
//                            hand-edit it; every loader below tolerates
//                            malformed pieces (warn and continue) and a
//                            malformed document (warn and run on
//                            defaults, leaving the file untouched for
//                            repair).
//    settings-default.json   machine-written image of the compiled-in
//                            defaults, refreshed on every load so it
//                            always documents this binary's defaults.
//                            Not user-editable (edits are clobbered);
//                            exists as the reference to copy from when
//                            doing file surgery, and as the source for
//                            first-run installation and for reset.
//    settings-backup-N.json  written by reset_settings (N = max+1), so
//                            the pre-reset state is always recoverable.
//
//  Documented behaviors of the load path (see settings_from_json):
//    - an absent action id takes its compiled-in default bindings;
//    - an id present with [] is explicitly unbound (distinct from
//      absent);
//    - unknown ids, malformed combos, and reserved keys are skipped
//      with a warning; the rest of the file still applies;
//    - binding a combo always steals it from any other action (same
//      rule as the rebind UI); a warning reports the displacement.
//

#ifndef settings_hpp
#define settings_hpp

#include <filesystem>
#include <vector>

#include "gui_keymap.hpp"
#include "string.hpp"

namespace wry {

    struct Settings {
        gui::Keymap keymap;
        // Future setting groups (audio, video, ...) land here.
    };

    // The compiled-in defaults (currently: default_keymap()).
    Settings default_settings();

    // ----------------------------------------------------------------
    // Pure text round-trip.

    // Serialize to the settings.json text form: pretty-printed, actions
    // in declaration order, every action listed (so the file documents
    // the full vocabulary, and [] visibly means "unbound").
    String json_from_settings(Settings const&);

    // Parse settings.json text.  Malformed pieces degrade per the rules
    // above, each appending one line to *warnings (if non-null).
    // Throws json::JsonParseError only if the document itself does not
    // parse or the root is not an object.
    Settings settings_from_json(StringView text,
                                std::vector<String>* warnings);

    // ----------------------------------------------------------------
    // File lifecycle.

    struct SettingsPaths {
        std::filesystem::path directory;   // e.g. "config"
        std::filesystem::path settings;    // .../settings.json
        std::filesystem::path defaults;    // .../settings-default.json
    };

    SettingsPaths settings_paths(std::filesystem::path directory = "config");

    // Load settings.json, creating the directory, refreshing
    // settings-default.json from the compiled-in defaults, and -- if
    // settings.json is absent -- installing it as a copy of the default
    // file first.  Never fails: any file-level problem appends a
    // warning and the compiled-in defaults fill in.
    Settings load_settings(SettingsPaths const&,
                           std::vector<String>* warnings);

    // Write settings.json (temp file + atomic rename).  False on any
    // I/O failure, in which case the previous file is left intact.
    bool save_settings(Settings const&, SettingsPaths const&);

    // Back up settings.json to settings-backup-N.json (N = max existing
    // + 1), then install a fresh copy of settings-default.json in its
    // place, and return the newly-loaded (default) settings.  If the
    // backup cannot be made, the reset is aborted (the user's file is
    // never destroyed un-backed-up) and the current settings are
    // returned instead, with a warning.
    Settings reset_settings(SettingsPaths const&,
                            std::vector<String>* warnings);

} // namespace wry

#endif /* settings_hpp */
