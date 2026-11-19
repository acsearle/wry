# The step pump: world stepping off the render frame

Status: BUILT 2026-11-19 on branch `step-pump` (on top of `path-trace`);
the render clock (section 7) BUILT 2026-11-20.
Decisions for antony: [decisions_2026-11-08.md](decisions_2026-11-08.md)
items 26 to 28.  Measured: section 12.1.

The world now keeps pace with the server's step stream however slow the
frames are.  A *step pump*, one sequential coroutine on the worker pool,
takes the finalized steps from the connection and applies them as they
arrive.  The main thread renders whatever World the pump last published.
A slow path-traced frame, a long encode or a stall at `nextDrawable` now
makes the frame rate drop, and the world goes on at 60 Hz.


## 1. What it replaces

The relay's clock finalizes a step every quantum of wall time
(`Relay::Limits::quantum`, 1/60 s).  Until this change the client applied
those steps on the main thread, inside the frame:

    -[WryDelegate render]
      service_commands, handle_events
      WorldState::update
        take_steps()           the connection's state machine: handshake,
                               JOIN, STEP decode, donations, the save
        up to 64 steps         _apply_step: sync_wait(World::step), hash
                               report, log append, _worlds, replay check,
                               donations
      encodeIntoCommandBuffer  renders _world_to_render
      nextDrawable, commit

So:

