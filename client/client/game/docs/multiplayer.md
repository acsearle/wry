# Multiplayer

Design for the networked game: the duties of the Server, the client
lifecycle, the wire protocol, and the plan for building it.  First agreed
2026-08-25; revised 2026-10-20 against the work that landed in between
(the command system and control socket, core/docs/commands.md; the
async I/O stack, core/kqueue_reactor.hpp, core/nursery.hpp,
container/mpsc_queue.hpp; the hashing groundwork in core/hash.hpp) and
against the decisions in section 0.  Of the plan in
section 13, the transport layer (stage 1), the action codec and
explicit step intake (stage 2), and the relay, client and host (stage
3) are landed: single player runs through the relay over a socket
pair, and a second process joins over TCP.

Status: normative design, with a staged build plan (section 13).  The
direction is agreed; specific numbers remain open (section 11).
Section 12 compares against prior art; the architecture is closely
convergent with Factorio's and OpenTTD's, which is intended reassurance
rather than coincidence.

--------------------------------------------------------------------------

## 0. Decisions of 2026-10-20

These revise or sharpen the 2026-08-25 design.  Where the sections
below disagree with the older doc, these win.

1. **All I/O is asynchronous**, on the kqueue reactor, as coroutines on
   the worker pool.  The game transport shares its transport layer with
   the control socket: the listener, connection and bounded-queue
   machinery is factored out of `io/control_socket.{hpp,cpp}` and both
   are instantiations of it.  Improving the control socket on the way
   (bounded inbound and outbound queues, a kick with a reason, a
   pipelining cap) is part of the work, not a detour (section 8).
2. **No encryption yet.**  Plain TCP.  The connection abstraction keeps
   the codec above the byte stream so TLS can be slotted underneath
   later without touching the vocabulary (open item 11.5).
3. **The server does not trust clients.**  Framing and envelopes are a
   security boundary: every length is capped per tag, every count and
   rate is capped per connection, inbound reads stop when a client's
   backlog is full (backpressure to the kernel and the client), and
   outbound overflow is death.  Misbehavior kicks: nothing further from
   that connection is processed, one courtesy `KICK{reason}` is sent for
   the peer's debugging, and the connection closes.  The server never
   judges the *legality* of an action in the game; it orders and
   broadcasts action bytes it does not read (section 7).
4. **The state hash is proof of work and proof of consistency.**  Beyond
   the desync tripwire, a client's hash reports are its liveness, its
   lag measurement, and its proof that it holds the state it claims: a
   joiner proves receipt of the donated save by hashing it, proves it is
   keeping up by hashing the steps it replays, and is promoted from
   observer to player only once its hash at some recent step matches the
   reference (section 6).  There is no separate `ACK` message.
5. **A process is a pure client, or a client plus server.  Never a
   server alone.**  The server in a hosting process holds no World; when
   it needs state (a save for a joiner, the reference hash) it asks its
   co-resident client, which is an ordinary client reached over the
   ordinary queues.  A clientless server is a later upgrade with a
   trust problem (it must pick a client to donate) that we do not solve
   now.  "Resident spectator" in the old design is replaced by "the
   host's own client" (section 2).
6. **No failover.**  If the server disappears, the game is over for its
   clients.  A client can, at any time, save its local state; that save
   is a fork of the game's history, which it can continue alone or host
   for others.  Promoting a client to server in place is a later
   upgrade.
7. **Single player is multiplayer on a same-process server.**  The
   local client connects to the local server over a socket pair and
   runs the full wire codec, so every solo session exercises the
   protocol.  `LocalServer` is retired, not grown.

## 1. The model

The game is deterministic lockstep.  Every client holds a full World and
advances it by identical steps; only player actions travel on the network.
The Server receives actions from all clients, places them into an
authoritative order, and broadcasts them back; clients apply the identical
broadcast and their Worlds advance identically.  Bandwidth scales with
player count, never with world size -- the property that makes networked
play of a factory-scale world possible at all.

**The stream is the game.**  A checkpoint save plus the finalized stream
from that checkpoint is a replay, a late-join, a reconnect, and a desync
repro, depending on where you cut it.  Everything in this document is one
mechanism -- the stream -- viewed at different offsets.

**No backpressure on the clock.**  The server advances on its own timer
and never waits for anyone: each step finalizes with whatever has
arrived.  Clients keep up or die (are kicked, and may rejoin).  This is
the same policy that governs the garbage collector, adopted with the same
open eyes: designs that refuse to wait are generally held to be a bad
idea, and we accept the risk deliberately, because every waiting design
hands the slowest or most malicious node control over everyone else's
experience.  Backpressure *on the socket* is a different matter and is
applied freely (section 7): a client that sends faster than the server
reads simply finds its socket full.

**Single binary.**  Determinism is guaranteed only among identical builds.
Version equality is enforced at handshake; there is no cross-version
compatibility ambition.

## 2. Roles and processes

