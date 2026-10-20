# Commands

Design for the command system: one registry of named, typed commands
that every input path compiles to -- GUI gestures, key bindings, the
drop-down console, a local control socket (scripts, tests, Claude via
MCP) -- and the literal syntax in which commands and their arguments
are written.  Agreed 2026-10-12.  All five stages of section 10 are
implemented (core/command.{hpp,cpp}, game/command_types.{hpp,cpp},
render/gui_context.cpp, the world commands in game/world_state.cpp,
io/control_socket.{hpp,cpp}, the --headless host in core/main.mm and
render/WryDelegate.mm, tools/wryctl, tools/wry_mcp.py,
tools/wry_smoke.py).  Of the section 7 vocabulary, the session commands
other than `quit` (new-game, save, load, list-saves) are not yet
written; the main menu's buttons reach them through `press`.

Status: normative design.  Section 11 lists what remains open.

--------------------------------------------------------------------------

## 1. The model

**Everything the user can do is a command.**  A command is a registry
entry: a stable name, a one-line summary, a list of typed parameters,
a category, and a handler.  The registry is the single source of truth
for what can be done, and every surface is a view of it:

- the console parses a typed line into an invocation and runs it;
- a key binding names a command (the keymap's stable ids *are* command
  names);
- GUI gestures construct invocations instead of acting directly;
- the control socket accepts invocations from outside the process;
- `help` prints the registry; the MCP adapter projects it as tools;
  console completion reads it.

Precedents: Blender (every button is an operator; the Info editor logs
each operator the GUI fires, and that log is how users learn to
script), Emacs (every key runs a named interactive command), the Quake
console (`bind`), and the Chrome DevTools Protocol (one machine-readable
schema from which every client is generated).

**Input is not a command; it is below them.**  Raw events (key down,
mouse move) are what the GUI turns *into* commands.  The system
therefore has two entry points: event injection, which exercises the
GUI exactly as a user would, and command invocation, which bypasses
the GUI.  Tests of the GUI use the first; almost everything else uses
the second.  Event injection is itself exposed as commands (`key`,
`click`, ...; section 7), so both entry points are reachable from every
surface.

**Commands are not a programming language.**  One line is one
command.  There are no variables, no substitution, no expressions, no
control flow.  Loops and conditionals live in the caller -- a Python
script on the socket, a C++ test, Claude.  If an embedded language is
ever truly wanted, we embed a real one (Lua, as Factorio does) rather
than grow this one.

## 2. Categories

Every command has exactly one category, which decides where it runs.
The split follows multiplayer.md section 3: anything that mutates the
World or its clock rides the stream; everything else does not.

- **local**: affects only this process -- view state, GUI, settings,
  queries, screenshots, event injection.  Runs immediately.  Never
  leaves the process.
- **world**: mutates the World.  The handler builds a `Player::Action`
  and calls `Server::submit`; the action is ordered by the server and
  applied when it comes back in a finalized step.  World commands
  inherit the totality obligation of multiplayer.md section 3: a world
  command is valid to *submit* whenever its arguments parse, and its
  effect is decided at application time against whatever the World
  then is.
- **clock**: pause, resume, step.  Addressed to the server role.
  Honored directly only when this process hosts the server (solo, or
  hosting); otherwise `pause` becomes a `PAUSE_VOTE` and `step` is
  refused.
- **session**: new game, save, load, quit.

Inbound traffic from the server (finalized steps) is *applied*, not
dispatched through the registry: it is already-ordered actions, not
requests.  But a `Player::Action` prints as the command line that
would have submitted it (section 6), so logs of the stream are
readable, and desync diffs (multiplayer.md section 5) name actions in
the same language the console speaks.

A world command is parsed from text into a typed `Player::Action`,
and the action's canonical command line is also its wire form: the
body of `ACTION` is that line (multiplayer.md section 7; `format_action`
and `parse_action` in game/command_types.hpp, one reader and one
printer, `read(print(a)) == a`).  The server forwards the bytes
without reading them; a receiver that cannot parse a body applies a
no-op.  A log of the stream therefore reads in the console's own
language with no decoder.

## 3. Origins

An invocation records where it came from:

