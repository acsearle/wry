# Multiplayer

Design for the networked game: the duties of the Server, the client
lifecycle, and the wire protocol, as agreed 2026-08-25.  Nothing below is
implemented yet except the seam it grows from (`game/server.hpp`:
`Server`/`LocalServer`, with `Player::_queue` as the per-step command
intake).

Status: normative design.  The direction is agreed; specific numbers and
small mechanisms remain open (section 10).  Section 11 compares against
prior art; the architecture is closely convergent with Factorio's and
OpenTTD's, which is intended reassurance rather than coincidence.

--------------------------------------------------------------------------

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

**No backpressure.**  The server advances on its own timer and never waits
for anyone: each step finalizes with whatever has arrived.  Clients keep
up or die (are kicked, and may rejoin).  This is the same policy that
governs the garbage collector, adopted with the same open eyes: designs
that refuse to wait are generally held to be a bad idea, and we accept the
risk deliberately, because every waiting design hands the slowest or most
malicious node control over everyone else's experience.  We will see how
it goes in the long run.

**Single binary.**  Determinism is guaranteed only among identical builds.
Version equality is enforced at handshake; there is no cross-version
compatibility ambition.

## 2. Roles

Three concepts, deliberately distinct:

- **Server**: orderer, relay, and clock.  It holds no World and does not
  simulate.  It is purely reactive -- socket events plus a step timer --
  and small enough to live directly on the network thread (section 8).
- **Client**: anything that subscribes to the stream and maintains a
  World by applying it.  All clients report state hashes and acks, and
  can donate saves.  A client with no player is a **spectator**.
- **Player**: an EntityID in the simulation, owned by a client.  Only
  clients-with-players submit actions or vote; spectators are passive.

**Dedicated server** = relay + a resident spectator client in the same
process (or at least the same box).  The spectator guarantees a save
donor is always available and provides a canonical hash reference, and it
needs no special machinery -- it is just a client.  **Single player** =
relay + one local client in one process; `LocalServer` grows into this
configuration of the real thing rather than remaining a sibling code
path.  Solo play runs the real serialize/deserialize loopback (in Debug
builds at minimum), so the wire format is exercised by every solo session
and cannot rot; the Debug suite is the correctness gate, as usual.

**Identity.**  The server assigns each connection a small **client id**
and issues a **session token**; reconnecting with the token within the
grace period reclaims the same client id.  The map from client id to
Player EntityID lives *inside the World*: joining rides the stream as a
server event (section 6), every client's simulation spawns the Player
entity on applying it, and deterministic EntityID allocation guarantees
they all compute the same id.  The relay never needs to know any EntityID
-- it could not, since it does not simulate.  A client finds its own
Player by looking up its own client id in its own World.

## 3. The step stream

Two origins of traffic ride the finalized stream:

- **player actions** (`Player::Action`), submitted by clients, ordered by
  the server;
- **server events**: player join and leave, pause and resume transitions,
  save-point markers -- authored by the server, carried as commands whose
  origin is the reserved server id.

Everything that mutates the World, or its clock, MUST ride in the stream.
Everything else (presence, pause votes, hash reports, acks, transfers) is
meta traffic and stays out of it, so that the stream alone replays the
game exactly.

**Finalization.**  `STEP{n, commands[]}` declares the content of step n,
possibly empty.  Empty steps are the common case and double as heartbeat
and clock.  `World::step()` consumes exactly one finalized step; a client
may hold several finalized steps not yet applied.

**Ordering is authorship, not consensus.**  The determinism obligation is
only that every client applies the identical broadcast.  The server's
choice of order within a step is free -- arrival order, round-robin,
anything -- because the broadcast *is* the definition of the order.
Nothing about the server's internal scheduling needs to be deterministic;
only fairness pressures constrain it.

**Sequencing.**  Each client numbers its actions with a monotonic
per-session sequence number.  The server keeps a next-expected cursor per
client: in-order arrivals are accepted (TCP makes gaps impossible while a
connection lives), duplicates below the cursor are dropped (resends after
reconnect), anything else is a protocol violation.  Actions beyond a
per-step cap are deferred to the next step, never dropped from the
middle; queue overflow kills the connection instead.  The resulting
contract is stronger than "in the order sent": **a client's finalized
actions are exactly a prefix of its submitted sequence, up to the death
of its session.**  Seeing your own action in a STEP is its
acknowledgment; there is no separate ack for actions, and a reconnecting
client resends everything past the last action it saw finalized.