Three concepts, deliberately distinct:

- **Server**: orderer, relay, and clock.  It holds no World and does not
  simulate.  It is one sequential coroutine task consuming one inbox
  (section 8): socket events, a step timer, and replies from its
  co-resident client.
- **Client**: anything that subscribes to the stream and maintains a
  World by applying it.  All clients report state hashes and can donate
  saves.  A client with no player is an **observer**.
- **Player**: an EntityID in the simulation, owned by a client.  Only
  clients-with-players submit actions; observers are passive.

**Processes.**  A Wry process is either a **pure client** (joined a game
elsewhere) or a **host**: a client plus a server, connected over a
socket pair, with the server additionally listening on TCP when the game
is open to others.  **Single player** is a host that is not listening.
There is no process that is a server and nothing else.

The host's client is where the server turns when it needs game state: it
is the save donor for every joiner and the reference for every hash
comparison.  It is in no way special on the wire -- it is a client that
happens to be reachable through an in-process queue as well as through a
socket -- and the server addresses it through the same vocabulary as any
other client, plus one private channel for donation requests that
cannot ride the socket (the server does not want save bytes to pass
through the client's inbound frame budget).

**Identity.**  The server assigns each connection a small **client id**.
The map from client id to Player EntityID lives *inside the World*:
joining rides the stream as a server event (section 6), every client's
simulation spawns the Player entity on applying it, and deterministic
EntityID allocation guarantees they all compute the same id.  The server
never needs to know any EntityID -- it could not, since it does not
simulate.  A client finds its own Player by looking up its own client
id in its own World.  Session tokens and reconnection are deferred
(open item 11.3); in this revision a lost connection is a LEAVE and a
returning client joins from scratch.

## 3. The step stream

Two origins of traffic ride the finalized stream:

- **player actions** (`Player::Action`), submitted by clients, ordered by
  the server;
- **server events**: player join and leave, pause and resume transitions
  -- authored by the server, carried as commands whose origin is the
  reserved server id.

Everything that mutates the World, or its clock, MUST ride in the stream.
Everything else (handshake, hash reports, donation, kicks) is meta
traffic and stays out of it, so that the stream alone replays the game
exactly.

**Finalization.**  `STEP{n, commands[]}` declares the content of step n,
possibly empty.  Empty steps are the common case and double as heartbeat
and clock.  `World::step()` consumes exactly one finalized step, applying
*all* of its commands in that step (section 9.2 on why this is a change);
a client may hold several finalized steps not yet applied.

**Ordering is authorship, not consensus.**  The determinism obligation is
only that every client applies the identical broadcast.  The server's
choice of order within a step is free -- arrival order, round-robin,
anything -- because the broadcast *is* the definition of the order.
Nothing about the server's internal scheduling needs to be deterministic;
only fairness pressures constrain it.  The initial policy is arrival
order, which with a sequential server task is simply inbox order.

**Sequencing.**  Each client numbers its actions with a monotonic
per-connection sequence number starting at 1.  The server keeps a
next-expected cursor per client: the expected number is accepted,
anything else is a protocol violation (TCP makes gaps impossible while a
connection lives, and without reconnection there are no resends).
Actions beyond a per-step cap are deferred to the next step, never
dropped from the middle; a backlog beyond a cap of steps is a flood and
kills the connection.  The resulting contract: **a client's finalized
actions are exactly a prefix of its submitted sequence, up to the death
of its connection.**  Seeing your own action in a STEP is its
acknowledgment.

**Actions are total.**  The server assigns actions to steps; a client
cannot target a step, and cannot know at submission which state its
action will meet -- or whether it will be finalized at all.  Every action
handler therefore validates at application time against whatever the
World then is -- target gone, tile changed, request stale -- and degrades
to best-effort or no-op.  No action may crash or diverge on any reachable
World, and no action may fail to *parse* in a way that crashes: a client
receiving a STEP whose action bytes do not decode treats that action as
a no-op (the server forwarded bytes it did not read; a malformed body
is the sender's problem, and the hash will catch any client that
disagrees about what no-op means).  This is a standing constraint on
the action vocabulary as it grows beyond `WRITE_VALUE_FOR_COORDINATE`,
and it favors coordinate-addressed actions (which age gracefully) over
captured-reference ones.

A corollary worth staring at: totality means diverged clients keep
playing indefinitely, their actions increasingly nonsensical as applied
to everyone else's World, and everyone else's to theirs.  Nothing
crashes.  Divergence is invisible except to the hash tripwire (section
5), which is why the tripwire is routine rather than exceptional.

## 4. Time

**The step rate is fixed and sacred.**  The server finalizes at a
constant quantum, regardless of anyone's simulation speed.  There is no
adaptive slowdown.  A client whose hardware cannot simulate at the step
rate cannot play; a client that falls behind catches up or dies.  Same
policy as section 1, same caveat.

**The clock commands** (`pause`, `resume`, `step n` of commands.md
section 8) are addressed to the server and honored only by the host:
they are the host's authority over the clock.  `pause` stops the timer
and emits `PAUSED` in the stream at the next boundary; `resume` emits
`RESUMED` and restarts it; `step n` finalizes n steps immediately, one
after another, without waiting for the timer, then leaves the clock
paused -- which is what tests and scripts want, and is why the headless
smoke test keeps its current timing.  A pure client's `pause` is
refused in this revision; majority-rule pause voting (the 2026-08-25
design, kept in open item 11.4) comes later.

**Clients keep two clocks.**  The *step clock* applies finalized steps:
one per frame when current, several per frame (bounded, so the GUI
stays live) when behind.  The *frame clock* renders whatever the newest
applied World is.  The smoothed render delay and rubber-banding of the
2026-08-25 design (an audio-style jitter buffer) are deferred to open
item 11.6; the first version renders the latest applied step and
accepts the jitter.  Gameplay absorbs what the network cannot: no
twitch mechanics, and immediate cosmetic acknowledgment of every local
action.

**Latency in single player.**  Today an action submitted in frame f is
applied in the step of frame f+1 (`LocalServer`'s one-frame round
trip).  Over a socket pair the round trip is the same order (two reactor
hops), and the step cadence is the server's quantum rather than the
frame rate.  The quantum defaults to the current step-per-frame rate so
that nothing changes perceptibly; it is a settings value, not a build
constant.

## 5. Desync

Divergence is a bug, full stop.  The machinery here exists to *detect and
diagnose* bugs, not to recover from them gracefully; the hope -- famous
last words -- is that day-one paranoia makes real desyncs rare enough
that the crude response never matters.

**The hash.**  Every client computes a content hash of its World at
every step boundary and reports it every k steps (k = 1 once the hash
is incremental, section 9.1; a coarser cadence until then), tagged with
the step number.  The hash is a function of content alone -- never of
pointers, trie shape, or build layout -- and the live hash equals the
hash of the canonical save stream, so a loaded save hashes as the
World it was saved from.  The definition and its cost are section 9.1's
business; this section only requires that it exist and be cheap enough
to run every step.

**What the hash proves.**  To the server, a `HASH{step, h}` from a
client says three things: the client is alive (liveness), it has
applied step n (lag), and it holds the same state as everyone else who
reported n (consistency).  It is also proof of work: a client cannot
report the hash of step n without having simulated step n, so a client
that only pretends to play (a relay bot, a spectator claiming to be a
player) is caught by the same mechanism.  Section 6 uses this to gate
the observer-to-player transition.

**The response.**  The server compares reports for the same step and
kicks the minority immediately -- no negotiation, no resync protocol.  A
kicked client may rejoin through the normal join path, which is the only
resync mechanism that exists.  Rationale, against the alternatives:

- pause-and-investigate lets any client stall the game by sending a
  deliberately poisoned hash;
- resync-the-minority builds recovery machinery whose main long-term
  effect is to hide the bugs we most need to fix;
- kick-the-minority makes a poisoned hash a self-kick and nothing more.

With no majority (two clients, or a tie), the reference is the host's
client.  (The host's client is always connected while the game exists,
so the reference always exists; this is the simplification bought by
decision 0.5.)

**The artifact.**  Checkpoint + finalized log = deterministic repro.  The
host writes the finalized stream to a log file beside its saves; on
mismatch the server names the offending step, and any client can be
asked to dump state (`save`).  Diagnosis is then offline: two Worlds
stepping the same log from the same checkpoint, bisect to the first
divergent step, structurally diff the states (the save format round
trips, so diffing is mechanical).  The harness is stage 6 of section 13;
every published account of shipping this architecture (section 12)
says desync debugging is where the time goes.

## 6. Join, leave, and the hash gate

A client needs a World consistent with some boundary B, plus the stream
from B forward.  There is no ring of past steps: the joiner is
subscribed to the live stream *before* the donor snapshots, so every
step after B reaches it in order and the donation only has to carry B.
(Until stage 5 lands, B is the scenario's genesis: the relay keeps the
whole stream and a subscriber replays it from step 1, bounded only by
the transport's outbound limit; `join` starts the scenario afresh
first.)

**Subscribe** (become an observer):

1. `HELLO`/`WELCOME` handshake: protocol version, build version, tier-2
   content hash (zero until there is tier-2 data), client id, step
   quantum, current head.  `HELLO` must be the first frame on a
   connection and must arrive within a handshake deadline; anything else
   is a violation.
2. At the next boundary B the server (a) adds the connection to the
   STEP broadcast, so it receives `STEP{B+1}` onward and buffers them,
   and (b) asks the host's client, through the in-process channel, for a
   save of its World *as of B*.  The request is queued ahead of
   `STEP{B}` on the host client's inbox, so the host client knows to root
   the World that `STEP{B}` produces and hand it to the ordinary
   background serializer (`io/save.cpp`'s walk, yielding as it goes).
   The bytes come back to the server, which streams them to the joiner
   as `SAVE_DATA` chunks under the transfer's id.
3. The joiner loads the save (on its main thread; load is the
   latency-critical path and stays synchronous for now), computes its
   hash, and reports `HASH{B, h}`.  This is its **proof of receipt**: the
   server already holds the host's hash for B, and a mismatch means a
   corrupt or incompatible transfer, which kicks.
4. The joiner replays its buffered steps at full speed, reporting hashes
   at the usual cadence, until it reaches the head.  Each matching
   report is **proof of keeping up**.  Catch-up must outrun the live
   game; it will, unless the hardware is simply below par (section 4's
   position: then it cannot play) -- or the host pauses.
5. An observer is a full client: it hashes and can be asked to donate
   (a host's client is always preferred as donor, so in this revision
   observers are never asked; the capability costs nothing).

**Join** (observer to player) is gated: the client sends `JOIN_REQUEST{
name}` and the server honors it only once that client's most recent hash
report is for a step within a small window of the head and matches the
reference -- **proof of readiness**.  Then a `JOIN{client id, name}`
server event rides the stream; every World spawns the Player entity
deterministically and records client id -> EntityID; the client finds
its Player in its own World (section 2).  Only after JOIN does the client
submit actions, and an action from a client with no Player is a
violation.  The host's client joins the same way; in single player the
gate is satisfied trivially at the first boundary.

**Leave and kick.**  All exits look the same in the stream: the server
emits `LEAVE{client id}`, and the Player entity, if any, idles in place
(it never acts again; despawning is a gameplay decision for later, open
item 11.2).  Exits happen:

- voluntarily (`QUIT`);
- liveness timeout: no hash report for T seconds while the clock runs
  (a paused game sends no steps and expects no hashes; the server pings
  instead);
- slow consumer: the per-client outbound queue overflows -- keep up or
  die applies at the socket, too;
- flood: the per-client inbound backlog exceeds its cap of steps;
- desync (section 5): immediate;
- protocol violation (malformed frame, bad length, bad sequence, wrong
  state for the message): immediate.

**Kick** is one procedure for every involuntary exit: stop reading the
connection (its reader is cancelled, so nothing further it sent is
processed), queue `KICK{reason}` as the last frame with end-of-stream
behind it, give the writer a short deadline to flush, close, and emit
`LEAVE` in the stream.  The reason is a short string for the peer's
log; it is a courtesy, not a protocol element, and a peer that does not
read it loses nothing.

**Server shutdown.**  The host quitting sends `SHUTDOWN` to every client
and closes; a client that loses its server shows the game as
disconnected and offers to save (the fork of decision 0.6) or quit.
The client's World is intact, so this is just the existing save path.

## 7. Wire format and trust

Binary, little-endian, no alignment: fields memcpy in and out.  Wire and
save formats share primitives but version independently: the wire schema
is negotiated per session and can change freely; saves must stay readable
for years.

**Framing.**  Every frame begins `u32 length` (of everything after it),
then `u16 tag`, then the payload.  The connection's reader partitions
the byte stream into frames with zero understanding beyond the length,
and the length cap is a function of the tag family: small for control
and actions, large only for `SAVE_DATA` chunks.  An out-of-range length,
or an unknown tag, is a violation.

**The common header is minimal by design: length and tag.**  Everything
else is per-family:

- `ACTION` adds `u64 seq`, then the action body -- and the body is
  **opaque to the server**.  The server parses envelopes only: it
  orders, stores, and forwards action bytes without decoding them.  Only
  clients interpret bodies.  This is what lets the relay stay ignorant
  of the game schema; it depends on the protocol version alone.
- **Sender is not a field.**  The server knows who sent a frame from the
  connection it arrived on.  The server stamps the origin's client id
  onto each command when rebroadcasting: `STEP{n, [{origin, seq,
  bytes}...]}`.  Server events are commands whose origin is the reserved
  server id 0, so the log is uniform: the game is exactly the sequence of
  STEPs.
- **Transfers** (save donation) add a server-assigned transfer id and
  chunk index.  There is deliberately no way for a client to address
  another client: the vocabulary is closed, and the relay is not a
  general message bus.
- **Step numbers** appear only in server-authored frames.  Clients never
  claim a step; the server assigns actions to steps (section 3).

**The action body** is the canonical command-line text of the action
(`write Coordinate(3, 4) OPCODE::SWAP`), UTF-8, length-capped: a
`Player::Action` is a command type like any other (`Type<Player::Action>`
with a reader and a printer, built from the readers the command system
already has), and its canonical printed form is its serialization.
This reverses commands.md section 2's "text never goes on the wire",
deliberately: the text codec exists, is total, is fuzzed by the
round-trip tests, and makes every log and desync diff readable without a
decoder, while a binary codec would be a second thing to write and
test for a payload of tens of bytes.  The server treats the body as
bytes either way, so a binary body can replace the text later behind
the same envelope if size ever matters (open item 11.7).

**Vocabulary** (initial; will grow):

- client to server: `HELLO{versions, tier2_hash}`, `ACTION{seq, bytes}`,
  `HASH{step, h}`, `JOIN_REQUEST{name}`, `SAVE_DATA{transfer, index,
  bytes}` (donor; unused while the host's client donates in-process),
  `QUIT`.
- server to client: `WELCOME{client_id, quantum, head}`, `STEP{n,
  commands[]}` (carrying player actions and, as server-origin commands,
  `JOIN`/`LEAVE`/`PAUSED`/`RESUMED`), `SAVE_BEGIN{transfer, boundary,
  total_bytes}`, `SAVE_DATA{transfer, index, bytes}`, `SAVE_END{transfer}`,
  `PING` (while paused), `KICK{reason}`, `SHUTDOWN`.

**Trust.**  The server parses hostile bytes; the framing and envelope
parser is a security boundary in a way the save parser is not.  The
rules, all enforced in the connection layer or the server task, never in
game code:

- every frame length is checked against its tag's cap before the body
  is read, and a frame is only delivered whole;
- every per-connection count is capped: unprocessed inbound frames
  (beyond which the reader parks, so the kernel buffer fills and TCP
  flow control reaches the client), queued outbound bytes (beyond which
  the client is kicked as a slow consumer), deferred actions (beyond
  which it is kicked as a flood), connections per listener;
- the state machine is strict: `HELLO` first and once, `ACTION` only
  after `JOIN`, `SAVE_DATA` only for a transfer the server opened with
  that client, and so on;
- the server never looks inside an action body, so nothing in game
  code can be reached by a hostile client except through the actions'
  totality, which is the game's own contract with itself.

Clients in turn parse server bytes and donated saves with the same
discipline on lengths and counts.  The save loader is a trusted-input
parser today (it asserts rather than rejects); hardening it is part of
the join stage (section 13).

What the server understands, in total: lengths, tags, sequence numbers,
client ids, step numbers, the transfer table, hashes, the clock.  Never
an action body; never a World.

## 8. Process and thread architecture

**The reactor is the only thread that touches kernel readiness**, and it
runs no user code (core/kqueue_reactor.hpp).  Everything else is
coroutines on the worker pool, which are mutators but **never touch GC
objects on the transport side**: readers and writers move bytes and
plain structs, and decoding a body into World-referencing types happens
on the consuming side of a queue, on the main thread.  This is exactly
the control socket's shape, generalized.

**The transport layer**, factored out of `io/control_socket.cpp`:

- `Connection`: owns a descriptor; a reader and a writer under a
  nursery, either of which ends the connection by stopping the other;
  a codec that splits the inbound byte stream into messages (lines for
  the control socket, length-prefixed frames for the game) with a cap;
  a **bounded** inbound path (the reader parks when the consumer's
  unprocessed count is at the cap) delivering to an inbox the consumer
  owns; a **bounded** outbound queue of bytes with an end-of-stream
  marker and an overflow policy; `kick(reason)` = stop reading, send a
  final message, end; and a label (peer pid and name for Unix sockets,
  address and port for TCP).
- `Listener`: an accept loop under a nursery of connections with a
  connection cap, the graceful/brutal shutdown pair the control socket
  already has, bound to a Unix path or a TCP port.
- `connect`: the client side, over `connect_to`.

The control socket becomes `Listener` + line codec + its existing event
vocabulary; its behavior is unchanged except for the bounds, which it
gains for free.

**The server** is one coroutine task in the host's process scope.  Its
inbox is one multi-producer single-consumer queue of events: a frame
from a connection, a connection opened or closed, the step timer, a
reply from the host's client (save bytes ready).  Consuming one inbox
sequentially makes the server a state machine with no locks of its own,
which is the property that keeps the trust rules checkable.  The timer is
`cancelable_after` re-armed per step; `step n` from the host bypasses it.
Bulk work per event is bounded (a save transfer is chunked, one chunk
per event) so relay traffic cannot starve the clock.

**The client** owns one `Connection` to its server, whose inbox the main
thread takes once per frame alongside the control socket's events.  The
`Server` seam of `game/server.hpp` keeps its shape in spirit:
`submit(action)` frames and queues an `ACTION`; `poll()` becomes
`take_steps()`, yielding zero or more finalized steps; `World::step`
consumes exactly one, with that step's commands as its input (section
9.2).  There is one implementation, the networked one; single player is
the same code over a socket pair.

**Waiting.**  The headless host idles on the control socket today; with
a server in the process it must also wake for finalized steps, donation
requests and save completions.  Individual waits do not compose, so the
idle becomes one `when_any` over the inboxes' `take()`s (ext/execution.hpp
has the race), or, simpler, every producer posts to one main-thread
inbox and the idle is one `take()`.  The second is the control socket's
own design (one event queue, a sum type) applied one level up, and is
the recommendation.

**The host's client** is a client like any other, plus the private
donation channel: a queue from the server task to the main thread
carrying `Donate{boundary, transfer}`, and the reverse path is the save
task's completion (`io/save.cpp`'s coroutine, which already serializes a
rooted snapshot off the main thread) handing its buffer to the server's
inbox.  No GC object crosses into the server task: the Root lives in
the save coroutine's frame, which is a mutator, and the server receives
bytes.