- `gui` -- constructed by an overlay or the world pump;
- `binding` -- a key binding fired;
- `console` -- typed in the drop-down console;
- `socket` -- arrived on the control socket (with a connection id).

Origin is for logging and for the one policy rule below; handlers do
not branch on it.  A command does the same thing however it is
reached, which is what makes a console line or a socket script a
faithful repro of something done with the mouse.

The policy rule: the control socket exists only when the process is
launched with `--control-socket <path>`; the socket file is created
mode 0600.  There is no TCP listener.  That is the whole security
story for now; world commands are already treated as hostile by the
server, so the exposure is local and clock commands, and the socket is
local-user-only.

## 4. Syntax

Parsing has two stages.  Stage 1 is a fixed, context-free grammar that
produces a syntax tree.  Stage 2 reads each argument node as the type
its parameter declares.  The separation is the difference from Tcl:
there is a real syntax tree before any interpretation, interpretation
is per *type* (one reader per type, registered once) and never per
*command*, and every type has a canonical printer.

### 4.1 Stage 1: tokens and tree

Tokens:

- **word**: a maximal run of characters other than whitespace and the
  delimiters `( ) , = " #`.  `step`, `12`, `-34`, `0x3a80`,
  `OPCODE::SWAP`, `Shift+R`, `toggle-map` are all single words.
- **string**: `"..."` with escapes `\"`, `\\`, `\n`, `\t`, `\r` and
  `\u{X}` (hex scalar value).  Other characters, including non-ASCII
  UTF-8, stand for themselves.  The printer escapes the remaining C0
  controls and DEL as `\u{X}`, so a printed line is always one line of
  visible text.
- **punctuation**: `(`, `)`, `,`, `=`.
- **comment**: `#` outside a string runs to end of line.

Grammar:

```
line  := word arg*                      // the command name, then arguments
arg   := word '=' node                  // named
       | node                           // positional
node  := word
       | string
       | word '(' [node (',' node)*] ')'   // tagged literal, e.g. Coordinate(12, -34)
```

Top-level arguments are separated by whitespace; arguments to a tagged
literal are separated by commas, and may contain whitespace, so
`Coordinate(12, -34)` is one node.  A word followed by `=` in argument
position is a parameter name; a word followed by `(` is a tag.  Both are
decided by the next token, so whitespace around `=` and `(` is
permitted (`at = Coordinate (1, 2)` is `at=Coordinate(1, 2)`), and no
lookahead beyond one token is needed.

Command names and parameter names are kebab-case words matching
`[a-z0-9-]+`, never containing `_`.  A `-` inside a word is harmless
only because there are no expressions; this is one of the places the
no-evaluation rule pays rent.  Excluding `_` makes kebab-to-snake a
bijection: C++ enumerators (as the keymap X-macro already does), and
any future embedded language, spell `toggle-map` as `toggle_map` and
`pipette(at=...)` mechanically, the way Blender maps
`MESH_OT_primitive_cube_add` to `bpy.ops.mesh.primitive_cube_add`.

### 4.2 Stage 2: reading nodes by type

After stage 1, arguments are bound to parameters: named arguments by
name, positional ones in declaration order; a parameter with no
argument takes its default or is an error.  Each node is then read by
its parameter's type.

A **tagged literal** `T(a, b, ...)` is data, not a call: the reader for
type `T` consumes the child nodes.  Readers are pure and total -- no
side effects, no lookups into the World, no allocation visible to
anyone else.  Nothing in argument position ever runs anything.

**Contextual reading** is the one convenience, and its scope is exactly
this: when the parameter's type is known, a word is read in that type's
namespace.

- A parameter of type `OPCODE` reads `SWAP` as `OPCODE::SWAP`.
- A parameter of type `String` reads a word as its own text, so
  `save name=probe` works.
- A parameter of type `KeyCombo` reads `Shift+R` through the keymap's
  existing combo parser.

When the parameter is `Term` (the top type), there is no expected
type, so the node must describe itself: `null`, `true`, `false`, an
integer, a quoted string, a qualified enumerator (`OPCODE::SWAP`,
`MATTER::...`), or a tagged literal (`EntityID(123)`).  A bare
unqualified word in `Term` context is an error.  The error may
*suggest* candidates ("did you mean OPCODE::SWAP?"); it never picks
one.

