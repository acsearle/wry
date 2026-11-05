//
//  gui_line_editor.hpp
//  client
//
//  Created by Antony Searle on 2026-11-05.
//
//  Single-line text editing: a line of text, an insertion point and a
//  selection.  Conventions, corner cases and what is left out:
//  render/docs/line_editing.md.
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
//      line -- and each erase is "erase what the same motion would cross";
//    - a second position, the ANCHOR, stays behind when the cursor moves
//      with Shift held.  The text between the two is the selection, and
//      typing or erasing replaces it.
//
//  The text is UTF-8; the cursor and the anchor are byte offsets that
//  always lie on a code point boundary.  The unit of motion called
//  "character" here is one code point; gui_line_editor.cpp says what
//  that leaves out.
//
//  Platform-neutral: the key bindings are read from wry::gui::Event.
//

#ifndef gui_line_editor_hpp
#define gui_line_editor_hpp

#include <algorithm>
#include <cstddef>
#include <vector>

#include "gui_clipboard.hpp"
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

        // The selection is the text between the anchor and the cursor,
        // whichever comes first.  With nothing selected the anchor is
        // the cursor.
        std::size_t anchor() const { return _anchor; }
        bool has_selection() const { return _anchor != _cursor; }
        std::size_t selection_first() const { return std::min(_anchor, _cursor); }
        std::size_t selection_last() const { return std::max(_anchor, _cursor); }
        StringView selected() const;

        // ---- Whole-line operations.  Each starts a new line as far as
        // undo is concerned.

        // Replace the line; the cursor goes to the end (history recall).
        void set(StringView text);

        // Remove and return the line (submission).
        String take();

        void clear();

        // ---- Motion.

        // Where the cursor would land after moving one unit.  At an end
        // of the line the answer is the cursor itself.
        std::size_t left_of_cursor(Unit) const;
        std::size_t right_of_cursor(Unit) const;

        // Move the cursor.  With `extend` the anchor stays, which grows
        // or shrinks the selection.  Without it the selection collapses:
        // a character move lands on the selection's end in that
        // direction and goes no further; a larger move continues from
        // that end.
        void move_left(Unit, bool extend = false);
        void move_right(Unit, bool extend = false);

        // Move the cursor to a byte offset (a mouse click or drag),
        // clamped to the line and to a code point boundary.
        void move_to(std::size_t offset, bool extend = false);

        void select_all();

        // Select the word, the run of spaces or the punctuation mark at
        // `offset` (a double click).
        void select_word_at(std::size_t offset);

        // ---- Editing.  With a selection, every one of these replaces or
        // removes the selection instead.

        // Insert at the cursor, leaving the cursor after the insertion.
        // Control characters are dropped: a line holds no newlines or
        // tabs.
        void insert(StringView text);

        // Replace bytes [first, last) and leave the cursor after the
        // replacement (completion).  Both must be code point boundaries.
        void replace(std::size_t first, std::size_t last, StringView text);

        // Erase what the same move would cross.  Erasing a character just
        // erases; erasing any larger unit KILLS: the text goes to the
        // kill buffer, from which yank() reinserts it.  Consecutive kills
        // accumulate into one buffer in text order, so three Ctrl-Ws
        // followed by a yank restore all three words.
        void erase_left(Unit);
        void erase_right(Unit);

        // Insert the kill buffer at the cursor.
        void yank();

        // ---- Undo.
        //
        // Every edit can be undone, and an undone edit redone until the
        // next new edit.  A run of typing is undone a word at a time, a
        // run of single-character erasures all at once; every other edit
        // is its own step.  Undo restores the cursor and the selection
        // along with the text.  False if there is nothing to undo / redo.
        bool undo();
        bool redo();

        // ---- Clipboard.  All three do nothing until a clipboard is set.
        // The clipboard is separate from the kill buffer, as it is in a
        // terminal.

        void set_clipboard(Clipboard const* clipboard) { _clipboard = clipboard; }

        // Copy the selection; cut copies it and erases it.  Nothing
        // selected: nothing happens.
        void copy() const;
        void cut();

        // Insert the clipboard's text.  A line cannot hold a line break,
        // so each newline or tab becomes a space.  (An owner with a use
        // for the lines -- the console submits them -- reads the
        // clipboard itself.)
        void paste();

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
        //   Shift with any arrow, Home, End   the same, extending the
        //                                     selection
        //   Cmd-A                             select all
        //   Backspace, Ctrl-H                 erase character left
        //   Delete, Ctrl-D                    erase character right
        //   Alt-Backspace / Alt-Delete        kill word left / right
        //   Ctrl-W                            kill blank-delimited word left
        //   Cmd-Backspace, Ctrl-U             kill to the start of the line
        //   Ctrl-K                            kill to the end of the line
        //   Ctrl-Y                            yank
        //   Cmd-Z / Shift-Cmd-Z               undo / redo
        //   Cmd-C / Cmd-X / Cmd-V             copy / cut / paste
        //   anything with text, without
        //   Ctrl or Cmd                       insert
        //
        // Enter, Escape, Tab, Up and Down are the owner's (submission,
        // dismissal, completion, history).
        bool on_key(Event const&);

    private:

        // What the previous operation was, for grouping undo steps.
        enum class Change { none, typing, erasing, other };

        struct State {
            String text;
            std::size_t cursor = 0;
            std::size_t anchor = 0;
        };

        void _reset(StringView text);
        void _checkpoint(Change kind, bool joins_previous);
        void _splice(std::size_t first, std::size_t last, StringView text);
        void _kill(std::size_t first, std::size_t last, bool leftward);

        String _text;
        std::size_t _cursor = 0;
        std::size_t _anchor = 0;

        String _killed;
        bool _chaining = false;         // the previous operation was a kill

        std::vector<State> _undo;       // states to return to, oldest first
        std::vector<State> _redo;       // states undone, most recent last
        Change _last_change = Change::none;
        bool _typed_space = false;      // the last character typed was a space

        Clipboard const* _clipboard = nullptr;

    };

} // namespace wry::gui

#endif /* gui_line_editor_hpp */
