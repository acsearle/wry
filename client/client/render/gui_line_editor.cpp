//
//  gui_line_editor.cpp
//  client
//
//  Created by Antony Searle on 2026-11-05.
//

#include <cstring>
#include <string>

#include "gui_line_editor.hpp"
#include "test.hpp"

namespace wry::gui {

    namespace {

        // ---- Boundaries.
        //
        // A cursor is a byte offset on a code point boundary.  What a
        // reader calls a character is a GRAPHEME CLUSTER, which can be
        // several code points: 'e' + U+0301 (combining acute), a flag
        // (two regional indicators), an emoji joined to a skin tone.
        // Stepping by code point puts the cursor inside such a cluster
        // and lets Backspace peel a mark off its base.  Segmenting
        // clusters properly is Unicode's UAX #29 with its property
        // tables; the atlas font draws no combining marks and the
        // platform layer has no input method yet, so nothing typed here
        // can be a cluster.  When that changes, these two functions are
        // the only ones that need to learn about clusters.

        std::size_t step_left(StringView s, std::size_t i) {
            assert(i > 0);
            utf8::iterator it(s.chars.begin() + i);
            --it;
            return (std::size_t)(it.base - s.chars.begin());
        }

        std::size_t step_right(StringView s, std::size_t i) {
            assert(i < s.chars.size());
            utf8::iterator it(s.chars.begin() + i);
            ++it;
            return (std::size_t)(it.base - s.chars.begin());
        }

        char32_t code_point_at(StringView s, std::size_t i) {
            return *utf8::iterator(s.chars.begin() + i);
        }

        // ---- Runs.
        //
        // Word motion in both directions is "cross the separators, then
        // cross the run".  Leftward that lands on the START of the
        // previous word; rightward on the END of the next one.  (The
        // macOS and readline convention.  Windows lands on the start of
        // the next word going right; the asymmetry here is deliberate.)

        template<typename Predicate>
        std::size_t run_left(StringView s, std::size_t i, Predicate in_run) {
            while (i > 0 && !in_run(code_point_at(s, step_left(s, i))))
                i = step_left(s, i);
            while (i > 0 && in_run(code_point_at(s, step_left(s, i))))
                i = step_left(s, i);
            return i;
        }

        template<typename Predicate>
        std::size_t run_right(StringView s, std::size_t i, Predicate in_run) {
            const std::size_t n = s.chars.size();
            while (i < n && !in_run(code_point_at(s, i)))
                i = step_right(s, i);
            while (i < n && in_run(code_point_at(s, i)))
                i = step_right(s, i);
            return i;
        }

        // Letters, digits and underscore.  Everything outside ASCII
        // counts as a letter, which is right for accented and CJK text
        // and wrong for the handful of non-ASCII punctuation marks.
        bool in_word(char32_t c) {
            return (c >= 0x80)
                || (c >= U'0' && c <= U'9')
                || (c >= U'A' && c <= U'Z')
                || (c >= U'a' && c <= U'z')
                || (c == U'_');
        }

        bool in_blank_word(char32_t c) {
            return c != U' ';
        }

        // C0 and C1 controls and DEL: nothing a line can show.
        bool is_control(char32_t c) {
            return (c < 0x20) || (c >= 0x7F && c < 0xA0);
        }

    } // namespace

    StringView LineEditor::before() const {
        return StringView(trusted, _text.chars.begin(), _text.chars.begin() + _cursor);
    }

    StringView LineEditor::after() const {
        return StringView(trusted, _text.chars.begin() + _cursor, _text.chars.end());
    }

    void LineEditor::set(StringView text) {
        clear();
        insert(text);
    }

    String LineEditor::take() {
        String line(std::move(_text));
        clear();
        return line;
    }

    void LineEditor::clear() {
        _text = String();
        _cursor = 0;
        _chaining = false;
    }

    void LineEditor::insert(StringView text) {
        // Filter into a copy first: `text` may view our own storage.
        String clean;
        for (char32_t c : text)
            if (!is_control(c))
                clean.push_back(c);
        _text.chars.insert(_text.chars.begin() + _cursor,
                           clean.chars.begin(), clean.chars.end());
        _cursor += clean.chars.size();
        _chaining = false;
    }