**Inference is from the expected type, never from global uniqueness.**
This is the stopping point on the slope, and the reason is structural:
inference from uniqueness breaks old scripts and old history the day
anyone adds a second enum containing `SWAP`; inference from context
cannot be broken by an unrelated addition.  Swift's implicit member
expressions (`.swap` resolved against the expected type) are the
precedent.

### 4.3 Canonical printing

Every type has a printer, and printing is always in the canonical
**fully qualified** form, which reads correctly in any context:
`OPCODE::SWAP`, `"probe"`, `Coordinate(12, -34)`, `EntityID(123)`.
Abbreviation is something a person typing gets, never what is
recorded.  History, logs and replay scripts therefore never depend on
contextual reading.

The round trip `read(print(x)) == x` holds for every type, in every
context, and is the first test written (section 10).

### 4.4 Literals, initial set

| Type | Canonical form | Notes |
|---|---|---|
| `Bool` | `true`, `false` | |
| `Int` | `-1234`, `0x3a80` | decimal or hex; int64; readers narrow and range-check |
| `String` | `"probe"` | bare word accepted in `String` context |
| enums | `OPCODE::SWAP` | the C++ enum's name, `::`, the enumerator's name without its prefix |
| `Coordinate` | `Coordinate(12, -34)` | permanent address |
| `EntityID` | `EntityID(76474647846)` | permanent, never compacted; also a Term kind |
| `Hash` | `Hash(0x...)` | |
| `KeyCombo` | `KeyCombo("Shift+R")` | bare word in context, via `combo_from_string` |
| `Term` | any of the self-describing forms above | null, booleans, integers, strings, enumerators, `EntityID(...)` |

**Never a pointer.**  There is no `Object(0x...)` literal and there
will not be one.  A printed address is not a GC root, so it can be
collected between the command that printed it and the one that uses it
-- a use-after-free typed from the console -- and it means different
things on every client, so it could never ride the stream.  Things are
named by stable identity (Coordinate, EntityID) or, for inspection, by
path from such an identity.

Coordinates and EntityIDs are parameter types in their own right; they
do not need to become Terms (the parked Morton-60 encoding stays
parked).  Command parameters are C++ types, of which `Term` is one.

### 4.5 From JSON

The socket and the MCP adapter carry arguments as JSON.  JSON `null`,
booleans, integers and strings map directly.  Every other type travels
as a JSON string containing its literal text (`"Coordinate(12, -34)"`)
and is read by the same reader.  One reader per type, not two.

## 5. The registry

```
struct ParamSpec {
    StringView name;            // kebab-case
    TypeSpec const* type;
    StringView summary;
    Default default_;           // none | constant | computed from context
};

struct CommandSpec {
    StringView name;            // kebab-case; stable once shipped
    StringView summary;         // one line, for help / MCP / completion
    Category category;          // local | world | clock | session
    std::span<ParamSpec const> params;
    Handler handler;            // (CommandContext&, Arguments const&) -> Reply
};

struct TypeSpec {
    StringView name;            // "Coordinate"
    Reader read;                // syntax node + context -> typed value | error
    Printer print;              // typed value -> canonical text
    StringView json_schema;     // fragment for the MCP tool schema
};
```

**Names are file format.**  Command names appear in history files,
replay scripts, settings.json bindings and MCP tool lists; like keymap
ids, never rename or reuse a shipped one.  The keymap's stable ids
(`rotate`, `toggle-map`, ...) become the names of the corresponding
commands, so the keymap's action list and the registry converge.

**Registration is explicit.**  Each module provides
`register_<module>_commands(Registry&)`; the host calls them at
startup.  No static-initialization-order games.  The registry is
static over the life of the process: every command is always listed,
whatever scene is showing, so the MCP tool list fetched once at
connection stays valid.  A handler whose preconditions do not hold
(no world loaded, say) returns an error.

**Computed defaults** make one command serve both the GUI and scripts.
`pipette` with no argument picks up at the tile under the cursor;
`pipette Coordinate(3, 4)` picks up there.  When an invocation is
logged, defaults are printed **resolved**: the history shows
`pipette Coordinate(3, 4)`, not `pipette`, so it replays independently
of where the cursor is.

