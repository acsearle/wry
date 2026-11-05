# Line editing

The console's input line is a `gui::LineEditor`
(`render/gui_line_editor.{hpp,cpp}`): a line of UTF-8 text and an
insertion point.  The class is platform-neutral and owns no painting, so
any future text field (a save name, a server address) can hold one.
`ConsoleOverlay` (gui.mm) owns submission, history recall, the cancel
key and the painting.

This is the behaviour usually just called "line editing".  The two
reference implementations are GNU readline (every Unix shell prompt;
Emacs key bindings) and the platform text field (the Cocoa text system
on macOS, which also honours the Emacs control keys).  We take the union
of the two, as they do not conflict.

## 1. The model

- The text is a sequence.  The cursor is a POSITION BETWEEN two elements
  (or at either end), not an element: n characters have n + 1 cursor
  positions.  The on-screen mark is properly the "caret" or "insertion
  point"; it is drawn as a bar between two glyphs.  (A terminal's block
  cursor sits ON a character and means "before this one".)
- Typing inserts before the cursor, which ends up after what was typed.
- Backspace erases the element before the cursor; Delete (forward
  delete) the one after it.
- Motion and erasure share their units: character, word, line.  Every
  erase is "erase what the same motion would cross", so there is one
  boundary function per unit and direction (`left_of_cursor`,
  `right_of_cursor`) and everything else is built on those.

Storage is one `String` plus a byte offset that always lies on a code
point boundary.  Insert and erase are O(length), which for one line is
nothing.  The classic structure for large texts is the GAP BUFFER: the
text is kept in two pieces with the free space at the cursor, so typing
is O(1) and moving the cursor copies characters across the gap.  The
console's previous rotating buffer was a gap buffer with the two pieces
joined end to end in a ring and no record of where the line started,
which is why it could move the cursor but could only show the line
rotated.

## 2. Keys

| Key | Effect |
|---|---|
| Left / Right, Ctrl-B / Ctrl-F | move one character |
| Alt-Left / Alt-Right (Ctrl-arrows too) | move one word |
| Cmd-Left / Cmd-Right, Home / End, Ctrl-A / Ctrl-E | move to the start / end of the line |
| Backspace, Ctrl-H | erase the character before the cursor |
| Delete, Ctrl-D | erase the character after the cursor |
| Alt-Backspace / Alt-Delete (Ctrl too) | kill the word before / after |
| Ctrl-W | kill the blank-delimited word before |
| Cmd-Backspace, Ctrl-U | kill to the start of the line |
| Ctrl-K | kill to the end of the line |
| Ctrl-Y | yank (reinsert what was killed) |
| Enter | submit |
| Up / Down, Ctrl-P / Ctrl-N | recall older / newer input |
| Ctrl-C | cancel the pending command |
| Escape, the toggle-console key | close the console |

Alt is Option on macOS.  Ctrl-arrows and Ctrl-Backspace are the Windows
and Linux spellings of word motion; on macOS the system usually takes
Ctrl-arrows for itself.

None of these are rebindable (settings_and_keybindings.md: navigation
keys are not actions).

## 3. Corner cases, and what was chosen

