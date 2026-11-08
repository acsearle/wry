# Line editing

The console's input line is a `gui::LineEditor`
(`render/gui_line_editor.{hpp,cpp}`): a line of UTF-8 text, an insertion
point and a selection.  Behind it sits a `gui::LineHistory`
(`render/gui_line_history.{hpp,cpp}`): the lines submitted, with recall,
search and a file.  Both are platform-neutral and own no painting, so
any future text field (a save name, a server address) can hold an
editor.  `ConsoleOverlay` (gui.mm) owns what is the console's:
submission, paste, completion, the suggestion.  What shows is a widget
tree of views onto those (`gui_widget.hpp`): a `ConsoleView` holding a
`TextLog` of the output over a `LineField`, the generic one-line field
that paints an editor (prompt, text, selection, suggestion, caret,
sideways scroll) and takes the mouse and the keys the console leaves to
the editor.

This is the behaviour usually just called "line editing".  The
reference implementations are GNU readline (every Unix shell prompt;
Emacs key bindings), the platform text field (the Cocoa text system on
macOS, which also honours the Emacs control keys), and for history
search and suggestions the fish shell.  We take the union, as they do
not conflict.

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
- A second position, the ANCHOR, stays behind when the cursor moves with
  Shift held.  The SELECTION is the text between anchor and cursor,
  whichever comes first.  With nothing selected the anchor is the
  cursor.  Every edit replaces or removes the selection if there is one.

Storage is one `String` plus two byte offsets that always lie on code
point boundaries.  Insert and erase are O(length), which for one line is
nothing.  The classic structure for large texts is the GAP BUFFER: the
text is kept in two pieces with the free space at the cursor, so typing
is O(1) and moving the cursor copies characters across the gap.  The
console's original rotating buffer was a gap buffer with the two pieces
joined end to end in a ring and no record of where the line started,
which is why it could move the cursor but could only show the line
rotated.

## 2. Keys

The editor's (`LineEditor::on_key`):

| Key | Effect |
|---|---|
| Left / Right, Ctrl-B / Ctrl-F | move one character |
| Alt-Left / Alt-Right (Ctrl-arrows too) | move one word |
| Cmd-Left / Cmd-Right, Home / End, Ctrl-A / Ctrl-E | move to the start / end of the line |
| Shift with any arrow, Home or End | the same move, extending the selection |
| Cmd-A | select all |
| Backspace, Ctrl-H | erase the character before the cursor |
| Delete, Ctrl-D | erase the character after the cursor |
| Alt-Backspace / Alt-Delete (Ctrl too) | kill the word before / after |
| Ctrl-W | kill the blank-delimited word before |
| Cmd-Backspace, Ctrl-U | kill to the start of the line |
| Ctrl-K | kill to the end of the line |
| Ctrl-Y | yank (reinsert what was killed) |
| Cmd-Z / Shift-Cmd-Z | undo / redo |
| Cmd-C / Cmd-X / Cmd-V | copy / cut / paste |

The console's (`ConsoleOverlay`):

| Key | Effect |
|---|---|
| Enter | submit |
| Up / Down, Ctrl-P / Ctrl-N | walk the history |
| Tab | complete the word before the cursor |
| Right, End, Ctrl-F, Ctrl-E at the end of the line | accept the grey suggestion |
| Alt-Right at the end of the line | accept one word of it |
| Cmd-V | paste, submitting at each line break |
| Ctrl-C | cancel the pending command |
| Escape, the toggle-console key | close the console |
| click, Shift-click, drag on the input line | place the cursor, extend, select |
| double click, triple click | select the word, the line |

(The mouse rows are `LineField`'s, which also consumes the release of
any press it took, so a double click on the line does not fall through
to the palette under it.)

Alt is Option on macOS.  Ctrl-arrows and Ctrl-Backspace are the Windows
and Linux spellings of word motion; on macOS the system usually takes
Ctrl-arrows for itself.  Cmd is the only spelling of copy, paste, undo
and select-all so far: on Windows and Linux those are Ctrl-C / V / Z /
A, which collide with cancel and the Emacs keys, and the port will have
to choose.

None of these are rebindable (settings_and_keybindings.md: navigation
keys are not actions).

## 3. Corner cases, and what was chosen

### 3.1 Text and motion