**Actions are total.**  The server assigns actions to steps; a client
cannot target a step, and cannot know at submission which state its
action will meet -- or whether it will be finalized at all.  Every action
handler therefore validates at application time against whatever the
World then is -- target gone, tile changed, request stale -- and degrades
to best-effort or no-op.  No action may crash or diverge on any reachable
World.  This is a standing constraint on the action vocabulary as it
grows beyond `WRITE_VALUE_FOR_COORDINATE`, and it favors
coordinate-addressed actions (which age gracefully) over
captured-reference ones.

A corollary worth staring at: totality means diverged clients keep
playing indefinitely, their actions increasingly nonsensical as applied
to everyone else's World, and everyone else's to theirs.  Nothing
crashes.  Divergence is invisible except to the hash tripwire (section
5), which is why the tripwire is routine rather than exceptional.

## 4. Time

**The step rate is fixed and sacred.**  The server finalizes at a
constant quantum, regardless of anyone's simulation speed.  There is no
adaptive slowdown (Factorio slows the whole game to accommodate the
slowest client; we deliberately do not).  A client whose hardware cannot
simulate at the step rate cannot play; a client that falls behind catches
up or dies.  Same policy as section 1, same caveat.

Clients keep two clocks:

- the **step clock**: apply finalized steps at the step rate when
  current, or as fast as possible when behind -- catch-up runs the
  simulation without render or audio at full speed, always chasing the
  live head (there is no server accommodation to wait for);
- the **render clock**: draw a smoothed interval behind the newest
  applied step, absorbing network jitter.  The delay adapts to observed
  jitter with hysteresis, and corrections rubber-band (run slightly fast
  or slow) rather than jump; hard fast-forward is reserved for big gaps.
  This is an audio-style jitter buffer and should be tuned like one.

A happy client renders a few steps behind the head at a stable delay.  A
sad client degrades exactly as expected: deeper delay, pauses, visible
catch-up.  Gameplay absorbs what the network cannot: no twitch mechanics,
and immediate cosmetic acknowledgment of every local action -- the dust
cloud now, the tile change when the action lands.

**Pause is majority rule.**  A pause vote is meta traffic: the server
broadcasts "$name wants to PAUSE [ ]" and each player may set or clear
its own box.  When set votes exceed half the connected players, the
server emits PAUSED at the next boundary and stops finalizing; when they
fall back to half or below, RESUMED.  The transitions ride the stream
(the step clock is simulation-relevant and replays must reproduce it);
the votes do not.  Joining and badly-lagging clients may automatically
request pause -- catch-up against a stopped head always succeeds, so a
courteous group can rescue a struggling member, and a majority that
declines can keep playing.  While paused, heartbeats continue as meta
traffic: the step clock is stopped, the connections are not.

## 5. Desync

Divergence is a bug, full stop.  The machinery here exists to *detect and
diagnose* bugs, not to recover from them gracefully; the hope -- famous
last words -- is that day-one paranoia makes real desyncs rare enough
that the crude response never matters.

**The tripwire.**  Every k steps, each client hashes the World as of a
designated boundary and reports it.  Hashing rides on persistent
structure sharing: take the O(1) snapshot at the boundary, hash it on a
background thread (initial definition: the hash of the canonical save
byte stream, reusing the save walk), report when ready, tagged with the
step number.  Detection latency of seconds is fine for a debugging tool;
what matters is that the cadence is routine, because totality (section 3)
guarantees nothing else will ever surface divergence.

**The response.**  The server compares reports for the same step and
kicks the minority immediately -- no negotiation, no resync protocol.  A
kicked client may rejoin through the normal join path, which is the only
resync mechanism that exists.  Rationale, against the alternatives:

- pause-and-investigate lets any client stall the game by sending a
  deliberately poisoned hash;
- resync-the-minority builds recovery machinery whose main long-term
  effect is to hide the bugs we most need to fix;
- kick-the-minority makes a poisoned hash a self-kick and nothing more.

With no majority (two clients, or a tie), the reference hash is the
resident spectator's if present, else the oldest connection's.

**The artifact.**  Checkpoint + finalized log = deterministic repro.  On
mismatch the server preserves the log and names the offending step; any
client can be asked to dump state.  Diagnosis is then offline: two Worlds
stepping the same log from the same checkpoint, bisect to the first
divergent step, structurally diff the states (the save format round
trips, so diffing is mechanical).  Build this harness early -- every
published account of shipping this architecture (section 11) says desync
debugging is where the time goes.