**What a "character" is.**  Three candidates: the byte, the code point,
the grapheme cluster (what a reader perceives: `e` + combining acute,
a flag, an emoji with a skin tone are each one cluster of several code
points).  The right answer is the cluster, by Unicode's text
segmentation rules (UAX #29), which need property tables.  We step by
code point.  The atlas font draws no combining marks and the platform
layer has no input method, so nothing typed can be a cluster today.
`step_left` / `step_right` in the .cpp are the only functions that would
change.

**What a "word" is.**  Two definitions are in use and both are kept.
`word`: a run of letters, digits and `_` (`toggle-console` is two
words, `Coordinate(-2,` contains two); used by Alt-arrows and
Alt-Backspace.  `blank_word`: a run of anything but spaces; used by
Ctrl-W, as in a terminal.  Code points outside ASCII count as letters.

**Where word motion lands.**  Both directions are "cross the
separators, then cross the run": leftward lands on the START of the
previous word, rightward on the END of the next.  That is the macOS and
readline rule.  Windows lands on the start of the next word going
right.

**Kill and yank.**  Erasing a character just erases.  Erasing a word or
to a line end KILLS: the text goes to a kill buffer and Ctrl-Y puts it
back.  Consecutive kills accumulate into one buffer in text order, so
Ctrl-W three times then Ctrl-Y restores three words; any other
operation in between starts a new buffer.  This is the only undo there
is.  readline keeps a ring of old kills (Alt-Y cycles); we keep one.

**Modifiers and text.**  A keystroke with Ctrl or Cmd is a command and
never inserts, even though the platform reports text for it (Cmd-V
arrives with the text "v").  Alt is NOT filtered: on macOS it is how
characters are typed (Option-3 is `#` on a British keyboard).  The cost
is that readline's Alt-B / Alt-F / Alt-D cannot be bound; the arrow
forms cover them.

**What may be inserted.**  Control characters are dropped, so a line
never holds a newline or a tab.  The event's text buffer is 12 bytes and
could in principle cut a UTF-8 sequence short; such text is dropped
rather than stored.

**A line wider than its field.**  The line scrolls sideways by the least
amount that keeps the caret inside the field with a margin of context on
both sides, and never so far that the end of the text leaves the right
edge (so erasing at the end scrolls back).  The scroll offset is paint
state in the overlay, not editor state.

**The caret blink.**  Any keystroke restarts the blink in its visible
phase, so the caret is solid while typing or moving and only blinks at
rest.

**History recall.**  Recall replaces the whole line and puts the cursor
at its end.  Submitting an empty line records nothing; an immediate
repeat is recorded once.

## 4. Not done

1. **Selection.**  A second position, the ANCHOR; the selection is the
   range between anchor and cursor.  Shift with any motion extends it,
   any unshifted motion collapses it (Left to its left end, Right to its
   right end, without moving further), typing or Backspace replaces it,
   Cmd-A selects all.  Shift is ignored today.
2. **Clipboard.**  Cmd-C / X / V need a platform hook (NSPasteboard
   lives behind the delegate).  Paste policy for a single line is a
   decision: flatten newlines to spaces, stop at the first one, or
   submit line by line.
3. **Undo.**  The kill buffer is the substitute.
4. **Mouse.**  Click to place the cursor (hit-testing glyph midpoints),
   drag to select, double-click for a word.
5. **Input methods and dead keys.**  The delegate reads
   `NSEvent.characters` directly; composing text (Option-E then E, CJK
   input) needs `NSTextInputClient` and a "marked text" range in the
   editor.
6. **Bidirectional text.**  With right-to-left scripts the logical order
   (storage) and visual order differ, and Left means "visually left".
   Out of scope with this font.

## 5. Next: history and completion

**History.**  Session recall exists (`_recall`).  What conventional
history adds:

- the DRAFT survives: going Up from a half-typed line and coming back
  Down restores it (today it is lost);
- edits to a recalled entry do not alter the stored entry;
- prefix search: with `st` typed, Up visits only entries starting `st`
  (fish, and readline's history-search-backward);
- incremental search, Ctrl-R;
- persistence across launches, with a size cap.

**Completion and suggestion.**  Two different features:

- COMPLETION (Tab): replace the token before the cursor with its only
  completion, or its longest common prefix, and list the candidates if
  several remain.  The completion context is `before()`, parsed by the
  command grammar: first token completes against `registry` names;
  later tokens against the parameter's type (enum words such as
  `OPCODE::`, `true` / `false`, sub-command words like `ssao`'s modes).
- AUTOSUGGESTION (fish): show the most likely whole line in grey after
  the cursor, accepted with Right or End at the end of the line.  The
  source is usually history first, then completion.

Both need only `before()`, `insert()` and a way for the overlay to paint
ghost text after the caret; the registry already knows every command's
parameters and their types (commands.md section 5).