This is Blender's split between an operator's `invoke(context, event)`,
which fills properties from the mouse and context when the GUI fires
it, and `execute(context)`, which acts on properties alone and is what
a script calls.  Here the split lives on the parameter: the data
source belongs to the parameter's default, not to the command's name.
A key binding therefore names the plain command (Q is `pipette`), and
there is no family of `pipette-at-mouse` variants.

The designed extension, deferred until a binding needs it (section
11): make the default sources nameable as a small **closed** set of
context words -- `@cursor` (the tile under the cursor), `@hand` (the
held Term) -- so a declared default is just `at = @cursor` and a
binding may write `write @cursor @hand`.  Context words are readers
that consult context at issue time and are printed resolved; they are
not variables: nothing assigns them, and the set is fixed in C++.

## 6. Replies and history

A handler returns a reply: success or error, and an optional result.
Results are typed and printed canonically, so anything the game prints
can be pasted back as input.  Some results are structured (the widget
tree, a log tail); those are JSON on the socket and rendered as text in
the console.  A screenshot result is an image: PNG bytes on the socket,
an image content block through MCP, a file path in the console.

**History** is a log of *issues*, with completions attached later.
Every invocation from every origin is recorded when issued, printed
canonically with resolved defaults, tagged with a sequence number, the
frame it was issued in, and its origin; its reply is recorded against
the same sequence number when it completes:

```
#41 frame 1200 console  step 10  -> frame 1210
#42 frame 1203 gui      write Coordinate(3, 4) OPCODE::SWAP
#43 frame 1204 console  frame 100  (cancelled)
#44 frame 1210 socket   step 2  from python3 wry_mcp.py 1234
```

A socket entry ends with its connection's label, which is who was on
the other end: the peer's pid, read from the kernel at connect
(`LOCAL_PEERPID`), with the first word or two of its command line, or
its process name.  The same label tags the console echo:
`[socket python3 wryctl 1234] step 2`.  The connection's ordinal
remains its identity (a pid can be reused once the peer exits); the
label is for people.

A pending entry is completed in place: it records its final status,
reply text and completion frame, and is shown `(pending)` until then.

Issue happens on the main thread, so the log is one total order across
sources.  Frame stamps let a replay reissue each line at the frame it
was issued.  It is what the console scrolls back through, and saved to
a file it is a script.  Because GUI gestures are invocations, the
history shows what the mouse did in the console's own language --
Blender's Info editor, for the same reason: it teaches scripting by
example.

**History is text, not values.**  Every argument round-trips through
its canonical printer, so the printed line is a complete record, and
history holds strings only: it is outside the garbage collector
entirely and keeps nothing alive.  Results likewise: history keeps the
printed reply, or a summary (`image 1920x1080`), never the object.
Nothing can be kept alive by being named, because nothing heavy -- a
world snapshot, say -- *can* be named (section 4.4: no pointers).  The
only GC-visible state is an invocation in flight: its typed arguments
live in the executor's pending queue, which is rooted and scanned, from
parse until completion.

**Printability is the price of entry.**  A value that cannot be printed
cannot be an argument, because its invocation could not be recorded.
In particular every Term kind that can be held in the hand (and hence
written by a GUI `write`) must have a literal.  Today that is null,
booleans, integers, strings, opcodes, matter and entity ids; a new
holdable Term kind brings its literal with it.

`Player::Action` gains a printer to its command-line form.  A finalized
step therefore prints as a list of command lines with their origin
client ids, which is what the desync tooling should show.

## 7. Initial vocabulary

Illustrative, not exhaustive; arguments in `[]` have defaults.

- **introspection** (local): `help [command]`, `widgets`, `log [lines]`,
  `get Coordinate`, `state-hash`, `history [lines]`.
- **view** (local): today's keymap actions -- `rotate`,
  `reverse-rotate`, `flip-horizontal`, `flip-vertical`, `toggle-map`,
  `toggle-console`, `toggle-jacobians`, `toggle-points`,
  `toggle-wireframe`, `pipette [Coordinate]` -- plus `drop`, `hold Term`.