## 6. Join, resume, leave

One mechanism at several depths: a client needs a World consistent with
some boundary B, plus the stream from B forward.

**Subscribe** (become a client; no player yet):

1. `HELLO`/`WELCOME` handshake: protocol version, build version, tier-2
   content hash (the prototype layering pays off: tier 1 must match by
   build, tier 2 by hash, tier 3 is the transferred save), session
   token, client id, step quantum, current head.
2. The server picks a donor -- the resident spectator if present, else
   any client -- and requests a save at boundary B.  The donor snapshots
   at B (O(1); donating is just the ordinary background-save path) and
   streams it through the server, which routes it to the joiner and tees
   the finalized stream from B onward.
3. The joiner loads the save, replays the teed stream at full speed, and
   is live when it reaches the head.  Catch-up must outrun the live game;
   it will, unless the hardware is simply below par (section 4's
   position: then it cannot play) -- or the group pauses (section 4).
4. A subscriber is a full client: it hashes, acks, and can donate.

**Join** (spectator to player): a `JOIN{client id, name}` server event
rides the stream; every World spawns the Player entity deterministically
and records client id -> EntityID; the client finds its Player in its own
World (section 2).  Only after JOIN does the client submit actions.

**Resume.**  The server keeps a ring of recent finalized steps.  A
reconnecting client presents its session token and "I have through N".
If N is inside the ring, the server replays the suffix and the client
rubber-bands back to live: the outage is invisible except as lag.  If N
has fallen off the ring, resume is impossible and the client rejoins from
scratch (fresh donation + catch-up).  The ring is nearly free -- steps
are input-sized, kilobytes per second for a full table of players -- so
retention is set generously (minutes).  The real bound is taste, not
memory: past some gap, loading a fresh save is faster than replaying the
suffix anyway, so deep retention buys nothing.

**Leave and kick.**  All exits look the same in the stream: the server
emits `LEAVE{client id}` (the fate of the abandoned Player entity --
idle in place vs despawn -- is a gameplay decision, currently open).
Exits happen:

- voluntarily (QUIT);
- liveness timeout: no traffic for T seconds (heartbeats make silence
  meaningful in both directions);
- slow consumer: the per-client outbound queue overflows -- keep up or
  die applies at the socket, too;
- desync (section 5): immediate;
- protocol violation (malformed frame, bad length, bad sequence):
  immediate.

A session token holds the client id (and any Player claim) for a grace
period G after connection loss; past G, LEAVE is emitted.

## 7. Wire format

