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

        // The nearest boundary at or before byte offset i, clamped to the
        // line.
        std::size_t snap(StringView s, std::size_t i) {
            i = std::min(i, (std::size_t)s.chars.size());
            while (i > 0 && i < s.chars.size()
                   && ((unsigned char)s.chars.begin()[i] & 0xC0) == 0x80)
                --i;
            return i;
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

    StringView LineEditor::selected() const {
        return StringView(trusted,
                          _text.chars.begin() + selection_first(),
                          _text.chars.begin() + selection_last());
    }

    // ---- Whole-line operations.

    void LineEditor::_reset(StringView text) {
        String clean;
        for (char32_t c : text)
            if (!is_control(c))
                clean.push_back(c);
        _text = std::move(clean);
        _cursor = _anchor = _text.chars.size();
        _chaining = false;
        _undo.clear();
        _redo.clear();
        _last_change = Change::none;
    }

    void LineEditor::set(StringView text) {
        _reset(text);
    }

    String LineEditor::take() {
        String line(std::move(_text));
        _reset(StringView());
        return line;
    }

    void LineEditor::clear() {
        _reset(StringView());
    }

    // ---- Motion.

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

    void LineEditor::move_left(Unit unit, bool extend) {
        if (!extend && has_selection()) {
            _cursor = selection_first();
            if (unit != Unit::character)
                _cursor = left_of_cursor(unit);
        } else {
            _cursor = left_of_cursor(unit);
        }
        if (!extend)
            _anchor = _cursor;
        _chaining = false;
        _last_change = Change::none;
    }

    void LineEditor::move_right(Unit unit, bool extend) {
        if (!extend && has_selection()) {
            _cursor = selection_last();
            if (unit != Unit::character)
                _cursor = right_of_cursor(unit);
        } else {
            _cursor = right_of_cursor(unit);
        }
        if (!extend)
            _anchor = _cursor;
        _chaining = false;
        _last_change = Change::none;
    }

    void LineEditor::move_to(std::size_t offset, bool extend) {
        _cursor = snap(_text, offset);
        if (!extend)
            _anchor = _cursor;
        _chaining = false;
        _last_change = Change::none;
    }

    void LineEditor::select_all() {
        _anchor = 0;
        _cursor = _text.chars.size();
        _chaining = false;
        _last_change = Change::none;
    }

    void LineEditor::select_word_at(std::size_t offset) {
        const std::size_t n = _text.chars.size();
        std::size_t first = snap(_text, offset);
        if (n == 0)
            return;
        // The character clicked is the one after the offset; at the end
        // of the line, the last one.
        if (first == n)
            first = step_left(_text, first);
        std::size_t last = step_right(_text, first);
        const char32_t c = code_point_at(_text, first);
        // A word or a run of spaces grows to the whole run; any other
        // mark stands alone.
        auto grow = [&](auto in_run) {
            while (first > 0 && in_run(code_point_at(_text, step_left(_text, first))))
                first = step_left(_text, first);
            while (last < n && in_run(code_point_at(_text, last)))
                last = step_right(_text, last);
        };
        if (in_word(c))
            grow(in_word);
        else if (c == U' ')
            grow([](char32_t x) { return x == U' '; });
        _anchor = first;
        _cursor = last;
        _chaining = false;
        _last_change = Change::none;
    }

    // ---- Editing.

    // Remember the present state as one to undo back to, unless this
    // change continues the previous one.  Any new change invalidates
    // what was undone.
    void LineEditor::_checkpoint(Change kind, bool joins_previous) {
        if (!(joins_previous && kind == _last_change)) {
            // Bounded: the oldest states go first.
            if (_undo.size() == 256)
                _undo.erase(_undo.begin());
            _undo.push_back(State{_text, _cursor, _anchor});
        }
        _redo.clear();
        _last_change = kind;
    }

    // Replace [first, last) by `text`, which holds no control characters
    // and is not a view of _text.
    void LineEditor::_splice(std::size_t first, std::size_t last, StringView text) {
        assert(first <= last && last <= _text.chars.size());
        _text.chars.erase(_text.chars.begin() + first, _text.chars.begin() + last);
        _text.chars.insert(_text.chars.begin() + first,
                           text.chars.begin(), text.chars.end());
        _cursor = _anchor = first + text.chars.size();
    }

    void LineEditor::insert(StringView text) {
        // Filter into a copy first: `text` may view our own storage.
        String clean;
        std::size_t count = 0;
        char32_t typed = 0;
        for (char32_t c : text) {
            if (!is_control(c)) {
                clean.push_back(c);
                typed = c;
                ++count;
            }
        }
        if (clean.empty() && !has_selection())
            return;
        // One character over no selection is typing.  A run of typing is
        // one undo step, until a word starts after a space.
        if (count == 1 && !has_selection()) {
            const bool starts_word = _typed_space && typed != U' ';
            _checkpoint(Change::typing, !starts_word);
            _typed_space = (typed == U' ');
        } else {
            _checkpoint(Change::other, false);
        }
        _splice(selection_first(), selection_last(), clean);
        _chaining = false;
    }

    void LineEditor::replace(std::size_t first, std::size_t last, StringView text) {
        assert(snap(_text, first) == first && snap(_text, last) == last);
        String clean;
        for (char32_t c : text)
            if (!is_control(c))
                clean.push_back(c);
        _checkpoint(Change::other, false);
        _splice(first, last, clean);
        _chaining = false;
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
        _checkpoint(Change::other, false);
        _splice(first, last, StringView());
        _chaining = true;
    }

    void LineEditor::erase_left(Unit unit) {
        if (has_selection()) {
            _checkpoint(Change::other, false);
            _splice(selection_first(), selection_last(), StringView());
            _chaining = false;
            return;
        }
        const std::size_t first = left_of_cursor(unit);
        if (unit != Unit::character) {
            _kill(first, _cursor, true);
            return;
        }
        _chaining = false;
        if (first == _cursor)
            return;
        _checkpoint(Change::erasing, true);
        _splice(first, _cursor, StringView());
    }

    void LineEditor::erase_right(Unit unit) {
        if (has_selection()) {
            _checkpoint(Change::other, false);
            _splice(selection_first(), selection_last(), StringView());
            _chaining = false;
            return;
        }
        const std::size_t last = right_of_cursor(unit);
        if (unit != Unit::character) {
            _kill(_cursor, last, false);
            return;
        }
        _chaining = false;
        if (last == _cursor)
            return;
        _checkpoint(Change::erasing, true);
        _splice(_cursor, last, StringView());
    }

    void LineEditor::yank() {
        // insert() copies before it writes and _killed is not _text, so
        // passing the buffer as a view is safe.
        insert(_killed);
    }

    // ---- Undo.

    bool LineEditor::undo() {
        if (_undo.empty())
            return false;
        _redo.push_back(State{std::move(_text), _cursor, _anchor});
        State& state = _undo.back();
        _text = std::move(state.text);
        _cursor = state.cursor;
        _anchor = state.anchor;
        _undo.pop_back();
        _chaining = false;
        _last_change = Change::none;
        return true;
    }

    bool LineEditor::redo() {
        if (_redo.empty())
            return false;
        _undo.push_back(State{std::move(_text), _cursor, _anchor});
        State& state = _redo.back();
        _text = std::move(state.text);
        _cursor = state.cursor;
        _anchor = state.anchor;
        _redo.pop_back();
        _chaining = false;
        _last_change = Change::none;
        return true;
    }

    // ---- Clipboard.

    void LineEditor::copy() const {
        if (_clipboard && _clipboard->write && has_selection())
            _clipboard->write(selected());
    }

    void LineEditor::cut() {
        if (!_clipboard || !_clipboard->write || !has_selection())
            return;
        _clipboard->write(selected());
        _checkpoint(Change::other, false);
        _splice(selection_first(), selection_last(), StringView());
        _chaining = false;
    }

    void LineEditor::paste() {
        if (!_clipboard || !_clipboard->read)
            return;
        String flat;
        for (char32_t c : _clipboard->read())
            flat.push_back((c == U'\n' || c == U'\r' || c == U'\t') ? U' ' : c);
        insert(flat);
    }

    // ---- Keys.

    bool LineEditor::on_key(Event const& e) {
        if (e.kind != WryEventKindKeyDown)
            return false;

        const bool shift = e.mods.has(Modifiers::Shift);
        const bool ctrl = e.mods.has(Modifiers::Ctrl);
        const bool alt = e.mods.has(Modifiers::Alt);
        const bool cmd = e.mods.has(Modifiers::Cmd);

        // The modifier on an arrow or an erase key picks the unit; Shift
        // on a motion key extends the selection.
        const Unit unit = cmd ? Unit::line
                        : (alt || ctrl) ? Unit::word
                        : Unit::character;

        switch (e.key) {
            case key::ArrowLeft:  move_left(unit, shift);        return true;
            case key::ArrowRight: move_right(unit, shift);       return true;
            case key::Home:       move_left(Unit::line, shift);  return true;
            case key::End:        move_right(Unit::line, shift); return true;
            case key::Backspace:  erase_left(unit);              return true;
            case key::Delete:     erase_right(unit);             return true;
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

        if (cmd && !ctrl && !alt) {
            switch (e.key) {
                case 'a': select_all();                  return true;
                case 'z': shift ? redo() : undo();       return true;
                case 'c': copy();                        return true;
                case 'x': cut();                         return true;
                case 'v': paste();                       return true;
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

        // An assertion whose operand has effects the test depends on:
        // the operand is evaluated in every build (assert's is not).
        void must(bool ok) {
            assert(ok);
            (void) ok;
        }

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
            require(ed.empty() && ed.cursor() == 0);
            type(ed, "abc");
            require(ed.text() == StringView("abc") && ed.cursor() == 3);
            ed.move_left(Unit::character);
            type(ed, "X");
            require(ed.text() == StringView("abXc") && ed.cursor() == 3);
            require(ed.before() == StringView("abX") && ed.after() == StringView("c"));

            // Backspace takes what is before, Delete what is after.
            ed.erase_left(Unit::character);
            require(ed.text() == StringView("abc") && ed.cursor() == 2);
            ed.erase_right(Unit::character);
            require(ed.text() == StringView("ab") && ed.cursor() == 2);

            // Both are no-ops at the end they point at.
            ed.erase_right(Unit::character);
            ed.move_left(Unit::line);
            ed.erase_left(Unit::character);
            ed.move_left(Unit::character);
            require(ed.text() == StringView("ab") && ed.cursor() == 0);
            ed.move_right(Unit::line);
            ed.move_right(Unit::character);
            require(ed.cursor() == 2);

            // take() empties the line.
            String line = ed.take();
            require(line == StringView("ab") && ed.empty() && ed.cursor() == 0);
        }

        // The cursor moves and erases by whole code points:
        // a (1 byte), e-acute (2), euro (3), grinning face (4), b (1).
        {
            LineEditor ed;
            ed.set("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80" "b");
            require(ed.cursor() == 11);
            const std::size_t stops[] = { 10, 6, 3, 1, 0, 0 };
            for (std::size_t stop : stops) {
                ed.move_left(Unit::character);
                require(ed.cursor() == stop);
                (void) stop;
            }
            const std::size_t back[] = { 1, 3, 6, 10, 11, 11 };
            for (std::size_t stop : back) {
                ed.move_right(Unit::character);
                require(ed.cursor() == stop);
                (void) stop;
            }
            ed.move_left(Unit::character);
            ed.erase_left(Unit::character);     // the face, all four bytes
            require(ed.text() == StringView("a\xC3\xA9\xE2\x82\xAC" "b"));
            require(ed.cursor() == 6);
            ed.move_left(Unit::line);
            ed.move_right(Unit::character);
            ed.erase_right(Unit::character);    // e-acute
            require(ed.text() == StringView("a\xE2\x82\xAC" "b"));
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
                require(ed.cursor() == stop);
                (void) stop;
            }
            const std::size_t ends[] = { 7, 18, 21, 25, 26, 26 };
            for (std::size_t stop : ends) {
                ed.move_right(Unit::word);
                require(ed.cursor() == stop);
                (void) stop;
            }
            // A blank-delimited word swallows the punctuation.
            ed.move_left(Unit::blank_word);
            require(ed.cursor() == 23);
            ed.move_left(Unit::blank_word);
            require(ed.cursor() == 8);
            ed.move_right(Unit::blank_word);
            require(ed.cursor() == 22);

            // A hyphenated name is two words but one blank word.
            ed.set("toggle-console");
            ed.move_left(Unit::word);
            require(ed.cursor() == 7);
            ed.move_right(Unit::line);
            ed.move_left(Unit::blank_word);
            require(ed.cursor() == 0);
        }

        // Kills accumulate while they are consecutive, in text order,
        // from either side; a yank restores them.
        {
            LineEditor ed;
            ed.set("one two three four");
            ed.erase_left(Unit::blank_word);
            ed.erase_left(Unit::blank_word);
            require(ed.text() == StringView("one two "));
            ed.yank();
            require(ed.text() == StringView("one two three four"));

            // Erasing a character is not a kill and breaks the chain.
            ed.erase_left(Unit::blank_word);        // "four"
            ed.erase_left(Unit::character);         // " "
            ed.erase_left(Unit::blank_word);        // "three" replaces "four"
            require(ed.text() == StringView("one two "));
            ed.yank();
            require(ed.text() == StringView("one two three"));

            // Left of the cursor then right of it: one buffer.
            ed.set("alpha beta gamma");
            ed.move_left(Unit::word);
            ed.move_left(Unit::character);          // between "beta" and " gamma"
            ed.erase_left(Unit::line);              // "alpha beta"
            ed.erase_right(Unit::line);             // " gamma"
            require(ed.empty());
            ed.yank();
            require(ed.text() == StringView("alpha beta gamma"));
        }

        // Insertion drops control characters, and may be handed the
        // editor's own text.
        {
            LineEditor ed;
            ed.insert("a\tb\nc\x7F" "d");
            require(ed.text() == StringView("abcd"));
            ed.move_left(Unit::character);
            ed.move_left(Unit::character);
            ed.insert(ed.text());
            require(ed.text() == StringView("ababcdcd") && ed.cursor() == 6);
        }

        // Key bindings.
        {
            const uint16_t ctrl = Modifiers::Ctrl;
            const uint16_t alt = Modifiers::Alt;
            const uint16_t cmd = Modifiers::Cmd;

            LineEditor ed;
            type(ed, "hold OPCODE::SWAP");
            must(ed.on_key(key_event(key::ArrowLeft, alt)));
            require(ed.cursor() == 13);
            must(ed.on_key(key_event(key::ArrowLeft, cmd)));
            require(ed.cursor() == 0);
            must(ed.on_key(key_event(key::ArrowRight, ctrl)));
            require(ed.cursor() == 4);
            must(ed.on_key(key_event(key::End)));
            require(ed.cursor() == 17);
            must(ed.on_key(key_event(key::Home)));
            require(ed.cursor() == 0);
            must(ed.on_key(key_event('e', ctrl)));
            require(ed.cursor() == 17);
            must(ed.on_key(key_event('a', ctrl)));
            must(ed.on_key(key_event('f', ctrl)));
            require(ed.cursor() == 1);
            must(ed.on_key(key_event('b', ctrl)));
            require(ed.cursor() == 0);
            must(ed.on_key(key_event('d', ctrl)));
            require(ed.text() == StringView("old OPCODE::SWAP"));
            must(ed.on_key(key_event(key::Delete, alt)));
            require(ed.text() == StringView(" OPCODE::SWAP"));
            must(ed.on_key(key_event('e', ctrl)));
            must(ed.on_key(key_event(key::Backspace, alt)));
            require(ed.text() == StringView(" OPCODE::"));
            must(ed.on_key(key_event('w', ctrl)));
            require(ed.text() == StringView(" "));
            // The two kills were consecutive, so the yank returns both.
            must(ed.on_key(key_event('y', ctrl)));
            require(ed.text() == StringView(" OPCODE::SWAP"));
            must(ed.on_key(key_event('h', ctrl)));
            require(ed.text() == StringView(" OPCODE::SWA"));
            must(ed.on_key(key_event(key::Backspace, cmd)));
            require(ed.empty());
            type(ed, "abcd");
            ed.move_left(Unit::character);
            ed.move_left(Unit::character);
            must(ed.on_key(key_event('k', ctrl)));
            require(ed.text() == StringView("ab"));
            must(ed.on_key(key_event('u', ctrl)));
            require(ed.empty());

            // Chords are not text: Cmd-Q arrives with the text "q".
            must(!ed.on_key(key_event('q', cmd, "q")));
            must(!ed.on_key(key_event('q', ctrl)));
            require(ed.empty());
            // Alt text is: Alt-A types a-ring on a US layout.
            must(ed.on_key(key_event('a', alt, "\xC3\xA5")));
            require(ed.text() == StringView("\xC3\xA5"));
            // A sequence the event buffer cut short is dropped, not stored.
            must(ed.on_key(key_event(key::Unknown, 0, "\xE2\x82")));
            require(ed.text() == StringView("\xC3\xA5"));

            // The owner's keys and other event kinds are not ours.
            must(!ed.on_key(key_event(key::Enter)));
            must(!ed.on_key(key_event(key::ArrowUp)));
            must(!ed.on_key(key_event(key::Tab)));
            must(!ed.on_key(key_event(key::Escape)));
            Event up = key_event('x', 0, "x");
            up.kind = WryEventKindKeyUp;
            must(!ed.on_key(up));
            (void) ctrl; (void) alt; (void) cmd;
        }

        co_return;
    };

    define_test("gui_line_editor_selection")
    {
        using Unit = LineEditor::Unit;
        const uint16_t shift = Modifiers::Shift;
        const uint16_t alt = Modifiers::Alt;
        const uint16_t cmd = Modifiers::Cmd;

        LineEditor ed;
        ed.set("hold OPCODE::SWAP");
        require(!ed.has_selection() && ed.selected().empty());

        // Shift with a motion leaves the anchor behind.
        must(ed.on_key(key_event(key::ArrowLeft, shift | alt)));
        require(ed.anchor() == 17 && ed.cursor() == 13);
        require(ed.selected() == StringView("SWAP"));
        must(ed.on_key(key_event(key::ArrowLeft, shift)));
        require(ed.selected() == StringView(":SWAP"));
        // ... and moving back past the anchor turns the selection round.
        ed.move_right(Unit::line, true);
        require(!ed.has_selection());
        ed.move_left(Unit::line, true);
        require(ed.selected() == StringView("hold OPCODE::SWAP"));
        require(ed.selection_first() == 0 && ed.selection_last() == 17);

        // An unshifted character move collapses to that end and stops.
        ed.move_to(5);
        ed.move_to(11, true);
        require(ed.selected() == StringView("OPCODE"));
        ed.move_left(Unit::character);
        require(!ed.has_selection() && ed.cursor() == 5);
        ed.move_to(11, true);
        ed.move_right(Unit::character);
        require(!ed.has_selection() && ed.cursor() == 11);
        // A larger move continues from that end.
        ed.move_to(5);
        ed.move_to(11, true);
        ed.move_left(Unit::word);
        require(!ed.has_selection() && ed.cursor() == 0);

        // Typing replaces the selection; so do Backspace and Delete,
        // whatever their unit.
        ed.move_to(5);
        ed.move_to(11, true);
        type(ed, "X");
        require(ed.text() == StringView("hold X::SWAP") && ed.cursor() == 6);
        ed.move_to(4, true);
        must(ed.on_key(key_event(key::Backspace, alt)));
        require(ed.text() == StringView("hold::SWAP") && ed.cursor() == 4);
        ed.move_to(6, true);
        must(ed.on_key(key_event(key::Delete)));
        require(ed.text() == StringView("holdSWAP"));

        // Select all.
        must(ed.on_key(key_event('a', cmd, "a")));
        require(ed.selected() == StringView("holdSWAP"));
        require(ed.text() == StringView("holdSWAP"));

        // move_to clamps, and snaps into a code point's start.
        ed.set("a\xE2\x82\xAC" "b");
        ed.move_to(2);
        require(ed.cursor() == 1);
        ed.move_to(3);
        require(ed.cursor() == 1);
        ed.move_to(99);
        require(ed.cursor() == 5);

        // A double click takes the word, the spaces, or the one mark.
        //      0         1
        //      01234567890123456
        ed.set("hold  OPCODE::SWAP");
        ed.select_word_at(2);
        require(ed.selected() == StringView("hold"));
        ed.select_word_at(4);
        require(ed.selected() == StringView("  "));
        ed.select_word_at(8);
        require(ed.selected() == StringView("OPCODE"));
        ed.select_word_at(12);
        require(ed.selected() == StringView(":"));
        ed.select_word_at(18);      // the end: the last word
        require(ed.selected() == StringView("SWAP"));
        ed.clear();
        ed.select_word_at(0);
        require(!ed.has_selection());

        (void) shift; (void) alt; (void) cmd;
        co_return;
    };

    define_test("gui_line_editor_undo")
    {
        using Unit = LineEditor::Unit;
        const uint16_t shift = Modifiers::Shift;
        const uint16_t cmd = Modifiers::Cmd;

        LineEditor ed;
        must(!ed.undo() && !ed.redo());

        // Typing is undone a word at a time: each step ends where a new
        // word began.
        type(ed, "one two three");
        must(ed.undo());
        require(ed.text() == StringView("one two "));
        must(ed.undo());
        require(ed.text() == StringView("one "));
        must(ed.undo());
        require(ed.empty());
        must(!ed.undo());
        // ... and redone the same way, until a new edit.
        must(ed.redo() && ed.redo());
        require(ed.text() == StringView("one two ") && ed.cursor() == 8);
        type(ed, "x");
        must(!ed.redo());
        require(ed.text() == StringView("one two x"));

        // A run of Backspaces is one step; moving the cursor ends a run.
        ed.erase_left(Unit::character);
        ed.erase_left(Unit::character);
        ed.erase_left(Unit::character);
        require(ed.text() == StringView("one tw"));
        ed.move_left(Unit::character);
        ed.erase_left(Unit::character);
        require(ed.text() == StringView("one w"));
        must(ed.undo());
        require(ed.text() == StringView("one tw") && ed.cursor() == 5);
        must(ed.undo());
        require(ed.text() == StringView("one two x") && ed.cursor() == 9);

        // Erasing nothing is not a step.
        ed.erase_right(Unit::character);
        must(ed.undo());
        require(ed.text() == StringView("one two "));

        // A kill, a replaced selection and a replace() are each a step,
        // and undo brings the selection back.
        ed.set("alpha beta");
        ed.erase_left(Unit::word);
        require(ed.text() == StringView("alpha "));
        ed.move_to(0);
        ed.move_to(5, true);
        type(ed, "A");
        require(ed.text() == StringView("A "));
        ed.replace(0, 1, "omega");
        require(ed.text() == StringView("omega ") && ed.cursor() == 5);
        must(ed.on_key(key_event('z', cmd, "z")));
        require(ed.text() == StringView("A "));
        must(ed.on_key(key_event('z', cmd, "z")));
        require(ed.text() == StringView("alpha ") && ed.selected() == StringView("alpha"));
        must(ed.on_key(key_event('z', cmd, "z")));
        require(ed.text() == StringView("alpha beta") && ed.cursor() == 10);
        must(ed.on_key(key_event('z', cmd | shift, "Z")));
        require(ed.text() == StringView("alpha "));

        // A new line has nothing to undo.
        ed.set("recalled");
        must(!ed.undo() && !ed.redo());
        type(ed, "!");
        (void) ed.take();
        must(!ed.undo());

        (void) shift; (void) cmd;
        co_return;
    };

    define_test("gui_line_editor_clipboard")
    {
        const uint16_t cmd = Modifiers::Cmd;

        // Without a clipboard the keys are still editing keys, and inert.
        LineEditor ed;
        ed.set("abc");
        ed.select_all();
        must(ed.on_key(key_event('x', cmd, "x")));
        must(ed.on_key(key_event('v', cmd, "v")));
        require(ed.text() == StringView("abc"));

        Clipboard clipboard;
        ed.set_clipboard(&clipboard);

        // Copy and cut take the selection; with none they leave the
        // clipboard alone.
        clipboard.write("kept");
        ed.move_to(0);
        must(ed.on_key(key_event('c', cmd, "c")));
        must(ed.on_key(key_event('x', cmd, "x")));
        require(clipboard.read() == StringView("kept"));
        ed.move_to(1);
        ed.move_to(3, true);
        must(ed.on_key(key_event('c', cmd, "c")));
        require(clipboard.read() == StringView("bc") && ed.text() == StringView("abc"));
        ed.move_to(0);
        ed.move_to(1, true);
        must(ed.on_key(key_event('x', cmd, "x")));
        require(clipboard.read() == StringView("a") && ed.text() == StringView("bc"));

        // Paste inserts at the cursor or over the selection, as one undo
        // step, with line breaks and tabs flattened to spaces.
        ed.move_to(1);
        must(ed.on_key(key_event('v', cmd, "v")));
        require(ed.text() == StringView("bac") && ed.cursor() == 2);
        clipboard.write("one\ntwo\tthree\r\n");
        ed.select_all();
        must(ed.on_key(key_event('v', cmd, "v")));
        require(ed.text() == StringView("one two three  "));
        must(ed.undo());
        require(ed.text() == StringView("bac"));

        (void) cmd;
        co_return;
    };

    // Differential test: random edits against the obvious model, a vector
    // of code points with a cursor and an anchor indexing it.
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
        std::size_t at = 0;         // the cursor, as an index into model
        std::size_t from = 0;       // the anchor

        // Remove the model's selection, if any; true if there was one.
        auto erase_selection = [&] {
            if (at == from)
                return false;
            const std::size_t first = std::min(at, from);
            model.erase(first, std::max(at, from) - first);
            at = from = first;
            return true;
        };

        for (int step = 0; step != 6000; ++step) {
            switch (random(12)) {
                case 0: case 1: case 2: {
                    char32_t c = alphabet[random(alphabet_size)];
                    String one;
                    one.push_back(c);
                    ed.insert(one);
                    erase_selection();
                    model.insert(model.begin() + at, c);
                    from = ++at;
                    break;
                }
                case 3:
                    ed.move_left(Unit::character);
                    if (at != from) at = std::min(at, from);
                    else if (at > 0) --at;
                    from = at;
                    break;
                case 4:
                    ed.move_right(Unit::character);
                    if (at != from) at = std::max(at, from);
                    else if (at < model.size()) ++at;
                    from = at;
                    break;
                case 5:
                    ed.erase_left(Unit::character);
                    if (!erase_selection() && at > 0) {
                        model.erase(--at, 1);
                        from = at;
                    }
                    break;
                case 6:
                    ed.erase_right(Unit::character);
                    if (!erase_selection() && at < model.size())
                        model.erase(at, 1);
                    break;
                case 7:
                    // Rarely, so lines grow long between resets.
                    if (random(8) == 0) {
                        if (random(2)) { ed.move_left(Unit::line);  at = 0; }
                        else           { ed.move_right(Unit::line); at = model.size(); }
                        from = at;
                    }
                    break;
                case 8:
                    if (random(100) == 0) {
                        ed.clear();
                        model.clear();
                        at = from = 0;
                    }
                    break;
                case 9:
                    ed.move_left(Unit::character, true);
                    if (at > 0) --at;
                    break;
                case 10:
                    ed.move_right(Unit::character, true);
                    if (at < model.size()) ++at;
                    break;
                case 11:
                    // An edit and its undo are the identity, selection
                    // included; so are an undo and its redo.
                    if (random(4) == 0) {
                        ed.insert("zz");
                        const bool undone = ed.undo();
                        require(undone);
                        (void) undone;
                        if (ed.undo()) {
                            const bool redone = ed.redo();
                            require(redone);
                            (void) redone;
                        }
                    }
                    break;
            }

            // Same text, and the cursor and the anchor are after the same
            // numbers of code points.
            String expected;
            std::size_t cursor_bytes = 0;
            std::size_t anchor_bytes = 0;
            for (std::size_t i = 0; i != model.size(); ++i) {
                if (i == at) cursor_bytes = expected.chars.size();
                if (i == from) anchor_bytes = expected.chars.size();
                expected.push_back(model[i]);
            }
            if (at == model.size()) cursor_bytes = expected.chars.size();
            if (from == model.size()) anchor_bytes = expected.chars.size();
            require(ed.text() == StringView(expected));
            require(ed.cursor() == cursor_bytes);
            require(ed.anchor() == anchor_bytes);
        }

        co_return;
    };

} // namespace wry::gui