- **event injection** (local): `key KeyCombo [up|down|press]`,
  `type String`, `mouse-move x y`, `click x y [button]`,
  `scroll dx dy`, `press label=String` (click the widget with that
  label).  Positions are in view points, top-left origin -- the
  `gui::Event` convention -- so injected events go through the same
  point-to-pixel conversion, and the same coalescing, as real ones.

**Continuous input is coalesced, and is never a command.**  Mouse
moves, scrolls and trackpad gestures arrive at platform- (and even
OS-version-) dependent rates, unboundedly many per frame.  The GUI pump
standardizes them: within a frame, each **run** of continuous events
between two discrete events (button, key) collapses -- moves to the
last position, scroll and magnify deltas summed.  A discrete event ends
the run, so a click always sees the right position (mouse-down carries
its own location regardless).  At most one continuous event of each
kind survives per gap, independent of platform; injected events pass
through the same pump, so recorded input is bounded and portable.

At the command level there are no moves at all: a position appears
only as a resolved argument (`pipette Coordinate(3, 4)`).  Blender
draws the same line -- `MOUSEMOVE` and trackpad events feed keymaps
and modal operators but never appear in the Info log, which records a
drag as its result (`transform.translate(value=...)`), not its path.
- **capture** (local): `screenshot [region]`.
- **world**: `write Coordinate Term` (what a click with a full hand
  does), `erase Coordinate` (right click).
- **clock**: `pause`, `resume`, `step [n]`, plus local `frame [n]`.
- **session**: `new-game`, `save [name]`, `load name`, `list-saves`,
  `quit`.

## 8. Time

Two clocks, as in multiplayer.md section 4: the **step clock**
(simulation steps, server-authored) and the **frame clock** (GUI and
render frames).  Today they are one: `WorldState::update` steps the
World once per frame.  The command system separates them only as far
as it needs to:

- `pause` stops stepping; frames continue, so the GUI stays live and
  screenshots work.
- `step n` advances exactly n simulation steps, one per frame, then
  replies.
- `frame n` waits n frames without stepping, for GUI animation and
  overlay settling.

Under the networked server, `pause`/`resume`/`step` are the same verbs
addressed to whichever server instance is local; that is the whole
migration.

**Asynchronous commands.**  `step 10` or `frame 5` cannot block the
main thread, and neither can `screenshot`: a frame already committed
to the GPU and swap chain is out of reach, so a screenshot adds a blit
of the *next* frame's presentable texture into a readback buffer and
completes from that command buffer's completion handler (a Metal
thread, so it only queues the reply back to the main thread; it never
touches the GC).  A handler may therefore return a pending reply: a
small state object the executor polls once per frame until it
completes.

Each source (console, each socket connection) executes its
invocations in order and issues nothing further while one is pending,
so a script's `step 10` followed by `screenshot` screenshots after the
tenth step.  Other sources are unaffected: the GUI stays live during a
socket script's `step 10`.  Every pending command is cancellable;
cancellation completes it with the reply `cancelled` and drops the
lines queued behind it.  The console's cancel key is Ctrl-C.

## 9. Execution and transport

**Where commands run.**  All handlers run on the main thread, at one
point in the frame: at the top of the host's frame, before the scene's
event handling.  Event-injection commands push onto
`GuiContext::events`, so injected events are dispatched in that same
frame exactly like real ones.

**The control socket.**  Opened only when the game is launched with
`--control-socket [PATH]` (`open client.app --args --control-socket`);
PATH defaults to `$TMPDIR/wry-control.sock`, which is also where the
tools look unless `$WRY_CONTROL_SOCKET` says otherwise.  The file is
created mode 0600 and removed at exit.

Requests are lines: a JSON object holding a command line or a
structured invocation, or a bare command line.  An `id` (number or
string) is echoed in the reply:

```
{"id": 1, "line": "step 10"}
{"id": 2, "command": "write", "args": {"at": "Coordinate(3, 4)", "term": "OPCODE::SWAP"}}
step 10
```

Every request gets exactly one reply line, in request order per
connection (a request too malformed to queue is answered at once):

```
{"id": 2, "status": "ok", "line": "write Coordinate(3, 4) OPCODE::SWAP", "text": ""}
```