    std::size_t LineEditor::left_of_cursor(Unit unit) const {
        switch (unit) {
            case Unit::character:
                return _cursor ? step_left(_text, _cursor) : 0;
            case Unit::word:
                return run_left(_text, _cursor, in_word);
            case Unit::blank_word:
                return run_left(_text, _cursor, in_blank_word);
            case Unit::line:
                return 0;
        }
        return _cursor;
    }

    std::size_t LineEditor::right_of_cursor(Unit unit) const {
        const std::size_t n = _text.chars.size();
        switch (unit) {
            case Unit::character:
                return (_cursor < n) ? step_right(_text, _cursor) : n;
            case Unit::word:
                return run_right(_text, _cursor, in_word);
            case Unit::blank_word:
                return run_right(_text, _cursor, in_blank_word);
            case Unit::line:
                return n;
        }
        return _cursor;
    }

    void LineEditor::_kill(std::size_t first, std::size_t last, bool leftward) {
        assert(first <= _cursor && _cursor <= last);
        if (first == last)
            return;
        if (!_chaining)
            _killed.clear();
        // Killed text is kept in the order it had in the line, so a
        // leftward kill goes on the front of what is already there.
        const char* a = _text.chars.begin() + first;
        const char* b = _text.chars.begin() + last;
        _killed.chars.insert(leftward ? _killed.chars.begin() : _killed.chars.end(), a, b);
        _text.chars.erase(a, b);
        _cursor = first;
        _chaining = true;
    }

    void LineEditor::erase_left(Unit unit) {
        const std::size_t first = left_of_cursor(unit);
        if (unit != Unit::character) {
            _kill(first, _cursor, true);
            return;
        }
        _text.chars.erase(_text.chars.begin() + first, _text.chars.begin() + _cursor);
        _cursor = first;
        _chaining = false;
    }

    void LineEditor::erase_right(Unit unit) {
        const std::size_t last = right_of_cursor(unit);
        if (unit != Unit::character) {
            _kill(_cursor, last, false);
            return;
        }
        _text.chars.erase(_text.chars.begin() + _cursor, _text.chars.begin() + last);
        _chaining = false;
    }

    void LineEditor::yank() {
        // insert() copies before it writes and _killed is not _text, so
        // passing the buffer as a view is safe.
        insert(_killed);
    }

    bool LineEditor::on_key(Event const& e) {
        if (e.kind != WryEventKindKeyDown)
            return false;

        const bool ctrl = e.mods.has(Modifiers::Ctrl);
        const bool alt = e.mods.has(Modifiers::Alt);
        const bool cmd = e.mods.has(Modifiers::Cmd);

        // The modifier on an arrow or an erase key picks the unit.
        const Unit unit = cmd ? Unit::line
                        : (alt || ctrl) ? Unit::word
                        : Unit::character;

        switch (e.key) {
            case key::ArrowLeft:  move_left(unit);        return true;
            case key::ArrowRight: move_right(unit);       return true;
            case key::Home:       move_left(Unit::line);  return true;
            case key::End:        move_right(Unit::line); return true;
            case key::Backspace:  erase_left(unit);       return true;
            case key::Delete:     erase_right(unit);      return true;
            default:
                break;
        }

        if (ctrl && !alt && !cmd) {
            switch (e.key) {
                case 'a': move_left(Unit::line);         return true;
                case 'e': move_right(Unit::line);        return true;
                case 'b': move_left(Unit::character);    return true;
                case 'f': move_right(Unit::character);   return true;
                case 'h': erase_left(Unit::character);   return true;
                case 'd': erase_right(Unit::character);  return true;
                case 'w': erase_left(Unit::blank_word);  return true;
                case 'u': erase_left(Unit::line);        return true;
                case 'k': erase_right(Unit::line);       return true;
                case 'y': yank();                        return true;
                default:
                    break;
            }
        }

        // Text.  Ctrl and Cmd chords are commands, never text (the
        // platform still reports Cmd-V's characters as "v").  Alt is
        // left alone: on macOS it is how many characters are typed.
        if (!ctrl && !cmd && e.text[0] != '\0') {
            const char* last = e.text + strnlen(e.text, sizeof(e.text));
            // The event's buffer is fixed-size and may have cut a
            // sequence short.
            if (utf8::isvalid(e.text, last))
                insert(StringView(trusted, e.text, last));
            return true;
        }

        return false;
    }