## 9. Supporting work

What multiplayer needs that does not exist yet, beyond the transport and
the server themselves.  These are the items the question "is any
supporting work missing?" turns up; each is a stage or part of one in
section 13.

### 9.1 The World content hash

Nothing in the tree hashes a World today.  core/hash.hpp has the
primitives (`splitmix64`, the Boost-shaped `hash_combine`, a buffered
byte hasher), the AMT carries a subtree count, and the design was
settled in the "AMT count and hash computation" session (2026-10-10):

- **Per-entry hash** `mix2(code, value_hash)`; **per-node hash** the
  64-bit wrapping sum of its entries' hashes, cached on the node.
  Content-only by construction, independent of trie shape and symbol
  width, equal to the sum over the flat code-ordered save record, so a
  loader can verify a file by summing as it reads.  Erase is subtraction,
  so the rebuild maintains it at O(nodes touched).  The binary Merkle
  form is the documented fallback if inclusion proofs or adversarial
  strength are ever needed; nothing here needs them (a poisoned hash is
  a self-kick).
- **Value hashes are content hashes, never pointers**: `Entity` and
  `HeapTerm` gain a content hash cached at freeze; Term OBJECT payloads
  hash by their target's content; `PersistentStack` cells cache a hash.
  The proposed mechanism is a second sink behind the save walk's write
  and visit surface -- mixing instead of appending, substituting a
  referent's content hash for its record number -- so every saveable
  class gets a hash with no per-class code and the live hash equals the
  save hash by construction.  The entry mix must be strongly non-linear
  (moving one unit from cell a to cell b must change the sum); pin that
  with a test.