Binary, little-endian, no alignment: fields memcpy in and out, in the
style of `io/binary.hpp` (whose header notes already name "actions to
network" as an intended use).  Wire and save formats share primitives but
version independently: the wire schema is negotiated per session and can
change freely; saves must stay readable for years.

**Framing.**  Every message begins `u32 length`, so the network thread
can partition a byte stream into messages with zero understanding; then
`u16 tag`; then the payload.  Length is capped per tag family -- small
for control and actions, large only for transfer chunks -- and an
out-of-range length is a protocol violation (death, section 6).

**The common header is minimal by design: length and tag.**  Everything
else is per-family:

- `ACTION` adds `u64 seq`, then the action body -- and the body is
  **opaque to the server**.  The server parses envelopes only: it orders,
  stores, and forwards action bytes without decoding them.  Only clients
  interpret bodies.  This is what lets the relay stay ignorant of the
  game schema; it depends on the protocol version alone.
- **Sender is not a field.**  The server knows who sent a message from
  the connection it arrived on; a client-written sender would be a lie
  waiting to happen.  The server stamps the origin's client id onto each
  command when rebroadcasting: `STEP{n, [{origin, seq, bytes}...]}`.
  Server events are commands whose origin is the reserved server id, so
  the log is uniform: the game is exactly the sequence of STEPs.
- **Transfers** (save donation) add a server-assigned transfer id and
  chunk index; the server routes chunks through a table it set up when it
  chose the donor.  There is deliberately no way for a client to address
  another client: the vocabulary is closed, and the relay is not a
  general message bus.
- **Step numbers** appear only in server-authored messages.  Clients
  never claim a step; the server assigns actions to steps (section 3).

**Vocabulary** (initial; will grow):

- client to server: `HELLO{versions, tier2_hash, token?}`,
  `ACTION{seq, bytes}`, `ACK{applied_through}`, `HASH{step, h}`,
  `PAUSE_VOTE{bool}`, `SAVE_DATA{transfer, index, bytes}` (donor),
  `QUIT`.
- server to client: `WELCOME{client_id, token, quantum, head}`,
  `STEP{n, commands[]}` (carrying player actions and, as server-origin
  commands, `JOIN`/`LEAVE`/`PAUSED`/`RESUMED`/save markers),
  `PAUSE_STATE{votes}` (meta, for the notification UI),
  `REQUEST_SAVE{boundary, transfer}` (to donor),
  `SAVE_DATA{transfer, index, bytes}` (to joiner), `PING`,
  `KICK{reason}`, `SHUTDOWN`.

The `ACK` stream earns its place three times over: it trims the resume
ring, measures each client's lag (kick policy, pause auto-request), and
estimates round trip for the render clock's delay buffer.

**Trust.**  The server parses hostile bytes; the framing and envelope
parser is a security boundary in a way the save parser is not.  Bounds
check everything, cap everything, die on violation.  Clients in turn
parse server bytes and donated saves -- a malicious donor can poison a
joiner, who will then fail its first hash and be kicked; the resident
spectator donor mostly retires this (open item 10.4).

What the server understands, in total: lengths, tags, sequence numbers,
client ids, step numbers, the transfer table, votes, hashes.  Never an
action body; never a World.

## 8. Process and thread architecture

One **network thread** per process owns all sockets.  Its duties:
framing (byte stream to whole messages and back), per-connection cursors
and caps, and the queues that connect it to the rest of the process.  It
traffics in bytes and plain structs only, and is **never a GC mutator**
-- it takes no part in the epoch protocol, per the standing rule that
foreign threads never run GC code.  Decoding a payload into
World-referencing types happens on the consuming side of the queue.

The Server itself is purely reactive -- socket readability plus the step
timer -- and holds no World, so its natural home is directly on the
network thread of whichever process hosts it: kqueue over the sockets
plus a timer source, with bounded work per wakeup so bulk transfer relay
cannot starve the step clock.  A dedicated server process is that thread,
a resident spectator client, and nothing else.

On the client side, game code polls or awaits the message queues (the
existing coroutine machinery applies).  The `Server` seam of
`game/server.hpp` keeps its shape: `poll()` grows the not-ready signal
its comment already anticipates, yielding zero or more finalized steps,
and `World::step()` consumes exactly one.  `Player::_queue` remains the
per-step intake it is today; the networked Server implementation fills
it from STEP commands instead of directly from local input.

## 9. Relation to current code

- `game/server.hpp` -- the seam survives as designed.  `LocalServer`
  becomes the loopback configuration of the real server rather than a
  sibling; `submit` grows sequence numbering; `poll` returns finalized
  steps rather than a bare command vector.
- `Command{EntityID player, ...}` -- on the wire a command names a
  client id, not an EntityID: the server never knows EntityIDs, and the
  client-id-to-Player map is World state (section 2).  Resolution
  happens at application time.
- `Player::Action` -- gains the totality obligation (section 3) and a
  serialization (the opaque body of `ACTION`) as it grows.
- `io/binary.hpp` -- the intended serializer substrate for the wire.
- Deterministic EntityID allocation (landed 2026-08) is load-bearing
  twice: JOIN spawns identical Player entities everywhere, and the state
  hash is meaningful at all.

## 10. Open items

1. Numbers: step quantum; hash cadence k; liveness timeout T; token
   grace G; ring retention; per-family length caps and per-client rate
   caps.
2. The precise hash: canonical-save-stream hash is the initial answer;
   an incremental/structural hash exploiting persistent sharing is the
   someday answer if full walks get expensive.
3. Player entity fate on LEAVE: idle in place vs despawn (gameplay).
4. Poisoned donation: a malicious donor hands a joiner a diverged save;
   the joiner dies at its first hash check.  Resident-spectator donors
   mostly retire this; revisit if peer donation matters.
5. Transport security: TLS over the TCP stream, or not; session-token
   secrecy depends on it.
6. Whether Release solo also runs the serialization loopback, or
   shortcuts it (Debug always runs it).
7. Transport evolution: framing is stream-agnostic on purpose, so QUIC
   (or UDP with bespoke reliability) can replace TCP per connection
   without touching the vocabulary if head-of-line stalls ever matter at
   our pace.  Not planned.
8. Spectating as a user-facing feature (observer UI, delayed spectating):
   nearly free under the model, unexamined.

## 11. Prior art

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
