//
//  gui_line_history.cpp
//  client
//
//  Created by Antony Searle on 2026-11-05.
//

#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>

#include <unistd.h>   // mkdtemp (test)

#include "gui_line_history.hpp"
#include "test.hpp"

namespace wry::gui {

    namespace {

        bool begins_with(StringView text, StringView prefix) {
            return text.chars.size() >= prefix.chars.size()
                && std::equal(prefix.chars.begin(), prefix.chars.end(), text.chars.begin());
        }

        // Entries are single lines with something on them.  The editor
        // never produces a control character, but an entry may come from
        // elsewhere.
        bool storable(StringView line) {
            bool blank = true;
            for (char32_t c : line) {
                if (c < 0x20 || c == 0x7F)
                    return false;
                if (c != U' ')
                    blank = false;
            }
            return !blank;
        }

        // Every well-formed line of the file, oldest first; how many
        // lines the file has; and whether its last line lacks its
        // newline (a writer was cut off in it).
        std::vector<String> read_lines(std::filesystem::path const& path,
                                       std::size_t& count, bool& unterminated) {
            std::vector<String> lines;
            count = 0;
            unterminated = false;
            std::FILE* file = std::fopen(path.c_str(), "rb");
            if (!file)
                return lines;
            std::string bytes;
            char buffer[4096];
            for (std::size_t n; (n = std::fread(buffer, 1, sizeof(buffer), file)) != 0; )
                bytes.append(buffer, n);
            std::fclose(file);
            std::size_t first = 0;
            while (first < bytes.size()) {
                std::size_t last = bytes.find('\n', first);
                if (last == std::string::npos) {
                    last = bytes.size();
                    unterminated = true;
                }
                ++count;
                const char* a = bytes.data() + first;
                const char* b = bytes.data() + last;
                if (utf8::isvalid(a, b)) {
                    StringView line(trusted, a, b);
                    if (storable(line))
                        lines.emplace_back(line);
                }
                first = last + 1;
            }
            return lines;
        }

        void write_lines(std::filesystem::path const& path,
                         std::vector<String> const& lines,
                         const char* mode) {
            std::FILE* file = std::fopen(path.c_str(), mode);
            if (!file)
                return;
            for (String const& line : lines) {
                std::fwrite(line.chars.begin(), 1, line.chars.size(), file);
                std::fputc('\n', file);
            }
            // Closing hands the bytes to the kernel; nothing waits for
            // the disk.
            std::fclose(file);
        }

    } // namespace

    void LineHistory::add(StringView line) {
        if (storable(line) && (_entries.empty() || _entries.back() != line)) {
            if (_entries.size() == capacity)
                _entries.erase(_entries.begin());
            _entries.emplace_back(line);
            if (!_path.empty())
                write_lines(_path, {_entries.back()}, "ab");
        }
        _index = _entries.size();
        _draft.clear();
        _shown.clear();
    }

    std::optional<String> LineHistory::older(StringView current) {
        // Not walking, or the recalled line was edited: this line is the
        // draft, and the walk starts from the newest entry.
        if (!_walking() || _shown != current) {
            _index = _entries.size();
            _draft = current;
        }
        for (std::size_t i = _index; i-- > 0; ) {
            // Skip entries that would not change the line: history holds
            // repeats that are not adjacent.
            if (begins_with(_entries[i], _draft) && _entries[i] != current) {
                _index = i;
                _shown = _entries[i];
                return _shown;
            }
        }
        // Nothing older: stay where the walk is.
        return std::nullopt;
    }

    std::optional<String> LineHistory::newer(StringView current) {
        // Not walking, or walking from a line since edited: there is
        // nothing newer than what is being typed.
        if (!_walking() || _shown != current) {
            _index = _entries.size();
            return std::nullopt;
        }
        for (std::size_t i = _index + 1; i < _entries.size(); ++i) {
            if (begins_with(_entries[i], _draft) && _entries[i] != current) {
                _index = i;
                _shown = _entries[i];
                return _shown;
            }
        }
        // Past the newest: back to the draft.
        _index = _entries.size();
        _shown.clear();
        return _draft;
    }

    StringView LineHistory::suggest(StringView prefix) const {
        if (!prefix.empty())
            for (std::size_t i = _entries.size(); i-- > 0; )
                if (_entries[i].chars.size() > prefix.chars.size()
                    && begins_with(_entries[i], prefix))
                    return _entries[i];
        return StringView();
    }

    void LineHistory::attach(std::filesystem::path path) {
        std::size_t count = 0;
        bool unterminated = false;
        std::vector<String> lines = read_lines(path, count, unterminated);
        if (lines.size() > capacity)
            lines.erase(lines.begin(), lines.end() - (std::ptrdiff_t)capacity);
        // End a line a writer was cut off in, so that the next entry
        // appended does not run into it.
        if (unterminated)
            write_lines(path, {String()}, "ab");
        // Prune the front once the file is twice what is kept.  A rename
        // replaces it whole or not at all; an entry another process
        // appends in between is lost, which is within the contract.
        if (count > 2 * capacity) {
            std::filesystem::path temporary = path;
            temporary += ".tmp";
            write_lines(temporary, lines, "wb");
            std::error_code ec;
            std::filesystem::rename(temporary, path, ec);
        }
        // What was entered before the file was attached stays newest.
        lines.insert(lines.end(),
                     std::make_move_iterator(_entries.begin()),
                     std::make_move_iterator(_entries.end()));
        if (lines.size() > capacity)
            lines.erase(lines.begin(), lines.end() - (std::ptrdiff_t)capacity);
        _entries = std::move(lines);
        _index = _entries.size();
        _draft.clear();
        _shown.clear();
        _path = std::move(path);
    }