- **The World hash** is a positional combine of `_time`,
  `_entity_id_source`, each map root's hash, `_waiting_on_time`, and
  `_ready` folded into the time wheel exactly as the save does.
- The `state-hash` command (commands.md section 7) exposes it.

Cost: the bulk of the work is the per-class plumbing.  Until the
incremental form lands, the fallback is to hash the canonical save
stream of an O(1) snapshot on a background thread at a coarse cadence,
which is what the 2026-08-25 design proposed and what stage 4 can ship
first.

### 9.2 The step intake

Two properties of today's intake are wrong for lockstep:

- `Player::notify` pops **one** action per step from `Player::_queue`,
  so a `STEP` carrying three actions from one player applies them over
  three steps.  Harmless for determinism (every client does the same)
  but wrong for the contract "the step's commands are applied in the
  step", and it interacts with the next point.
- `Player::_queue` is a mutable member of a published-immutable GC
  object and is **not saved** (`Player::_save_body` persists identity
  only).  A donor snapshot at boundary B with actions still queued hands
  the joiner a World that will not apply them; the joiner diverges at
  its first hash.

The fix is to make the step's input explicit: `World::step` takes the
step's command list (origin client id, sequence, decoded action), the
transaction context exposes the list, `Player::notify` applies the
actions whose origin maps to it in one transaction (a transaction
writes a key at most once, so the last write to a cell within a step
wins; the WAIT_ALWAYS lesson of 2026-09-18 stands), and the queue
disappears from the entity.  Nothing unsaved then influences a step,
and the save/hash of a World at B is a complete description of it.
Landed 2026-10-21.  A JOIN's Player first acts in the step after the
JOIN; an action in the JOIN's own step is dropped, which the server
never produces (a client cannot submit before it sees its JOIN) and
`LocalServer` avoids by making the JOIN a step of its own.