`status` is `ok`, `error` or `cancelled`; `line` is the canonical
invocation (empty if the request did not bind); `image` is added when
the result is an image file (`screenshot`).  Each connection is its own
command source: sequential, and cancelled when the connection closes.

**Closing.**  The server ends a connection by sending every reply it has
queued and then closing, so a client that reads until end of stream
sees all of them.  The converse is not offered: a client that closes,
or half-closes its sending side, before it has read its replies forfeits
whatever is still in flight.  The server sees either as end of stream on
its read, ends the connection at once (the writer is stopped, queued
replies are dropped) and cancels the requests still queued or pending,
which answer nothing.  Unix domain sockets support `shutdown(2)` as TCP
does, so a half-close is possible; it just does not mean "answer
everything, then close" here.  The tools do not rely on it: `wryctl`
and the MCP adapter read each reply before they close.  If a piped mode
ever wants half-close to mean "that is all", the server needs one more
event (the reader's end of stream, ordered after its lines), on which
the main thread queues the end-of-stream marker once the source's last
reply is queued; until then the semantics are the simple ones above.
A structured argument is a JSON string literal of its type unless the
parameter is a String; a string literal must be exactly one node, so it
cannot smuggle in further arguments.

Socket IO runs on the kqueue reactor (core/kqueue_reactor.hpp) as
coroutines on the worker pool, through the transport layer the game's
connections share (io/transport.hpp, multiplayer.md section 8): a
`Transport` with the line codec.  Its loop owns a nursery of
connections, each a reader and a writer under a nursery of their own,
either of which ends the connection by stopping the other.  They move
bytes only.  Each connection's opening (with its peer label), its
complete lines, and its closing after its last line, go in that order
to one event queue (`ControlSocket::events()`, a multi-producer
single-consumer queue) that the main thread takes once per frame and
parses -- parsing can create Terms, so it must happen on a mutator --
and replies go to each connection's writer through a single-producer
single-consumer queue.  There is no read deadline: an idle connection
is legitimate (the MCP adapter holds one open).  Descriptors are closed
only by the frame that owns them, after its nursery has joined, so no
descriptor is closed under an armed knote.  The loop is a task the host
spawns into the process scope (the nursery in main), so it runs under
the root token; it also runs under the transport's own two stop
sources, one for accepting alone and one for everything, bridged from
the inherited token.  `shutdown(grace)` is a task: it stops accepting,
queues an end of stream behind what each connection has pending, waits
up to `grace` for the connections to finish (the last one to close sets
a single-consumer event the task awaits under a deadline), then
`close()`, which cancels whatever is left and unlinks the path;
`close()` alone is the brutal form.  main runs the graceful form
through `sync_wait` at quit, before requesting the root token and
draining the scope.  Invocations from the socket echo to the console as
`[socket] ...`, so a person watching sees what a script or agent does.

**Bounds.**  The socket is local-user-only, but it is bounded as the
game's connections are, by the transport: a line over 1 MiB, or more
than 16 connections, is refused; a connection may have at most 256
lines delivered and not yet taken by the main thread, beyond which its
reader parks and the kernel buffers fill (the peer's writes block, no
memory grows); replies queue up to 16 MiB for a connection that is not
reading them, beyond which it is kicked as a slow consumer; and a
connection may have at most 64 requests queued or pending at once
(`GuiContext::max_pipelined_requests`), beyond which each further
request is refused at once with an error reply, so a client that never
reads its replies cannot make the main thread queue them without
bound.  A kicked connection gets one last line, an error reply with no
id (`{"status": "error", "text": "kicked: <reason>"}`), then the end of
stream; nothing it sent after the kick is processed.

**`tools/wryctl`**: a command-line client.  `wryctl step 10` joins its
arguments into one line, sends it, prints the reply text, and exits 1
on error or cancellation; `wryctl -` runs lines from stdin in order.

**`tools/wry_mcp.py`**: the MCP adapter, stdlib-only Python, launched
by the MCP client over stdio (`claude mcp add wry -- python3
tools/wry_mcp.py`).  It asks the game to `describe` its registry and
exposes one tool per command, with input schemas generated from the
parameter types (Int, Bool and String map to JSON types; any other type
is a string described by its type's example literal), plus a generic
`wry_command` tool taking a whole line.  A reply with an `image` comes
back as image content.  If the game is not running when tools are
listed, only `wry_command` is offered, and the full list is announced
with `notifications/tools/list_changed` once a call reaches the game.
The adapter knows no command by name; it is never edited when commands
are added.

**Headless host.**  `client --headless` (the binary itself; no `open`,
no window, no Dock icon) runs the same scenes, registry and socket with
no window, view or layer.  It implies `--control-socket`.  The drawable
is a fixed 1920 x 1080 pixels viewed at two pixels per point, so
coordinates and screenshots match a windowed run on a Retina display.

Frames run only while commands have work -- something pending, queued,
injected and undispatched, or waiting on the socket -- at a fixed dt of
1/60 s; otherwise main sleeps on the socket, repinning every 100 ms so
the collector advances.  `step 10` therefore takes ten frames as fast as
they render, and a script sees the same scene timers on every run.
Each frame waits for its command buffer to complete (there is no
drawable to throttle it), which also completes screenshots before the
next frame.  `quit` ends it, as QUIT TO DESKTOP does in a window.

The delegate owns the scenes, which own the WorldState, whose Root
destructors shade, and main unpins for the shutdown drain; a shade from
an unpinned thread during marking traps.  So main releases the delegate
pinned, and the pin straddles the end of main's autorelease pool: AppKit
autoreleases a reference to the delegate into that pool, so the dealloc
happens at the pool's pop, not at the release of main's own reference.
A quit from the menu never exercises this; a quit with a live game does,
which is why the smoke test starts one.

**`tools/wry_smoke.py`** is the end-to-end test: it launches the newest
Debug build headless on a private socket, plays a scripted session
(menu, NEW GAME, write, step, screenshot), checks every reply, quits,
and requires exit status 0 -- the whole game under the scheme's
sanitizers, driven only through the command interface.

## 10. Staging

Each landing builds and passes the Debug suite.

1. **Grammar and types.**  Tokenizer, stage-1 parser, the registry,
   `TypeSpec`s for the initial literal set, canonical printers.
   Brute-force tests: random values round-trip through print and read
   in every context; malformed input is rejected, never crashes.  No
   GUI involvement.
2. **Console and keymap convergence.**  The console executes lines and
   keeps history; `help`; `WorldState::perform_action`'s cases become
   command handlers named by the keymap ids; `submit_local_commands`'s
   writes become `write`/`erase` invocations; GUI invocations echo to
   history.
3. **Driver commands.**  `pause`/`resume`/`step`/`frame` with pending
   replies; event injection; `screenshot` (blit the presentable texture
   to a shared buffer, encode PNG); `widgets`.
4. **Outside.**  Control socket, `wryctl`, the MCP adapter.
5. **Headless host.**

## 11. Open items

1. Lists and records as literals (a command taking several
   coordinates).  Not needed for the initial vocabulary; if added,
   `[a, b]` is the obvious spelling and stays data.
2. Namespacing: all commands share one flat namespace.  Accepted for
   now; if it ever hurts, dotted names (`debug.toggle-points`) are a
   non-breaking addition only if adopted before the names ship.
3. Key bindings to arbitrary command lines (`"F5": "step 10"`), Quake
   style, as a settings.json extension.
4. Session handles (`$1`, gdb style) for drilling into results: would
   need explicit rooting, are a step towards variables, and are
   deferred.
5. `source file` to run a script file line by line: harmless (still no
   control flow) but not yet needed.
6. Authentication beyond socket file permissions.
7. Character literals for Term characters.
8. Context words (`@cursor`, `@hand`; section 5): designed, deferred
   until a binding needs a source other than its parameter's default.
9. In-between samples: coalescing discards the path of a mouse run.
   If something ever needs it (drag-painting a line of tiles), keep
   the superseded samples as an opt-in side channel, as Blender does
   with `INBETWEEN_MOUSEMOVE` for paint strokes.
10. Defaults resolve at bind time, before the handler runs, so a
    parameter the handler ignores on some branch must still resolve:
    `pipette` with a full hand drops and never reads `at`, yet fails
    if the cursor is off the ground.  Harmless while the camera always
    looks at the ground; if it bites, add optional parameters (absent
    is a value) rather than lazy defaults, which would break
    resolved-default printing.
