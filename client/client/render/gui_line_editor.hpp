//
//  gui_line_editor.hpp
//  client
//
//  Created by Antony Searle on 2026-11-05.
//
//  Single-line text editing: a line of text and an insertion point.
//
//  The model is the one every text field and every terminal line editor
//  (GNU readline, the Cocoa text system) shares:
//
//    - the text is a sequence; the cursor is a POSITION BETWEEN two
//      elements of it (or at either end), not an element.  n characters
//      have n + 1 cursor positions;
//    - typing inserts before the cursor, which then sits after what was
//      typed;
//    - Backspace erases the element before the cursor, Delete the one
//      after it;
//    - motion and erasure come in the same units -- character, word,
//      line -- and each erase is "erase what the same motion would cross".
//
//  The text is UTF-8 and the cursor is a byte offset that always lies on
//  a code point boundary.  The unit of motion called "character" here is
//  one code point; gui_line_editor.cpp says what that leaves out.
//
//  Platform-neutral: the key bindings are read from wry::gui::Event.
//

#ifndef gui_line_editor_hpp
#define gui_line_editor_hpp

#include <cstddef>

#include "gui_event.hpp"
#include "string.hpp"

namespace wry::gui {

    class LineEditor {
    public:

        // Units of motion and erasure.
        //
        //   character   one code point
        //   word        a run of letters, digits and '_' (everything else
        //               separates): `toggle-console` is two words
        //   blank_word  a run of anything but spaces: `Coordinate(-2,` is
        //               one.  Ctrl-W's unit in a terminal
        //   line        everything on that side of the cursor
        enum class Unit { character, word, blank_word, line };

        // ---- Reading.

        StringView text() const { return _text; }
        bool empty() const { return _text.empty(); }

        // The cursor, as a byte offset into text(): 0 is before the first
        // character, text().chars.size() after the last.
        std::size_t cursor() const { return _cursor; }

        // The text on either side of the cursor.  before() is what a
        // renderer measures to place the caret, and what completion
        // completes.
        StringView before() const;
        StringView after() const;

        // ---- Whole-line operations.

        // Replace the line; the cursor goes to the end (history recall).
        void set(StringView text);

        // Remove and return the line (submission).
        String take();

        void clear();

        // ---- Editing.

        // Insert at the cursor, leaving the cursor after the insertion.
        // Control characters are dropped: a line holds no newlines or
        // tabs.
        void insert(StringView text);

        // Where the cursor would land after moving one unit.  At an end
        // of the line the answer is the cursor itself.
        std::size_t left_of_cursor(Unit) const;
        std::size_t right_of_cursor(Unit) const;

        void move_left(Unit unit)  { _cursor = left_of_cursor(unit);  _chaining = false; }
        void move_right(Unit unit) { _cursor = right_of_cursor(unit); _chaining = false; }

        // Erase what the same move would cross.  Erasing a character just
        // erases; erasing any larger unit KILLS: the text goes to the
        // kill buffer, from which yank() reinserts it.  Consecutive kills
        // accumulate into one buffer in text order, so three Ctrl-Ws
        // followed by a yank restore all three words.
        void erase_left(Unit);
        void erase_right(Unit);

        // Insert the kill buffer at the cursor.
        void yank();

        // ---- Keys.

        // Apply a key-down event; true if it was an editing key (the text
        // or cursor may still be unchanged, as for Left at the start of
        // the line).  Bindings are the union of the macOS text-field
        // keys and the Emacs / readline control keys that every Cocoa
        // text field also honours:
        //
        //   Left / Right, Ctrl-B / Ctrl-F     character
        //   Alt-Left / Alt-Right              word       (Ctrl-arrows too:
        //                                                 Windows / Linux)
        //   Cmd-Left / Cmd-Right, Home / End,
        //   Ctrl-A / Ctrl-E                   line
        //   Backspace, Ctrl-H                 erase character left
        //   Delete, Ctrl-D                    erase character right
        //   Alt-Backspace / Alt-Delete        kill word left / right
        //   Ctrl-W                            kill blank-delimited word left
        //   Cmd-Backspace, Ctrl-U             kill to the start of the line
        //   Ctrl-K                            kill to the end of the line
        //   Ctrl-Y                            yank
        //   anything with text, without
        //   Ctrl or Cmd                       insert
        //
        // Enter, Escape, Tab, Up and Down are the owner's (submission,
        // dismissal, completion, history).
        bool on_key(Event const&);

    private:

        void _kill(std::size_t first, std::size_t last, bool leftward);

        String _text;
        std::size_t _cursor = 0;

        String _killed;
        bool _chaining = false;     // the previous operation was a kill

    };

} // namespace wry::gui

#endif /* gui_line_editor_hpp */