### 9.3 Client id to Player map, and the server events

The World gains a persistent map from client id to Player EntityID
(`_player_for_client`, landed 2026-10-21), written by the `JOIN` server
event and read by each step to route a client's actions; `LEAVE`
erases the entry and the Player idles in place.  `JOIN` is idempotent
for a client the World already maps, which is how a host's own client
keeps its Player across a load of its own save; a fork hosted by a
client that was not client 1 in the original game maps client 1 to the
original host's Player (open item 11.11).  `PAUSED`/`RESUMED`
are events the World records only for replay fidelity (they do not
change state; they change the step clock, which replays reproduce by
the log alone).  Save format bump for the new World member.

### 9.4 Action codec

`Type<Player::Action>`: reader and printer in the command grammar, as
section 7 specifies, with a round-trip test alongside the existing type
tests.  `Player::Action` holds a `Term`, so a decoded action is rooted
while it waits for its step, the way a bound argument is today
(commands.md section 6).

### 9.5 Save as bytes

`io/save.cpp` serializes to a buffer already (`serialize_world`), but
`load_game` reads a file path.  Join needs `load_world(span<byte>)`, and
the loader needs to reject bad input instead of asserting (section 7).
The donor side is the existing background save coroutine with its sink
redirected to the server's inbox.

### 9.6 Transport extraction and control-socket improvements