**What a "character" is.**  Three candidates: the byte, the code point,
the grapheme cluster (what a reader perceives: `e` + combining acute,
a flag, an emoji with a skin tone are each one cluster of several code
points).  The right answer is the cluster, by Unicode's text
segmentation rules (UAX #29), which need property tables.  We step by
code point.  The atlas font draws printable ASCII only and the platform
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

**Modifiers and text.**  A keystroke with Ctrl or Cmd is a command and
never inserts, even though the platform reports text for it (Cmd-Q
arrives with the text "q").  Alt is NOT filtered: on macOS it is how
characters are typed (Option-3 is `#` on a British keyboard).  The cost
is that readline's Alt-B / Alt-F / Alt-D cannot be bound; the arrow
forms cover them.

**What may be inserted.**  Control characters are dropped, so a line
never holds a newline or a tab.  The event's text buffer is 12 bytes and
could in principle cut a UTF-8 sequence short; such text is dropped
rather than stored.

### 3.2 Selection

**Collapsing.**  An unshifted move with a selection collapses it.  A
character move lands on the selection's end in that direction and goes
no further (Left on a selection puts the cursor at its left end, not
one further left).  A word or line move continues from that end.

**Which end moves.**  Shift-moves move the cursor, never the anchor, so
extending past the anchor turns the selection round rather than growing
it both ways.

**Erasing.**  With a selection, Backspace and Delete in any unit erase
exactly the selection.  That is not a kill: it does not touch the kill
buffer.

**Double click.**  Takes the word under the pointer; on a run of spaces,
the run; on any other mark, that one mark.  At the end of the line, the
last word.  A third click takes the line.  Clicks count as repeats when
they land on the same cursor position within 400 ms.

**Clicks land on the nearest cursor position**, so the left half of a
glyph puts the cursor before it and the right half after it.  The
positions are those of the last painted frame.

**The mouse and what is behind the console.**  The console still lets
clicks through to the world, except those that start on its input line;
a drag begun there is the console's until release, wherever the pointer
goes.

### 3.3 Kill, yank and undo

**Kill and yank.**  Erasing a character just erases.  Erasing a word or
to a line end KILLS: the text goes to a kill buffer and Ctrl-Y puts it
back.  Consecutive kills accumulate into one buffer in text order, so
Ctrl-W three times then Ctrl-Y restores three words; any other
operation in between starts a new buffer.  readline keeps a ring of old
kills (Alt-Y cycles); we keep one.  The kill buffer is NOT the
clipboard: the two are separate in a terminal and in Cocoa, and here.

**Undo granularity.**  Undo steps back through states of (text, cursor,
anchor), so undoing also restores the selection an edit replaced.  What
counts as one step decides how it feels:

- a run of typing is one step until a new word starts (a non-space
  typed after a space), so typed text comes off a word at a time;
- a run of single-character erasures is one step;
- moving the cursor ends either run;
- every other edit (a kill, a paste, a yank, a replaced selection, a
  completion) is its own step.

An edit after an undo discards what could have been redone: history is
linear, as everywhere but Emacs.  256 steps are kept.

**Undo's scope is the line.**  Submitting, clearing, and recalling a
history entry each start a new line with nothing to undo.

### 3.4 Clipboard and paste

**The clipboard is plain text** (`gui::Clipboard`, gui_clipboard.hpp):
read the text if what is there has a text form, write text.  On macOS
that is the pasteboard's string type; an application that copies styled
text almost always offers the plain form alongside.  Only the windowed
host is given the system clipboard.  Headless and offscreen runs and
tests keep a private in-process one, so a script never reads or
overwrites what the user copied.

**Copy and cut need a selection.**  With none they do nothing (some
editors take the whole line; a terminal does not).

**Pasting several lines.**  The console treats pasted text as typed
text: it goes in at the cursor, over the selection if there is one, and
each line break is an Enter.  A pasted script therefore runs line by
line, each line queued behind the last (commands.md section 8), and
whatever follows the final line break stays on the input line
unsubmitted.  CR LF counts once.  Tabs become spaces.  Blank lines and
`#` comment lines do nothing.  The first pasted line joins whatever was
already typed.  A plain `LineEditor::paste` (a future text field)
cannot submit, so it flattens line breaks to spaces instead.

### 3.5 History

**The draft survives.**  The line being typed when a walk starts is
kept, and comes back when Down passes the newest entry.

**The draft is the search term.**  With `st` typed, Up visits only
entries that begin `st` (fish, and readline's history-search-backward).
An empty draft visits everything.  Entries that would not change the
line are skipped, so non-adjacent repeats do not stall the walk.

**Recalled lines are copies.**  Editing one never alters the entry.  An
edited recalled line becomes a draft in its own right: Up searches with
it, and Down has nothing newer to go to.  (readline instead keeps the
edit attached to the entry until the next submission; simpler to
explain this way.)

**What is recorded.**  Lines as typed, whether or not the command
succeeded.  Not blank lines, and not an immediate repeat of the newest
entry.  The newest 1000.

**The file is best effort.**  `config/console_history.txt`, one entry
per line, attached by the windowed host only.  It is read once at
launch; each new entry is appended with one open-write-close and no
sync to disk.  Nothing is transactional: a crash can lose the last
entries, two instances appending at once interleave whole lines, and a
line cut short by a crash stays as a truncated entry (the next launch
ends it, so later entries do not run into it).  When a launch
finds more than 2000 lines the file is rewritten (to a temporary, then
renamed over) with the newest 1000.  An instance already running keeps
its own in-memory history regardless.

### 3.6 Completion and suggestion

Two features that are easily confused:

**Completion (Tab)** acts on the word before the cursor and is asked
for.  The candidates come from the registry (commands.md section 5).
Tab replaces the word with as much as all candidates agree on; a single
candidate is completed whole and followed by a space (not after
`name=`, and not if a space already follows).  If that adds nothing,
the candidates are listed in the output.  If there are none, the
command's signature is printed, once.  This is bash's behaviour, minus
its "press Tab twice to list".  Matching ignores case and the
completion corrects it.  A completion is one undo step.

**Suggestion (the grey text)** is offered unasked, for the whole line,
only while the cursor is at the end of a non-empty line with nothing
selected.  It is the rest of the newest history entry that begins with
the line; failing that, what Tab would add to the word: as much as all
of its candidates agree on, so `f` shows `lip-` for `flip-horizontal`
and `flip-vertical`, and the whole word if there is only one.  It only
ever continues the word as spelled; a completion that would correct
letter case is left to Tab.  A partial suggestion looks the same as a
whole one.  It is never part of the text: Enter submits
what was typed.  Right, End, Ctrl-F or Ctrl-E accept it (those keys
have nowhere else to go at the end of a line); Alt-Right accepts one
word.  This is fish's autosuggestion.

**No popup.**  A menu of candidates under the cursor (an IDE's, or
zsh's menu-select) needs its own widget, keyboard focus rules and a
place to draw over the scrollback.  The list in the output costs
nothing and scrolls with everything else.

### 3.7 Painting

**A line wider than its field.**  The line scrolls sideways by the least
amount that keeps the caret inside the field with a margin of context on
both sides, and never so far that the end of the text leaves the right
edge (so erasing at the end scrolls back).  The scroll offset is paint
state in the overlay, not editor state.

**The caret blink.**  Any keystroke or click restarts the blink in its
visible phase, so the caret is solid while typing or moving and only
blinks at rest.

## 4. Not done

1. **Input methods and dead keys.**  The delegate reads
   `NSEvent.characters` directly, so Option-E then E does not compose
   and there is no CJK input.  It needs `NSTextInputClient` on the view
   and a "marked text" range in the editor.  Pointless until the atlas
   font has glyphs beyond ASCII: the composed characters would be stored
   and submitted but drawn as nothing.
2. **Grapheme clusters**, for the same reason (3.1).
3. **Bidirectional text.**  With right-to-left scripts the logical order
   (storage) and visual order differ, and Left means "visually left".
4. **Incremental history search** (Ctrl-R).  The prefix walk covers most
   of its use.
5. **A candidate popup** (3.6).
6. **A kill ring** (3.3).
7. **Scrolling the output** with the wheel or PageUp.
8. **Windows and Linux key spellings** for copy, paste, undo and
   select-all (section 2).

## 5. Testing

Unit tests: `gui_line_editor*` (including a randomized differential
test against a vector-of-code-points model with cursor and anchor),
`gui_line_history*` (the file in a temporary directory),
`command_completion`, `gui_context_console_editing` (the console end to
end: Tab, the suggestion, walks, paste).

By eye: an offscreen instance driven through the socket,

```
client --offscreen --no-tests --skip-splash --control-socket $TMPDIR/x.sock
wryctl toggle-console; wryctl type '"tog"'; wryctl key Tab
wryctl key Shift+Alt+Left; wryctl click 60 230; wryctl screenshot /path.png
```

The socket path must fit `sockaddr_un` (104 bytes, less the `.tmp` the
listener appends).  What cannot be driven this way is the platform edge:
real Cmd chords arriving through `-keyDown:`, and the system
pasteboard.