    // ----------------------------------------------------------------
    // Tests.

    namespace {

        Event key_event(uint32_t k, uint16_t mods = 0, const char* text = "") {
            Event e{};
            e.kind = WryEventKindKeyDown;
            e.key = k;
            e.mods.bits = mods;
            std::strncpy(e.text, text, sizeof(e.text) - 1);
            return e;
        }

        void type(LineEditor& editor, const char* text) {
            for (; *text; ++text) {
                const char one[2] = { *text, '\0' };
                editor.on_key(key_event((uint32_t)*text, 0, one));
            }
        }

    } // namespace

    define_test("gui_line_editor")
    {
        using Unit = LineEditor::Unit;

        // Typing inserts before the cursor.
        {
            LineEditor ed;
            assert(ed.empty() && ed.cursor() == 0);
            type(ed, "abc");
            assert(ed.text() == StringView("abc") && ed.cursor() == 3);
            ed.move_left(Unit::character);
            type(ed, "X");
            assert(ed.text() == StringView("abXc") && ed.cursor() == 3);
            assert(ed.before() == StringView("abX") && ed.after() == StringView("c"));

            // Backspace takes what is before, Delete what is after.
            ed.erase_left(Unit::character);
            assert(ed.text() == StringView("abc") && ed.cursor() == 2);
            ed.erase_right(Unit::character);
            assert(ed.text() == StringView("ab") && ed.cursor() == 2);

            // Both are no-ops at the end they point at.
            ed.erase_right(Unit::character);
            ed.move_left(Unit::line);
            ed.erase_left(Unit::character);
            ed.move_left(Unit::character);
            assert(ed.text() == StringView("ab") && ed.cursor() == 0);
            ed.move_right(Unit::line);
            ed.move_right(Unit::character);
            assert(ed.cursor() == 2);

            // take() empties the line.
            String line = ed.take();
            assert(line == StringView("ab") && ed.empty() && ed.cursor() == 0);
        }

        // The cursor moves and erases by whole code points:
        // a (1 byte), e-acute (2), euro (3), grinning face (4), b (1).
        {
            LineEditor ed;
            ed.set("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80" "b");
            assert(ed.cursor() == 11);
            const std::size_t stops[] = { 10, 6, 3, 1, 0, 0 };
            for (std::size_t stop : stops) {
                ed.move_left(Unit::character);
                assert(ed.cursor() == stop);
                (void) stop;
            }
            const std::size_t back[] = { 1, 3, 6, 10, 11, 11 };
            for (std::size_t stop : back) {
                ed.move_right(Unit::character);
                assert(ed.cursor() == stop);
                (void) stop;
            }
            ed.move_left(Unit::character);
            ed.erase_left(Unit::character);     // the face, all four bytes
            assert(ed.text() == StringView("a\xC3\xA9\xE2\x82\xAC" "b"));
            assert(ed.cursor() == 6);
            ed.move_left(Unit::line);
            ed.move_right(Unit::character);
            ed.erase_right(Unit::character);    // e-acute
            assert(ed.text() == StringView("a\xE2\x82\xAC" "b"));
        }

        // Words: left lands on starts, right on ends.
        {
            LineEditor ed;
            //      0         1         2
            //      01234567890123456789012345
            ed.set("pipette Coordinate(-2, -2)");
            const std::size_t starts[] = { 24, 20, 8, 0, 0 };
            for (std::size_t stop : starts) {
                ed.move_left(Unit::word);
                assert(ed.cursor() == stop);
                (void) stop;
            }
            const std::size_t ends[] = { 7, 18, 21, 25, 26, 26 };
            for (std::size_t stop : ends) {
                ed.move_right(Unit::word);
                assert(ed.cursor() == stop);
                (void) stop;
            }
            // A blank-delimited word swallows the punctuation.
            ed.move_left(Unit::blank_word);
            assert(ed.cursor() == 23);
            ed.move_left(Unit::blank_word);
            assert(ed.cursor() == 8);
            ed.move_right(Unit::blank_word);
            assert(ed.cursor() == 22);

            // A hyphenated name is two words but one blank word.
            ed.set("toggle-console");
            ed.move_left(Unit::word);
            assert(ed.cursor() == 7);
            ed.move_right(Unit::line);
            ed.move_left(Unit::blank_word);
            assert(ed.cursor() == 0);
        }

        // Kills accumulate while they are consecutive, in text order,
        // from either side; a yank restores them.
        {
            LineEditor ed;
            ed.set("one two three four");
            ed.erase_left(Unit::blank_word);
            ed.erase_left(Unit::blank_word);
            assert(ed.text() == StringView("one two "));
            ed.yank();
            assert(ed.text() == StringView("one two three four"));

            // Erasing a character is not a kill and breaks the chain.
            ed.erase_left(Unit::blank_word);        // "four"
            ed.erase_left(Unit::character);         // " "
            ed.erase_left(Unit::blank_word);        // "three" replaces "four"
            assert(ed.text() == StringView("one two "));
            ed.yank();
            assert(ed.text() == StringView("one two three"));

            // Left of the cursor then right of it: one buffer.
            ed.set("alpha beta gamma");
            ed.move_left(Unit::word);
            ed.move_left(Unit::character);          // between "beta" and " gamma"
            ed.erase_left(Unit::line);              // "alpha beta"
            ed.erase_right(Unit::line);             // " gamma"
            assert(ed.empty());
            ed.yank();
            assert(ed.text() == StringView("alpha beta gamma"));
        }

        // Insertion drops control characters, and may be handed the
        // editor's own text.
        {
            LineEditor ed;
            ed.insert("a\tb\nc\x7F" "d");
            assert(ed.text() == StringView("abcd"));
            ed.move_left(Unit::character);
            ed.move_left(Unit::character);
            ed.insert(ed.text());
            assert(ed.text() == StringView("ababcdcd") && ed.cursor() == 6);
        }

        // Key bindings.
        {
            const uint16_t ctrl = Modifiers::Ctrl;
            const uint16_t alt = Modifiers::Alt;
            const uint16_t cmd = Modifiers::Cmd;

            LineEditor ed;
            type(ed, "hold OPCODE::SWAP");
            assert(ed.on_key(key_event(key::ArrowLeft, alt)));
            assert(ed.cursor() == 13);
            assert(ed.on_key(key_event(key::ArrowLeft, cmd)));
            assert(ed.cursor() == 0);
            assert(ed.on_key(key_event(key::ArrowRight, ctrl)));
            assert(ed.cursor() == 4);
            assert(ed.on_key(key_event(key::End)));
            assert(ed.cursor() == 17);
            assert(ed.on_key(key_event(key::Home)));
            assert(ed.cursor() == 0);
            assert(ed.on_key(key_event('e', ctrl)));
            assert(ed.cursor() == 17);
            assert(ed.on_key(key_event('a', ctrl)));
            assert(ed.on_key(key_event('f', ctrl)));
            assert(ed.cursor() == 1);
            assert(ed.on_key(key_event('b', ctrl)));
            assert(ed.cursor() == 0);
            assert(ed.on_key(key_event('d', ctrl)));
            assert(ed.text() == StringView("old OPCODE::SWAP"));
            assert(ed.on_key(key_event(key::Delete, alt)));
            assert(ed.text() == StringView(" OPCODE::SWAP"));
            assert(ed.on_key(key_event('e', ctrl)));
            assert(ed.on_key(key_event(key::Backspace, alt)));
            assert(ed.text() == StringView(" OPCODE::"));
            assert(ed.on_key(key_event('w', ctrl)));
            assert(ed.text() == StringView(" "));
            // The two kills were consecutive, so the yank returns both.
            assert(ed.on_key(key_event('y', ctrl)));
            assert(ed.text() == StringView(" OPCODE::SWAP"));
            assert(ed.on_key(key_event('h', ctrl)));
            assert(ed.text() == StringView(" OPCODE::SWA"));
            assert(ed.on_key(key_event(key::Backspace, cmd)));
            assert(ed.empty());
            type(ed, "abcd");
            ed.move_left(Unit::character);
            ed.move_left(Unit::character);
            assert(ed.on_key(key_event('k', ctrl)));
            assert(ed.text() == StringView("ab"));
            assert(ed.on_key(key_event('u', ctrl)));
            assert(ed.empty());

            // Chords are not text: Cmd-V arrives with the text "v".
            assert(!ed.on_key(key_event('v', cmd, "v")));
            assert(!ed.on_key(key_event('q', ctrl)));
            assert(ed.empty());
            // Alt text is: Alt-A types a-ring on a US layout.
            assert(ed.on_key(key_event('a', alt, "\xC3\xA5")));
            assert(ed.text() == StringView("\xC3\xA5"));
            // A sequence the event buffer cut short is dropped, not stored.
            assert(ed.on_key(key_event(key::Unknown, 0, "\xE2\x82")));
            assert(ed.text() == StringView("\xC3\xA5"));

            // The owner's keys and other event kinds are not ours.
            assert(!ed.on_key(key_event(key::Enter)));
            assert(!ed.on_key(key_event(key::ArrowUp)));
            assert(!ed.on_key(key_event(key::Tab)));
            assert(!ed.on_key(key_event(key::Escape)));
            Event up = key_event('x', 0, "x");
            up.kind = WryEventKindKeyUp;
            assert(!ed.on_key(up));
            (void) ctrl; (void) alt; (void) cmd;
        }

        co_return;
    };