Section 8's `Connection`/`Listener`, extracted from the control socket,
plus the bounds the todo already lists against it: a bounded inbound
queue with reader parking, a bounded outbound queue, a pipelining cap
per connection (commands.md section 9 should state it), and `kick`.
The control socket's tests move to the shared layer and the socket keeps
its own for the line codec and the event order.

### 9.7 Hosting and joining from outside

`--host [PORT]` and `--join HOST:PORT` on the command line, and `host
[port]`, `join host:port`, `leave` as session commands, so the headless
host and the control socket drive a two-process game without a GUI.
Menu buttons for the same come with the main-menu work, not here.

### 9.8 Version constants

A protocol version and a build identity (a git hash or a build counter
stamped at build time) for `HELLO`.  Tier-2 content hash is zero until
tier-2 data exists.

## 10. Relation to current code

- `game/server.hpp` -- `Server` becomes the client's view of the
  stream: `submit` frames an action, `take_steps` returns finalized
  steps.  `LocalServer` is deleted once the socket-pair host works.
- `game/world_state.cpp` -- `update` applies zero or more finalized
  steps per frame instead of exactly one; `_paused`/`_step_budget` move
  to the server (the clock commands address it); `submit_write` is
  unchanged in shape; `save_current`/`load_from_save` gain byte-buffer
  siblings for donation and join.