- the world kept up only while frames took less than 64/60 s;
- the time spent stepping was added to every frame;
- steps arrived in bursts (one frame's worth at a time), so motion jumped
  by a frame's worth of steps;
- a stalled main thread stalled everything the connection does on the
  client side: the handshake (kicked: "handshake timeout" under heavy
  load), the hash reports (the tripwire, and the relay's view of our lag),
  the donations to a joiner.


## 2. The design in one picture

    reactor (R0) ----> transport inbox (MPSC) ---------------+
                                                             |
    pool:  STEP PUMP (one task per session) <----------------+
             take_steps / donations / save     (RemoteServer, locked)
             for each step in order:
               World::step (shielded; fans out over the pool)
               report_hash, log append, replay check, donations
               head = next; publish {head, steps taken, ...}
               yield to the pool
             park on the transport inbox
                     |
                     | publish: under published_mutex, then a news item
                     v
    main:  update(): adopt the published snapshot (once per frame)
             _world_to_render = snapshot.world  ... the renderer and
             every command read this, never the head
           encode, present

The main thread never applies a step.  It *adopts*: once per frame,
before anything reads the World, it copies the newest published snapshot
(a Root to the newest World, plus the few numbers that go with it) into
its own fields.  Everything the frame does then sees one World.


## 3. Where the pump runs

**A coroutine on the worker pool** (`step_pump` in
`game/world_state.cpp`), spawned into the process scope (`_gui.scope`)
when a session or a replay starts, one at a time per WorldState.  Being
one sequential task, it is its own strand: nothing else touches its state
while it runs (section 4), so it needs no lock of its own.

The alternative was a dedicated thread.  It buys nothing here:

- `World::step` is already a coroutine that fans out over the pool; a
  dedicated thread would `sync_wait` it, i.e. block while the pool does
  the work.  The pump depends on the pool's progress either way.
- A thread is one more mutator to pin, register (`thread_public`) and
  unpin around every wait, by hand; a pool task gets the workers'
  discipline for free (section 7).
- Waiting on the connection is an MPSC queue `wait()`, an awaitable.  A
  thread would need a second way to block on it.
- Shutdown is the scope's: the root's stop request destroys a parked pump
  like any other task, and the join waits for it.

The pool is shared with the map builder, saves, asset decodes and tests.
A saturated pool delays the pump; it delayed `World::step` before too.  If
that ever shows, a dedicated pump executor is a local change.


## 4. Ownership

The state splits in two.

**The pump's** (struct `WorldState::Pump`): everything the stream
touches.

| field | what |
|---|---|
| `head` | `Root<World const*>`: the newest applied World, the stream's position |
| `steps_taken` | steps applied since the World was installed |
| `backlog` | finalized steps taken and not yet applied (or a replay's log) |
| `awaiting_save` | a joiner's hold until its donated save lands |
| `donations` | donation requests not yet served |
| `log`, `log_directory` | the session log (multiplayer.md section 5) |
| `replay` | a replay's record and result |
| `client` | the RemoteServer, borrowed; null when sessionless |

**The main thread's**: the session's ownership (`_host`, `_remote`,
`_server`), the displayed snapshot (`_world_to_render`, `_steps_taken`,
`_awaiting_save`, `_replay_view`, `_log_path`), and everything that was
already main-thread-only (camera, hand, overlays, the map handoff).

The pump's state belongs to the pump task **while it runs**, and to the
main thread **while it is stopped**.  It changes hands only at the two
edges of section 9: the start (`scope.soon`) and the end (the run's
`ended` event).  There is no lock on it, because no two threads ever own
it at once.

Ending the pump is cooperative: main sets the run's `stop` flag and posts
a wake into the transport inbox; the pump finishes the step it is in (a
step is never cut short: `World::step` is awaited under `shield`), sees
the flag, publishes and ends; main waits for `ended`.  That wait is at
most one step plus one pool hop.  It happens only around the session's
lifecycle, which already blocks the main thread for longer (`_end_session`
waits up to 500 ms for the host's graceful shutdown).

A RAII `PumpStopped` (depth counted, so nested lifecycle calls work)
brackets each lifecycle entry point: stop, mutate, publish, adopt,
restart if there is anything to pump.

### 4.1 The main thread's users of the World

Each user of the old `_worlds` deque and of `_world_to_render`, and
where it goes:

| user | before | after |
|---|---|---|
| renderer (`WryWorldScene`), money line | `_world_to_render` | the same field, now the World the render clock chose from the adopted ones (section 7), drawn at `tnow - _render_before` |
| `get`, `dump`, `state-hash`, `pipette`, `machine_at` / `follow`, `follow_camera` | pop / push `_worlds` | read `_world_to_render` |
| `save_current` | pop / push `_worlds` | a Root copy of `_world_to_render` |
| map builder handoff (~1 Hz) | `_world_to_render` | unchanged |
| `step N` | target `_steps_taken + N` | target `applied + N` (the pump's live count); replies once the *displayed* World has that many (`_steps_drawn`) |
| `pause`, `resume` | the relay | unchanged (the relay's own inbox) |
| `write` (submit) | `_server->submit` | unchanged; RemoteServer is locked (section 6) |
| `session` | live fields | the snapshot, plus the RemoteServer (locked) |
| `replay`, `ReplayPending` | `_replay`, `_backlog` | stop the pump; install; the pump replays; the result rides the snapshot |
| `corrupt` | pop `_worlds`, push the edit | stop the pump, edit `head`, publish, restart |
| `install_displayed_world` (new game, load, join placeholder) | main | main, pump stopped |
| the donated save landing | main, in `update` | the pump: load, install, report its hash |
| donations | main, after every step | the pump, after every step |
| connection-state log lines | main, polling `state()` | unchanged (locked) |

Commands read the **displayed** World, not the head: what you see is
what `get` and `state-hash` answer for (decision 27).  While the clock
runs, that World is the render clock's delay behind the newest adopted
one, which is itself up to a frame behind the head: a step or two
locally.  When paused the three are the same once the pump has drained,
because a paused stream is drawn at its newest step (section 7).


## 5. Publication

The pump publishes after every step it applies, and whenever its other
state changes (a save landed, a replay finished, a stream refused):

    struct Published {
        Root<World const*> world;          // == head
        uint64_t lineage;                  // counts installs
        std::vector<Drawable> recent;      // each World head has been, once,
                                           // stamped (section 7)
        uint64_t steps_taken;
        bool awaiting_save;
        std::optional<ReplayView> replay;  // path, through, result
        std::filesystem::path log_path;
        std::vector<String> messages;      // log lines for main to print
    };

under `published_mutex`, a leaf lock held only to assign those fields.
Then it emplaces one item into `news`, an MPSC queue whose only consumer
is the main thread.

Main adopts in `WorldState::take_published()`, the first thing `update()`
does after repinning:

1. `news.try_take()` (and drop what it took);
2. lock `published_mutex`, copy the snapshot into the adopted fields,
   move `recent` and the messages out, unlock;
3. feed `recent` to the render clock and the ring, and choose the World
   to display (section 7);
4. append the messages to the log.

`news` exists for the hosts that sleep when idle (offscreen, headless):
`GuiContext::idle()` is false while `news` is non-empty, and
`wait_for_input` waits on `news` (with the control socket) where it used
to wait on the transport inbox, which the pump now consumes.  Section 9.3
shows no publish is left unadopted while main sleeps.

The snapshot holds a Root, so the World it names stays alive however long
main takes to adopt it.  The recent Worlds are held, by Root, for the
render clock: at most 64 in the snapshot and 64 in main's ring.  Past
that a World the pump has moved past is dropped (its stamp kept), so
however long main stalls the snapshot holds a bounded number of Worlds.
The deque `_worlds` is gone.


## 6. RemoteServer

`RemoteServer` was main-thread-only.  Now the pump drives its inbound
side (`take_steps`, which runs the client's state machine: handshake,
JOIN, STEP decode, donation requests, save reassembly; `report_hash`,
`take_donations`, `donate`, `take_save`, `disconnect`), while the main
thread still submits actions (`submit`) and reads its state for the log
and `session` (`state`, `client_id`, `head`, `reason`).

It gains a `std::mutex`, taken by every public method; internal paths
(`_on_message` resubmitting the actions queued before JOINED) use an
unlocked `_submit`.  `reason()` returns a copy.  The lock is held for a
batch's decode at most, and never across a suspension.  `wake()` posts a
no-op event into the transport inbox, which the pump consumes and
ignores: main's way to get a parked pump's attention.

Lifecycle calls (`leave`, `shutdown`, `dial`, `adopt`) happen with the
pump stopped.


## 7. The render clock: what the frame draws

Built 2026-11-20 (decision 26; multiplayer.md open item 11.6; the
2026-08-25 design, an audio-style jitter buffer).  The code is
`game/render_clock.hpp` (the clock, pure arithmetic) and
`WorldState::take_published` / `_choose_drawn` (the ring).

The frame does not draw the newest World.  Steps become drawable at
uneven wall times: the network, the relay's timer, and the pump's own
step times all add jitter.  If each step is drawn the moment it lands,
that unevenness goes straight to the screen.  At 60 Hz some frames show
two steps of motion and some show none.  At 120 Hz frames repeat a step
and then jump.  So instead the frame draws a moment of the world on a
steady clock.  The clock runs at the step rate, a small adaptive delay
behind the stream.

**The ring.**  The pump puts every World it publishes into the
snapshot's `recent`, once.  Each entry is stamped with the `steady_clock`
time the pump published it.  It is also stamped with whether the
stream's clock had stopped after it: the step carried PAUSED, or was
stepped while paused.  At each adoption main moves the entries out into
`_drawable`, which holds the last 64 Worlds (about a second) at
consecutive times.  If main is slower than that (a frame longer than 64
steps), the snapshot keeps only the newest 64 Worlds.  Older entries
keep their stamps without their World, so the clock still learns from
every arrival.

**Drawing between steps.**  The clock gives a fractional step number r,
and the frame draws World m = ceil(r) at the moment r, a fraction
`before = m - r` in [0, 1) of a step before its own time.
`WryWorldScene` puts each machine at `move_progress(machine, m, before)`
(machine.hpp) along its move from `_old_time` to `_new_time`.  The rule
is World m over (m - 1, m], not World n over [n, n + 1).  A step from
World m - 1 runs at `now() == m - 1`, so every move in World m was
decided at m - 1 or before (`_old_time <= m - 1`), and World m knows
where everything is from m - 1 on.  World m - 1 does not know which
moves begin at m - 1: drawn past its own time it would hold each machine
at its waypoint for a step, and then jump a step's worth along the next
move.  Under ceil, consecutive Worlds agree where they meet: World m at
its own time and World m + 1 a whole step before its own put every
machine in the same place.  Either it is on the same move at the same
progress, or it is at rest on the same cell (one move's end, the next
one's start).  `world_state_move_progress_continuity` checks this over
300 steps of the starting world, and fails for the floor rule.  At
integer r the frame is exactly the old integer drawing.  A digging
machine's shake is a function of the integer time, as before.  The
newest World is drawable only up to its own time, so r never passes the
newest step.

**The schedule.**  The relay finalizes step n at n·q plus a constant, q
being its quantum (from WELCOME: 1/60 s).  A step's *offset* is
`t - n·q`, where t is when it became drawable here.  The offset is that
constant plus the step's own delay: network, timer and pump.  Over the
last 120 arrivals (2 s):
- the minimum offset is the *base*: when a step arrives at the earliest;
- the *spread* (max minus min, in steps) is the jitter.

**The target.**  At wall time T the newest step the schedule promises is
(T - base)/q.  The clock aims a delay D behind it: `target = (T - base)/q
- D`, with `D = spread + margin`.  The margin is one step, because a step
arrives whole: the newest step's moment is reached only when the step
after it is due.  At that delay the latest arrival in the window would
still have been in time.  D is at most 30 steps (0.5 s).

**Hysteresis.**  D rises at once when the spread does: a late step is a
reason to buffer more now.  It falls at most a step per second, and only
once the late step has left the 2 s window.

**Rubber-banding.**  The clock runs at the step rate times
`1 + clamp(e·q / 0.5 s, ±10%)`.  Here e is how far the target is from
where the clock would be at the step rate.  A change in D or in the base
is absorbed by running up to 10% slow or fast, never by a jump.

**Fast-forward.**  If the target is more than 8 steps (133 ms) ahead,
the clock jumps to it.  Slewing that far at 10% would take ten times as
long.  Only an outage or a catch-up produces such an error.

**Underrun.**  r never passes the newest step.  A stream later than the
delay covers holds the picture on the newest World at its own time
until the next World lands (counted as an underrun).  The clock then
rubber-bands or fast-forwards on.

**Discontinuities.**  Some arrivals are not jitter, and are not learned
as it:
- **A gap.**  An arrival more than 0.25 s behind the schedule is an
  outage.  Neither it nor the burst that ends it is learned until
  arrivals are back within the delay.  The picture holds through the
  outage and fast-forwards after it.  A "gap" that has not closed in
  0.5 s means the schedule itself moved: the clock learns it afresh.
- **Early.**  An arrival more than half a step earlier than the base
  means the window was late, not jittery: a join or load catching up,
  its backlog applied faster than real time.  The clock learns afresh
  from that arrival, and the delay does not rise.
- **Paused.**  The clock draws a step the stream's clock stopped after
  exactly, at fraction 0.  So a paused world is a still frame (the path
  tracer converges), and `step n` while paused shows its step as soon as
  it lands.  Pausing jumps the picture forward by the delay, a step or
  two locally.  Resuming restarts the relay's timer, so the first step
  after it starts the schedule afresh.
- **A new lineage.**  An installed World (a new game, a load, a join, a
  replay) resets the ring and the clock.  The installed World is drawn
  as it is until steps follow.

**Who reads which.**  `_world_to_render` is the *displayed* World, the
clock's choice.  The renderer reads it, and so does every command,
since what you see is what you get (decision 27): `get`, `dump`,
`state-hash`, `save`, `pipette`, `follow` and the map.  `step n` replies
once the displayed World has the steps (`_steps_drawn`).  `_world_newest`
is the newest adopted World, read by `session`'s `time` and a replay's
progress.  While paused the two are the same.

**Determinism.**  The clock reads wall time, so which moment of a
running world a frame shows varies from run to run, as the newest step
did before.  While paused it is exact, so scripts and screenshots, which
pause, are unchanged.

**Tuning.**  The numbers are `RenderClock::Limits`.  Single player over
the socket pair learns a delay of under a step (section 12.1).  Over a
network D grows with the jitter it sees.  To tune it like an audio
buffer, watch `session`'s ` render R delay D underruns U snaps S`.  The
spread is a peak hold (the window maximum).  A high percentile would
resist a single outlier at the cost of more underruns.  That is
deferred until real networks show which is wanted.

**Latency.**  A local action shows D later than it would against the
newest World.  Locally that is under a step.  Over a network it is what
the jitter costs, and multiplayer.md's answer stands: an immediate
cosmetic acknowledgment of every local action.


## 8. GC obligations

The pump is a pool coroutine, so it runs only on pool workers, which are
pinned mutators whenever they run a task (`global_work_queue_service`:
pinned around batches of at most 10 tasks).  Every point at which the
pump resumes is on such a worker: the start (`scope.soon`), the inbox
wait (scheduled by `emplace`), the step's completion, and the yield.

- **Possession.**  Across every suspension the pump holds GC objects only
  through Roots: `head`, the World being stepped from (a Root copy), the
  step's result (`Root<World*>`), the snapshot's Root, and the Steps in the
  backlog (their actions hold `Root<Term>`).  No bare pointer outlives the
  task slice it was loaded in.
- **The step's epoch pin.**  `pin_global_epoch` brackets the `World::step`
  await, as `_apply_step_async` did, through an RAII guard.  The await is
  under `shield`, so the frame cannot be destroyed there, and the pin is
  always released.
- **Repin cadence.**  After every step the pump yields
  (`TransferToPoolExecutor`).  A long catch-up (a join replaying the
  stream, a replay, a stall) is then a sequence of short tasks, each
  counted in some worker's batch, instead of one task that keeps a worker
  pinned for thousands of steps and holds the epoch back.
- **Never blocking.**  The pump takes two leaf locks (`published_mutex`,
  the RemoteServer's), neither held across a suspension or around
  anything that waits.  It never waits on the collector.  It parks only
  on the transport inbox.
- **Roots across threads.**  The snapshot's Root is written by the pump
  and copied by main, both pinned; the count never reaches zero in
  between (the pump's `head` holds one).  An old World's last Root may
  drop on either thread; both are pinned mutators, so the shade is legal.
- **The ring.**  The render clock's recent Worlds are Roots, in the
  snapshot and in main's `_drawable`, at most 64 each: a second of steps
  kept alive.  The Worlds are persistent and share most of their
  structure, so a second's worth costs what the steps changed, not 64
  Worlds.  Roots dropped by the pump's trim are released after the lock,
  like the superseded head.
- **Main.**  Still repins once per frame.  It may block pinned in
  `_stop_pump` (one step at most), which holds the epoch back that long,
  as `_end_session` already does for longer.


## 9. Memory ordering

The synchronizes-with edges relied on, and the interleavings that need
them.

### 9.1 Ownership handoff

**Start.**  Main writes the pump's state, then `scope.soon(step_pump(...))`
pushes the frame onto the work queue (`BlockingDeque`, a mutex).  The
worker pops it under the same mutex: main's unlock synchronizes-with the
worker's lock, so every write main made to the pump's state
happens-before the pump's first read of it (coroutines.md section 1.2).

**End.**  The pump's last act is `run->ended.set()` (a guard, the first
local of the frame, so its destructor runs last, after every other local
Root has gone, on the normal path and on destruction alike).  `set()` is
an `exchange(acq_rel)`.  Main waits with `co_await run->ended` (async, for
`end_session`) or `coroutine::sync_wait` of it (blocking):

- if the event is already set when main arrives, main's publishing CAS
  fails, reading the set marker with `acquire`: it synchronizes-with the
  pump's release exchange;
- if main parked first, `set()`'s exchange reads main's frame handle and
  schedules it; the work-queue mutex orders the schedule before main's
  frame resumes (and `sync_wait`'s semaphore before main's thread does).

Either way every write the pump made happens-before main's next read, and
main owns the state.

**The stop flag** is `relaxed` on both sides.  It carries no data; what
makes the pump see it is the wake.  Main stores `stop = true`, then
`client->wake()` emplaces into the transport inbox under its mutex.  The
pump's `take_steps` takes from the inbox under that mutex, and only then
does the pump load `stop`.  If the take saw the wake, the emplace
happens-before the take, so the store happens-before the load, and by
write-read coherence the load reads true.  Interleavings:

    (a) pump parked on the inbox
        main: stop = true; wake()  -- emplace schedules the pump
        pump: resumes; take_steps (takes the wake); loads stop: true; ends

    (b) pump in the middle of a step
        main: stop = true; wake()  -- the item waits in the inbox
        pump: step done; publish; loads stop: true or false
              false: next loop take_steps takes the wake; loads stop: true
        pump ends after at most the one step it was in

    (c) pump about to park
        pump: loads stop: false
        main: stop = true; wake()
        pump: inbox.wait() finds the item, does not park; take_steps;
              loads stop: true; ends

    (d) sessionless (a replay): no inbox; the pump loads stop between
        steps.  The store becomes visible in finite time; the replay also
        ends by itself when its backlog is empty.

**Process shutdown.**  `root.request_stop()` (main, pinned) runs the
inbox wait's stop callback, which destroys the parked pump frame on main;
the guard's destructor sets `ended`.  A pump mid-step finishes the step
(shielded), and is destroyed at its next inbox wait (a requested token
never parks) or at its next stop check between replay steps.  The
WorldState, destroyed after the join (core/main.mm), finds `ended` set.

### 9.2 Publication

The pump writes the snapshot under `published_mutex`; main reads it under
the same mutex.  The pump's unlock synchronizes-with main's next lock.
The World the snapshot names was built by `World::step` on the pool and
completed into the pump's frame through the Future (its last writes
happen-before the pump's resumption), and sealed: so its contents
happen-before main's reads of them, by transitivity.  Main reads only;
Worlds are immutable once built.

### 9.3 No lost wake-up for an idle host

Claim: if main finishes an adoption and then finds `news` empty (and so
may sleep), it has adopted every snapshot published before that check.

The pump, per publish P: (a) write the snapshot under the mutex, (b)
`news.emplace`.  Main, per adoption: (1) `news.try_take()`, (2) read the
snapshot under the mutex; then later, before sleeping, `idle()` reads
`news.size()` (under the queue's mutex).

- If main's (1) took P's item, then P's (b) happened-before (1), so P's
  (a) happened-before (2): main read P or a later snapshot.
- If not, P's item is still queued, unless P's (b) comes after main's
  `size()` check, and then the emplace wakes `wait_for_input`.  So main
  does not sleep on P.

### 9.4 The live step count

`Pump::applied`, the count of steps the pump has applied, is an
`Atomic<uint64_t>` stored `relaxed` by the pump after each step and read
`relaxed` by `step N` to set its target.  It publishes nothing: main does
not read pump state through it.  Coherence guarantees main never sees it
go backwards; any value between the displayed count and the true head is
a correct reading of a concurrent counter.

### 9.5 RemoteServer

Every public method locks its mutex, so its fields are only ever touched
under it.  The transport's own thread-safety (send, end, post from any
thread) is unchanged.


## 10. Determinism

- Steps apply **in order, one at a time**: the pump is one sequential
  task; the backlog is FIFO; each step is checked to continue the head
  (`step.number == head time + 1`), as before; a step that does not
  disconnects.
- The head changes only by the pump applying the next step, or by main
  while the pump is stopped (install, corrupt), as the stream's own
  lifecycle.
- `World::step` and worldgen are untouched: worldgen and placement stay a
  pure function of the seed.  `make_starting_world` still runs on the
  main thread at a new game.
- What changed is presentation only: *which* World a frame shows.  The
  hash reports, the session log and the replay check see every step, as
  before.


## 11. Costs and limits

- Lifecycle commands (`new-game`, `load`, `join`, `leave`, `host` of a
  replay, `replay`, `corrupt`, cancelling a replay) block the main thread
  for up to one step to stop the pump.  In the tests, which run on pool
  workers, that blocks a worker briefly; the tests end the session by
  awaiting first wherever they did before.
- Catch-up is no longer capped per frame: a joiner replaying the stream
  applies it as fast as the pool steps it, publishing each step.
  Publishing is one Root assignment under a lock, so it does not
  dominate.
- One pump per WorldState at a time; a second start while one runs is a
  bug (asserted).
- The render clock adds a stamp and a Root per published World, and one
  `RenderClock::arrived` (a pass over the 120-entry window) per World at
  adoption: negligible.


## 12. Tests and measurement

- The world_state tests no longer apply steps by hand: they adopt
  (`take_published`) until the displayed count is where they want it.
- `world_state_step_pump`: with the main thread stalled (the test
  simply does not adopt) for well over 64 steps' worth of wall time, the
  pump's applied count keeps pace with the relay's head; adoption then
  jumps straight to the head.
- `frame-stall MS` (console): sleep the main thread that long in every
  frame, a stand-in for a slow encode, so the effect can be measured
  without a GPU-bound frame.
- `session` prints, besides `head` (the last step received) and `applied`
  (the adopted count), `pumped` (the pump's live count), `time` (the
  newest adopted World's), `render R delay D underruns U snaps S` (the
  render clock: the position drawn, the delay in steps, the frames it
  ran dry, the jumps), and `relay` (the relay's head, hosting).
- `render_clock_jitter`, `_gap`, `_pause` and `_catch_up` drive the
  clock with synthetic arrivals and frames:
  - jittered arrivals drawn at 120 Hz: once warmed up, no underruns, the
    rate within 10% of the step rate, and a delay just over the worst
    lateness; calm, it falls to its margin;
  - a one-second outage: the picture holds, fast-forwards once the burst
    lands, and the outage is not learned as jitter;
  - pause, a stepped burst, resume: exact newest steps while paused, and
    no jump or learned delay after;
  - a catch-up faster than real time: the delay never rises.
- `world_state_step_pump` also runs 40 frames on the live session: the
  displayed World is one of the recent ones, never past the newest, drawn
  a fraction in [0, 1) of a step before its time, never going backwards.
- `world_state_move_progress_continuity`: consecutive Worlds agree where
  they meet (World m at its time, World m + 1 a step before its time),
  over 300 steps of the starting world.

### 12.1 Measured

`tools/measure_step_pump.py` drives the offscreen client (Debug build,
`--offscreen --no-tests`, its own socket and settings): `load
"pt_reference"`, then phases of frames, sampling `session` back to back
(each reply waits for a frame, so the reply spacing is the frame
period).  "World" is the pump's live head on this branch and the applied
count on the base; "displayed" is what the frame drew; the lags are
the most each trailed the relay's head, in steps (60 a second).

    build  phase          samples  frame ms  relay/s  world/s  world lag  displayed lag
    pump   raster             905       6.6     60.0     60.0          1              1
    pump   stall 250 ms        30     272.6     60.1     60.1          0             17
    pump   stall 1500 ms       10    1534.5     60.0     60.0          1             93
    pump   path-trace         288      34.8     60.0     60.0          1              3
    base   raster             884       6.8     60.0     60.0          1              1
    base   stall 250 ms        29     280.8     59.9     59.9         18             18
    base   stall 1500 ms       10    1553.6     59.8     41.2        439            439
    base   path-trace         282      35.5     60.0     79.2        193            193

The base is `path-trace` plus only `frame-stall` and the relay's head in
`session` (branch `step-pump-base`).  Under 1.5 s frames it applies its
64 a frame, 41 steps/s, and after 15 s it is 439 steps (7.3 s) behind;
the next phase is it still catching up.  The pump applies 60 steps/s in
every phase and is never more than a step behind; what the frame draws
trails by one frame's worth (the snapshot is adopted at the frame's
start, and the `session` reply comes a frame later).

`path-trace` with the world running restarts its accumulation every
step, so it traces one sample per pixel per frame: about 31 ms of GPU a
frame here, not the 100-250 ms of a paused accumulation.  The stalls
stand in for the slower frames.


**The render clock** (2026-11-20, `tools/measure_render_clock.py`, Debug,
offscreen, pt_reference, single player over the socket pair).  Each
`session` reply is one frame.  The *rms* columns are each quantity's
RMS residual, in steps, from a straight line in wall time (fitted per
second, the phase's first second dropped).  `rms newest` is what drawing
the newest World would show; `rms render` is what the clock draws.
*Behind* is the mean of newest minus drawn; *delay* is the clock's at
the phase's end.

    phase        frames  frame ms  render/s  rms newest  rms render  behind  delay  underruns  snaps
    fast           1612       5.0      60.0       0.297       0.052    0.53   1.07          0      0
    stall 30 ms     128      47.2      60.1       0.317       0.245    0.67   1.46          0      0
    stall 250 ms     23     270.4      60.1       0.234       0.125    0.76   1.41          0      0
    fast again      797       5.0      60.1       0.294       0.051    0.65   1.06          0      0
    paused          560       5.4       0.0       0.000       0.000    0.00   1.06          0      0
    resumed         955       5.2      60.0       0.300       0.057    0.50   1.11          4      0

With fast frames the drawn position is about 6 times smoother than the
newest step: 0.05 of a step from a straight line against 0.3, the
sawtooth of whole steps landing unevenly.  The delay settles at about a
step (18 ms) and the picture trails the newest World by about half a
step.  Under slow frames the residuals are mostly the measurement: the
reply comes a frame after the frame's clock was read, and with a
30-270 ms sleep in between, that spacing varies.  Paused, the newest
step is drawn exactly.  Resuming cost 4 underrun frames while the
schedule was relearned, and nothing after.  The local stream has little
jitter; a network's would show in the delay, which this run cannot.

## 13. Other documents

multiplayer.md section 4 ("Clients keep two clocks") and section 8 ("The
client ... whose inbox the main thread takes once per frame") describe the
old arrangement; both are updated to the pump.  Open item 11.6 (the
render clock) is built (section 7).