    // Differential test: random edits against the obvious model, a vector
    // of code points and an index into it.
    define_test("gui_line_editor_random")
    {
        using Unit = LineEditor::Unit;

        const char32_t alphabet[] = { U'a', U'b', U' ', U'-', U'_', 0xE9, 0x20AC, 0x1F600 };
        const std::size_t alphabet_size = sizeof(alphabet) / sizeof(alphabet[0]);

        uint64_t state = 0x9E3779B97F4A7C15;
        auto random = [&state](uint64_t bound) {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return (state >> 20) % bound;
        };

        LineEditor ed;
        std::u32string model;
        std::size_t at = 0;     // index into model

        for (int step = 0; step != 4000; ++step) {
            switch (random(9)) {
                case 0: case 1: case 2: {
                    char32_t c = alphabet[random(alphabet_size)];
                    String one;
                    one.push_back(c);
                    ed.insert(one);
                    model.insert(model.begin() + at, c);
                    ++at;
                    break;
                }
                case 3:
                    ed.move_left(Unit::character);
                    if (at > 0) --at;
                    break;
                case 4:
                    ed.move_right(Unit::character);
                    if (at < model.size()) ++at;
                    break;
                case 5:
                    ed.erase_left(Unit::character);
                    if (at > 0) model.erase(--at, 1);
                    break;
                case 6:
                    ed.erase_right(Unit::character);
                    if (at < model.size()) model.erase(at, 1);
                    break;
                case 7:
                    // Rarely, so lines grow long between resets.
                    if (random(8) == 0) {
                        if (random(2)) { ed.move_left(Unit::line);  at = 0; }
                        else           { ed.move_right(Unit::line); at = model.size(); }
                    }
                    break;
                case 8:
                    if (random(100) == 0) {
                        ed.clear();
                        model.clear();
                        at = 0;
                    }
                    break;
            }

            // Same text, and the cursor is after the same number of
            // code points.
            String expected, prefix;
            for (std::size_t i = 0; i != model.size(); ++i) {
                expected.push_back(model[i]);
                if (i < at)
                    prefix.push_back(model[i]);
            }
            assert(ed.text() == StringView(expected));
            assert(ed.cursor() == prefix.chars.size());
            assert(ed.before() == StringView(prefix));
        }

        co_return;
    };

} // namespace wry::gui
