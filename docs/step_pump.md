# The step pump: world stepping off the render frame

Status: BUILT 2026-11-19 on branch `step-pump` (on top of `path-trace`).
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
| renderer (`WryWorldScene`), money line | `_world_to_render` | the same field, now the adopted snapshot |
| `get`, `dump`, `state-hash`, `pipette`, `machine_at` / `follow`, `follow_camera` | pop / push `_worlds` | read `_world_to_render` |
| `save_current` | pop / push `_worlds` | a Root copy of `_world_to_render` |
| map builder handoff (~1 Hz) | `_world_to_render` | unchanged |
| `step N` | target `_steps_taken + N` | target `applied + N` (the pump's live count); replies once the *displayed* count reaches it |
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
what `get` and `state-hash` answer for.  While the clock runs they may be
a step or two behind the stream; when paused, the two are the same once
the pump has drained (decision 27).


## 5. Publication

The pump publishes after every step it applies, and whenever its other
state changes (a save landed, a replay finished, a stream refused):

    struct Published {
        Root<World const*> world;          // == head
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
2. lock `published_mutex`, copy the snapshot into the displayed fields,
   move the messages out, unlock;
3. append the messages to the log.

`news` exists for the hosts that sleep when idle (offscreen, headless):
`GuiContext::idle()` is false while `news` is non-empty, and
`wait_for_input` waits on `news` (with the control socket) where it used
to wait on the transport inbox, which the pump now consumes.  Section 9.3
shows no publish is left unadopted while main sleeps.

The snapshot holds a Root, so the World it names stays alive however long
main takes to adopt it.  A World the pump has moved past is dropped when
the snapshot is overwritten and main's copy moves on; old Worlds are not
queued.  The deque `_worlds` is gone.


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


## 7. What the renderer interpolates against

`tnow = world_get_time(world)` of the **displayed** World, as before:
machines interpolate between `_old_time` and `_new_time` at the World's
own integer time.  Nothing else is consistent with the World being drawn.

With frames as fast as steps this looks as it did.  With slow frames each
frame shows the newest step, so motion advances in jumps of
frame-time x 60 steps, but never lags the stream, and never bursts and
catches up as the 64-step cap made it do.  With frames faster than steps
(ProMotion at 120 Hz), consecutive frames repeat a step.

A smoother clock is possible without touching the simulation: draw at
`tnow + f`, f the fraction of a quantum since the snapshot was adopted,
clamped below 1.  Every machine in motion in World t has `_new_time > t`,
so `t + f < _new_time` for f < 1, and the interpolation never runs past
the end of the move it is in.  That is multiplayer.md open item 11.6 (the
render clock), and decision 26: not built here.


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
  (the displayed World's count), `pumped` (the pump's live count),
  `time` (the displayed World's) and `relay` (the relay's head, hosting).

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


## 13. Other documents

multiplayer.md section 4 ("Clients keep two clocks") and section 8 ("The
client ... whose inbox the main thread takes once per frame") describe the
old arrangement; both are updated to the pump.
