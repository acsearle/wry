//
//  gui_line_history.hpp
//  client
//
//  Created by Antony Searle on 2026-11-05.
//
//  The lines a LineEditor's owner has submitted: recall, prefix search,
//  suggestion, and a best-effort file.  render/docs/line_editing.md.
//
//  RECALL.  older() and newer() walk the entries (Up and Down).  The
//  line being typed when the walk starts is the DRAFT: it is kept, it
//  comes back when the walk returns past the newest entry, and it is
//  also the search term -- with `st` typed, the walk visits only entries
//  that begin `st`.  An empty draft visits everything.  Entries are
//  never altered by what is done to a recalled line; editing one and
//  walking again starts a new walk with the edited line as the draft.
//
//  FILE.  One entry per line of UTF-8 text.  Loaded once; each new entry
//  is appended as it is added, with one open-write-close and no sync to
//  disk.  Nothing here is transactional, on purpose: a crash can lose
//  the last few entries or leave the last one cut short, and two
//  processes appending at once interleave whole lines.  When a load
//  finds the file has grown past twice the capacity it is rewritten
//  with the newest `capacity` entries.
//

#ifndef gui_line_history_hpp
#define gui_line_history_hpp

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

#include "string.hpp"

namespace wry::gui {

    class LineHistory {
    public:

        // The most entries kept in memory, and what a prune keeps.
        static constexpr std::size_t capacity = 1000;

        // Entries, oldest first.
        std::vector<String> const& entries() const { return _entries; }

        // Record a submitted line and end any walk.  Blank lines and an
        // immediate repeat of the newest entry are not recorded.
        void add(StringView line);

        // Walk towards older / newer entries.  `current` is the line as
        // it stands.  Returns the line to show, or nullopt to leave it:
        // nothing older matches, or the walk is already at the draft.
        std::optional<String> older(StringView current);
        std::optional<String> newer(StringView current);

        // The newest entry that begins with `prefix` and is longer than
        // it; empty if there is none or the prefix is empty.
        StringView suggest(StringView prefix) const;

        // Read `path` (if it exists) into the entries and append new
        // entries to it from now on.  Failures are silent: history is a
        // convenience.
        void attach(std::filesystem::path path);

    private:

        bool _walking() const { return _index != _entries.size(); }

        std::vector<String> _entries;

        // The walk.  _index == _entries.size(): not walking.
        std::size_t _index = 0;
        String _draft;      // the line the walk started from
        String _shown;      // what the walk last returned

        std::filesystem::path _path;    // empty: not persisted

    };

} // namespace wry::gui

#endif /* gui_line_history_hpp */