    // ----------------------------------------------------------------
    // Tests.

    namespace {

        // An assertion whose operand has effects the test depends on:
        // the operand is evaluated in every build (assert's is not).
        void must(bool ok) {
            assert(ok);
            (void) ok;
        }

    } // namespace

    define_test("gui_line_history")
    {
        LineHistory h;
        must(!h.older("") && !h.newer(""));
        require(h.suggest("x").empty());

        // Blank lines and immediate repeats are not recorded.
        h.add("step 1");
        h.add("");
        h.add("   ");
        h.add("step 1");
        h.add("pause");
        h.add("step 2");
        h.add("resume");
        require(h.entries().size() == 4);

        // A plain walk visits everything and returns to the (empty) draft.
        must(*h.older("") == StringView("resume"));
        must(*h.older("resume") == StringView("step 2"));
        must(*h.older("step 2") == StringView("pause"));
        must(*h.older("pause") == StringView("step 1"));
        must(!h.older("step 1"));
        must(*h.newer("step 1") == StringView("pause"));
        must(*h.newer("pause") == StringView("step 2"));
        must(*h.newer("step 2") == StringView("resume"));
        must(h.newer("resume")->empty());
        must(!h.newer(""));

        // The draft survives the walk, and is the search prefix.
        must(*h.older("st") == StringView("step 2"));
        must(*h.older("step 2") == StringView("step 1"));
        must(!h.older("step 1"));
        must(*h.newer("step 1") == StringView("step 2"));
        must(*h.newer("step 2") == StringView("st"));

        // A draft that matches nothing stays put, and leaves no walk.
        must(!h.older("zzz"));
        must(!h.newer("zzz"));

        // Editing a recalled line starts a new walk from the edited line;
        // the entry itself is untouched.
        must(*h.older("") == StringView("resume"));
        must(*h.older("resume") == StringView("step 2"));
        must(!h.newer("step 22"));              // edited: nothing newer
        must(!h.older("step 22"));              // and nothing begins so
        must(*h.older("pa") == StringView("pause"));
        must(*h.newer("pause") == StringView("pa"));
        require(h.entries()[2] == StringView("step 2"));

        // An entry equal to the line is skipped: with "pause" typed, Up
        // finds nothing new to show.
        must(!h.older("pause"));

        // Adding ends a walk.
        must(*h.older("") == StringView("resume"));
        h.add("help");
        must(!h.newer("resume"));
        must(*h.older("") == StringView("help"));
        h.add("help");

        // Suggestion: the newest entry that extends the prefix.
        require(h.suggest("st") == StringView("step 2"));
        require(h.suggest("step 1") .empty());
        require(h.suggest("").empty());
        require(h.suggest("q").empty());

        co_return;
    };

    define_test("gui_line_history_file")
    {
        char directory_template[] = "/tmp/wry-history-XXXXXX";
        const char* made = mkdtemp(directory_template);
        require(made);
        const std::filesystem::path directory(made);
        const std::filesystem::path path = directory / "history.txt";

        // No file yet: attaching is fine, and entries start one.
        {
            LineHistory h;
            h.add("before attach");
            h.attach(path);
            h.add("one");
            h.add("two");
            require(h.entries().size() == 3);
        }
        // "before attach" was never written; the others were.
        {
            LineHistory h;
            h.attach(path);
            require(h.entries().size() == 2);
            require(h.entries()[0] == StringView("one"));
            require(h.entries()[1] == StringView("two"));
            must(*h.older("") == StringView("two"));
            h.add("three");
        }
        // Garbage is skipped: a line of invalid UTF-8, a line with a
        // control character, an empty line.  A last line cut short is
        // kept as it is, and ended, so the next entry gets its own line.
        {
            std::FILE* file = std::fopen(path.c_str(), "ab");
            require(file);
            std::fputs("bad \xC3\x28\n" "tab\there\n" "\n" "four\n" "cut sho", file);
            std::fclose(file);
            LineHistory h;
            h.attach(path);
            require(h.entries().size() == 5);
            require(h.entries()[2] == StringView("three"));
            require(h.entries()[3] == StringView("four"));
            require(h.entries()[4] == StringView("cut sho"));
            h.add("five");
            LineHistory again;
            again.attach(path);
            require(again.entries().size() == 6);
            require(again.entries()[4] == StringView("cut sho"));
            require(again.entries()[5] == StringView("five"));
        }
        // Past twice the capacity the file is cut back to the newest
        // `capacity` entries.
        {
            std::FILE* file = std::fopen(path.c_str(), "wb");
            require(file);
            const std::size_t written = 2 * LineHistory::capacity + 5;
            for (std::size_t i = 0; i != written; ++i)
                std::fprintf(file, "entry %zu\n", i);
            std::fclose(file);
            LineHistory h;
            h.attach(path);
            require(h.entries().size() == LineHistory::capacity);
            require(h.entries().back() == StringView("entry 2004"));
            require(h.entries().front() == StringView("entry 1005"));
            std::size_t count = 0;
            bool unterminated = false;
            std::vector<String> lines = read_lines(path, count, unterminated);
            require(count == LineHistory::capacity && !unterminated);
            require(lines.front() == StringView("entry 1005"));
            (void) written;
        }

        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
        (void) made;
        co_return;
    };

} // namespace wry::gui
