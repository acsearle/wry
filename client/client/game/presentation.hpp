//
//  presentation.hpp
//  client
//
//  Created by Claude on 2026-11-10.
//
//  Presentation tables loaded from text assets: the display names and
//  colours of terrain and matter kinds, and the (draft) opcode help.
//  The files are <repo>/assets/{terrain,matter,opcodes}.json, copied
//  into client.app/Contents/Resources/data by the build; see
//  assets/README.md.
//
//  Presentation only: per client, freely editable, never read by the
//  simulation (World::step, hashing, saves).  The rules tables -- matter
//  prices, dig times, generator parameters -- stay compiled in, since
//  lockstep peers must agree on them (docs/data_driven.md).
//
//  Fallbacks: every field starts at its compiled-in value
//  (TERRAIN_COLOR_SRGB, MATTER_COLOR_SRGB, names title-cased from the
//  enumerator spelling), and the loaders below only overwrite what the
//  file supplies well-formed.  A missing file, missing keys, or a
//  malformed document each append one warning; unknown keys and
//  malformed fields warn and are skipped.  The opcode help has no
//  compiled-in text: an opcode without an entry has empty strings.
//

#ifndef presentation_hpp
#define presentation_hpp

#include <array>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <vector>

#include "matter.hpp"
#include "opcode.hpp"
#include "string.hpp"
#include "terrain.hpp"

namespace wry {

    using Rgba8 = std::array<uint8_t, 4>;   // sRGB bytes, straight alpha

    struct KindPresentation {
        String name;    // display name, "Iron Ore"
        Rgba8 color;    // sRGB
    };

    struct OpcodeHelp {
        String name;      // display name, "Turn North"; empty: no entry
        String summary;   // one line, for a tooltip
        String help;      // a few sentences
        String stack;     // "a b -- c", top rightmost; empty: untouched
    };

    inline constexpr std::size_t MATTER_KIND_COUNT = std::size(MATTER_NAMES);
    inline constexpr std::size_t OPCODE_COUNT = std::size(OPCODE_NAMES);

    struct Presentation {
        std::array<KindPresentation, TERRAIN_KIND_COUNT> terrain;
        std::array<KindPresentation, MATTER_KIND_COUNT> matter;
        std::array<OpcodeHelp, OPCODE_COUNT> opcode;   // indexed by OPCODE

        // The terrain tint for a code; an unknown code reads as rock
        // (plain ground), as the board and map have always drawn it
        Rgba8 const& terrain_color(Terrain t) const {
            return terrain[((t >= 0) && (t < TERRAIN_KIND_COUNT)) ? t : TERRAIN_ROCK].color;
        }

        // The pile colour for a code; an unknown code reads as code 0
        Rgba8 const& matter_color(int64_t m) const {
            return matter[((m >= 0) && ((std::size_t)m < MATTER_KIND_COUNT)) ? (std::size_t)m : 0].color;
        }

        // The help entry for an opcode, or nullptr (no entry, or an
        // unknown code)
        OpcodeHelp const* opcode_help(int64_t op) const {
            if ((op < 0) || ((std::size_t)op >= OPCODE_COUNT))
                return nullptr;
            OpcodeHelp const& h = opcode[(std::size_t)op];
            return h.name.empty() ? nullptr : &h;
        }
    };

    // The compiled-in values: the colour tables of terrain.hpp and
    // matter.hpp, names title-cased from the enumerators, no opcode help
    Presentation default_presentation();

    // Overlay one file's text onto *p.  `file` names it in warnings.
    // Never throws: a malformed document appends a warning and leaves
    // *p untouched.
    void terrain_presentation_from_json(Presentation& p, StringView text,
                                        std::vector<String>* warnings,
                                        StringView file = StringView("terrain.json"));
    void matter_presentation_from_json(Presentation& p, StringView text,
                                       std::vector<String>* warnings,
                                       StringView file = StringView("matter.json"));
    void opcode_presentation_from_json(Presentation& p, StringView text,
                                       std::vector<String>* warnings,
                                       StringView file = StringView("opcodes.json"));

    // Where the shipped files are: the app bundle's Resources/data when
    // it exists (the CMake build copies assets/*.json there), else the
    // source tree's assets/ (an Xcode build, which does not copy them),
    // found from this source file's own path.  Empty if neither exists.
    std::filesystem::path presentation_directory();

    // default_presentation(), overlaid with whichever of the three files
    // `directory` holds; a missing file appends one warning
    Presentation load_presentation(std::filesystem::path const& directory,
                                   std::vector<String>* warnings);

    // The process's tables.  Until presentation_install they are
    // default_presentation().  presentation_install must run before any
    // other thread starts (core/main.mm calls it before the collector
    // and pool threads are created): the tables are then immutable, and
    // std::thread's construction synchronizes-with the start of each
    // new thread ([thread.thread.constr]), so every later read, on any
    // thread or any task those threads run, happens-after the install
    // and needs no further synchronization.
    Presentation const& presentation();
    void presentation_install(Presentation p);

} // namespace wry

#endif /* presentation_hpp */