- `game/player.{hpp,cpp}` -- `_queue` goes; `notify` reads the step's
  slice (section 9.2).
- `game/world.{hpp,cpp}` -- `step` takes the step's commands; new
  client-id map; the content hash.
- `io/control_socket.{hpp,cpp}` -- split into the shared transport and
  the line-codec socket; gains bounds and `kick`.
- `io/network.hpp` -- the sketch was deleted; `io/transport.hpp` is
  the transport layer.
- `io/binary.hpp` -- the intended substrate for the envelope encoding;
  it has no callers today and may be trimmed to what the envelopes need.
- `core/command.hpp`, `game/command_types.cpp` -- `Type<Player::Action>`.
- `core/main.mm`, `render/WryDelegate.mm`, `render/gui_context.cpp` --
  the flags, the server task's spawn into the process scope, the unified
  main-thread inbox.
- Deterministic EntityID allocation (landed 2026-08) is load-bearing
  twice: JOIN spawns identical Player entities everywhere, and the state
  hash is meaningful at all.

## 11. Open items

1. Numbers: step quantum; hash cadence k before the incremental hash;
   liveness timeout T; handshake deadline; per-tag length caps;
   per-connection inbound, outbound and deferred-action caps; join
   window (how close to head a hash must be).
2. Player entity fate on LEAVE: idle in place for now; despawn is a
   gameplay decision.
3. Reconnection: session tokens, a ring of recent steps, resend after
   reconnect.  The 2026-08-25 design (resume from "I have through N")
   stands as the plan; dropped from this revision to ship the core.
4. Pause voting: majority rule with the "$name wants to PAUSE" notice,
   per the 2026-08-25 design.  This revision has host-authoritative
   pause only.
5. Transport security: TLS under the codec.  Until then, hosting is for
   trusted networks, which the UI should say.
6. Render clock: the smoothed delay behind the newest applied step.
7. Binary action bodies, if the text form ever costs bandwidth.
8. Clientless server, and failover by promoting a client: both need the
   server to trust a client as donor and reference; revisit after the
   core ships.
9. Spectating as a user-facing feature: nearly free under the model,
   unexamined.
10. Transport evolution: framing is stream-agnostic on purpose, so QUIC
    can replace TCP per connection without touching the vocabulary.
    Not planned.
11. Identity across forks: client ids are per game, so a save hosted by
    a client other than the original host maps client 1 to the
    original host's Player.  A persistent player identity (a name or a
    key) in the map would fix it; not needed while hosting is for
    trusted groups.

## 12. Prior art

- **Factorio** (dev blog FFF #147 and the desync posts): client-server
  deterministic lockstep; server orders and broadcasts; join by save
  streamed through the server plus catch-up.  The closest relative.
  Differences: their server simulates (validation, authoritative saves,
  authoritative pause); UDP with bespoke reliability; bounded client-side
  prediction for avatar movement and GUI.  We choose relay ignorance,
  TCP, and cosmetic-only feedback -- simplifications their experience
  suggests are affordable at our pace.
- **OpenTTD**: commands routed through the server over plain TCP,
  executed at scheduled frames; joins by save streamed through the
  server.  Existence proof for the transport choice.
- **Age of Empires** ("1500 Archers on a 28.8", Bettner & Terrano, GDC
  2001): the barrier design -- peers schedule commands two turns ahead
  and everyone waits for everyone.  Its stall is the famous failure mode,
  and the reason the timer-driven server won history.  Rejected here.
- **GGPO / rollback**: predict remote input, roll back and re-simulate
  on mispredict.  Right for twitch games with small state; wrong here.
  Noted in passing that persistent snapshots would make bounded rollback
  unusually cheap for this engine if some interaction ever demands it.
- **Forrest Smith, "Synchronous RTS Engines and a Tale of Desyncs"**
  (Relic): why section 5 exists and why the hash is on from day one.

## 13. Staging

Each landing builds and passes the Debug suite, and keeps
tools/wry_smoke.py green.  The order puts the risky, novel piece (the
server as a sequential state machine over hostile bytes) early with a
differential oracle beside it (the old `LocalServer` path survives until
stage 3 proves the socket-pair host equivalent), per the house rule of
landing risky changes as brute-force-tested primitives first.

1. **Transport** (landed 2026-10-21 as `io/transport.{hpp,cpp}`:
   `Transport` with `LineCodec` and `FrameCodec`, the bounds, `kick`,
   `listen_unix`, `listen_tcp`, `connect_tcp`; the control socket is a
   `Transport` with the line codec, and `GuiContext` caps pipelined
   requests).  Extract the connection loop and the two codecs from the
   control socket; add the bounds and `kick`; re-base the control
   socket on them.  Tests: the existing control-socket suite
   unchanged in behavior; new tests for the frame codec (fuzzed lengths
   and tags, cap enforcement), inbound parking under a stalled consumer,
   outbound overflow, kick-with-reason delivery, a TCP listener and
   `connect` over loopback.  No game code touched.
2. **Action codec and step intake** (landed 2026-10-21: `Command` in
   `game/server.hpp` with ACTION/JOIN/LEAVE/PAUSED/RESUMED;
   `World::step(commands)` stages JOIN and LEAVE on a copy and advances
   with the actions on the `TransactionContext`; `Player::notify`
   applies its clients' actions, last write per cell winning;
   `World::_player_for_client`, save version 6; `format_action` /
   `parse_action` and `Type<Player::Action>`; `LocalServer` numbers
   the local client's actions and answers `request_join` with a JOIN
   step of its own).  `Type<Player::Action>` with
   round-trip tests; `World::step` takes the step's commands; `Player`
   loses its queue; the World gains the client-id map and the save
   version bumps.  `LocalServer` still feeds the step, now as a command
   list per step.  Tests: the existing player and world-state tests
   under the new intake; a save/load round trip with pending commands
   proves 9.2.
3. **Server and client** (landed 2026-10-21: `game/wire.{hpp,cpp}`
   is the protocol, `game/relay.{hpp,cpp}` the server as one task over
   its transport's inbox with its timers and controls posted into the
   same queue, `game/host.{hpp,cpp}` the `RemoteServer` that implements
   the seam over a connection and the `Host` pairing it with a relay
   over a socket pair; `WorldState` takes finalized steps each frame,
   applying a bounded share when behind; the clock commands address the
   relay; `--host`, `--join`, `host`, `join`, `leave`; `LocalServer` is
   gone.  Until stage 5, a joiner starts from the scenario's genesis
   and replays the relay's whole stream, which the relay keeps.)  The
   server task with its inbox, timer,
   cursors, caps and state machine; the client's `Server` implementation
   over a `Connection`; the host as server plus client over a socket
   pair; the clock commands addressed to the server; `--host`, `--join`,
   `host`, `join`, `leave`.  Single player runs through it and
   `LocalServer` is deleted when the smoke test passes.  Tests:
   in-process, a server with two or three clients over socket pairs,
   checking the prefix contract, arrival-order finalization, every kick
   reason, and that a violating client's later bytes are never
   processed; out-of-process, a second headless binary joins the first
   over TCP, both driven by the control socket (a sibling of
   wry_smoke.py), and both report the same `state-hash` after the same
   steps.
4. **Hash.**  The coarse form first (hash of the canonical save stream of
   a snapshot, every k steps, on a worker), `HASH` reports, kick the
   minority, `state-hash`.  Then the incremental form of 9.1, with the
   save-stream hash as its differential oracle, and k drops to 1.
5. **Join.**  Donation through the host's client, `SAVE_*` frames, load
   from bytes with a hardened loader, catch-up, the hash gate on
   `JOIN_REQUEST`.  Tests: in-process join mid-game with a hash check at
   B and at the head; the two-process script extended with a late
   joiner that issues actions after promotion; a deliberately corrupted
   transfer is rejected.
6. **Diagnosis.**  The host writes the finalized log; a tool replays a
   checkpoint plus log headlessly and bisects two logs' first divergent
   step by structural diff of the saves.  Deliberately after the core,
   and before anyone is invited to play.

Then the open items, roughly in the order of section 11.
