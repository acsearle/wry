# Decisions waiting on Antony, 2026-11-08

Links are relative to this file (`docs/`); `~/Desktop/assets` is outside the repo, reached as `../../assets`.

The autonomous TODO run (52 commits on main since `0bc4468 todo 3`) stopped
at these items. Each one names where its context lives. They are ordered
roughly by how much they unblock.

## Hygiene (affects everything)

1. **The repo is on an iCloud-synced Desktop, and iCloud is writing conflict
   copies inside `.git`.** Today it made `index 2`, `refs/heads/... 2`, a
   `refs/heads/main 2` and a stray `WryWorldScene 2.mm`, which broke a build.
   One commit was also recorded with the wrong parent; that was repaired, and
   `git fsck` is clean. Move the repo off the synced Desktop, or rename its
   folder to end in `.nosync`? (The conflict copies are kept under the
   session scratchpad, not deleted.)
   **Decided 2026-11-09:** rename the repo folder to `wry.nosync` (Claude is doing this).
2. **Agent worktrees.** `~/Desktop/wry-wt-*` hold one branch each (all merged
   or recorded below) plus uncommitted comparison screenshots in their
   `docs/` folders. Their `build/` folders are already deleted. Remove them
   all with `git worktree remove` once you have looked at the screenshots?
   **Decided 2026-11-10:** remove each worktree once its decision is made. Done: 18 removed (their untracked screenshots moved to `~/.Trash/wry-worktree-screenshots-2026-11-10/`); the three whose decisions are open (codoc 18, session 14, sender 22) moved to `~/Desktop/worktrees.nosync/`, where future agent worktrees go (off the iCloud Desktop).

## Look and feel (screenshots in assets/ or the worktrees' docs/)

3. **Tile captions.** `caption-decals 100` draws Noto Sans Condensed (OFL)
   captions as decals instead of the Futura baked into `assets_tile.png`.
   Make it the default and drop Futura? Comparison:
   `wry-wt-strings/docs/captions_zoom{100,200}.png` (worktree since removed; the screenshots are in `~/.Trash/wry-worktree-screenshots-2026-11-10/`). That needs the palette
   and cursor captions done the same way first, and Noto is about 40% wider.
   **Decided 2026-11-09:** make the Noto decal captions the default and drop Futura (Claude is doing this now).
4. **Bloom.** It is no longer computed unless shown (`toggle-bloom`). As built
   it blurs the HUD too and, with no HDR highlights, reads as a haze
   ([`~/Desktop/assets/bloom_off_vs_on.png`](../../assets/bloom_off_vs_on.png)). Delete it, or move it before the GUI with
   a threshold?
   **Decided 2026-11-09:** delete bloom (Claude is doing this now).
5. **Indoor lighting.** `environment room` uses a procedural room
   ([`~/Desktop/assets/tools/make_room_env.py`](../../assets/tools/make_room_env.py)). The effect is mild while the sun
   dominates ([`~/Desktop/assets/environment_day_vs_room.png`](../../assets/environment_day_vs_room.png)). Move the sun to the
   window's direction and dim it indoors? Use an HDR window?
   **Decided 2026-11-10:** punt until the radiance-cascade experiment; the lighting will be made to fit the chosen algorithm.
6. **Clear coat** defaults to 50% on tiles and tokens (`coat TILES TOKEN`), at
   roughness 1/8. Keep it?
   **Decided 2026-11-09:** keep it.
7. **UI font** is now Noto Sans Medium ([`~/Desktop/assets/ui_font_weights.png`](../../assets/ui_font_weights.png)
   compares Light, Regular and Medium). Keep it?
   **Decided 2026-11-09:** keep it. The weights comparison shows the importance of *optical* weight; Antony will think more about that (todo note added).
8. **Ambient occlusion: pick one** (the existing DECIDE): GTAO, Luft, or the
   symbol decals. Radiance cascades are a fourth candidate
   ([`docs/radiance_cascades.md`](radiance_cascades.md): ambient yes, sun no; it lists 7 sub-decisions).
   A scale-free GTAO variant is also possible: the radius turned out to be
   world-space, not pixels.
   **Decided 2026-11-09:** GTAO; delete the others (Luft, symbol-decal AO) (Claude is doing this now). Later, in an interactive turn: implement radiance cascades to compare against GTAO, with an environment map that suits them (indoors, a large-solid-angle window, relatively high up, relatively close to the view direction, giving soft short shadows) and no directional sun or shadow map at all. Screen-space limits are fine. Height field: not yet, since the experiment has no sun (todo item added).
9. **Feedback item "U-turn is prominent and unhelpful"**: which U-turn? The
   TURN BACK tile's place in the palette, the U-turn in the DUMP glyph, or
   something else?
   **Decided 2026-11-09:** the TURN BACK tile. Left as a REMINDER item in todo.md for much later.

## Design

10. **Data-driven tables**: the four decisions in [`docs/data_driven.md`](data_driven.md)
    (presentation data now and simulation data later? ruleset hash or rules
    in the World? where the files live? who drafts the opcode help text?).
    Context help on hover depends on these.
    **Decided 2026-11-10:** presentation data now, in a new repo [`assets/`](../assets/README.md) folder (terrain.json, matter.json, opcodes.json; JSON keyed by short name); Claude drafted the opcode help for Antony to edit. Done: 6f885f6. LATER: move the text parts of `~/Desktop/assets` (.json, .py, .svg) into the repo (todo). Still open as **10b**: simulation tables (prices, dig times): ruleset hash in HELLO, or rules in the World? (`docs/data_driven.md` option 1 vs 2; prices in a per-client file would desync lockstep.)
11. **"Explode the stack" when following a truck**: what should it look
    like? (Click-to-follow is built.)
    **Decided 2026-11-09:** the stack's tiles are stacked tightly; explode means draw them vertically with enough spacing to read their faces, probably spaced as if they were cubes. Animating into and out of the state is a stretch goal (Claude is doing this now).
12. **Player write lost on a same-cell conflict**: retry by peek-then-pop,
    re-propose on abort, or accept the drop?
    **Decided 2026-11-10:** accept the drop; playtest to see how irritating it is. New open question (todo): what should happen when the cell changes during or after the turn the action is submitted, but before it is applied.
13. **Console output scrolling**: the log fills the screen, so scrolling it
    would take the scroll wheel from world panning while the console is open.
    **Decided 2026-11-10:** the console is modal (keys and mouse) while open, and the wheel scrolls its output. Done: 69d2a81. Playtest how annoying modality is.
14. **Named saves** (`save NAME`): saves are named only by file today, and
    names would need a format change.
15. **Asset cache memory**: decoded assets and the Metal objects made from
    them are now kept for the whole process (WryRenderContext.assetCache).
    That makes a second New Game's scene setup 10 ms instead of 1.3 s
    (Debug), and an `atlas` switch no longer stalls frames. The cache holds
    about 237 MiB after the first world, plus about 101 MiB for each other
    atlas shown, held even at the main menu, with no eviction. Is that
    acceptable, or should it be evicted when you return to the menu?
    **Decided 2026-11-09:** the memory is completely fine. New todo item: load all static assets as early as possible, before opening a world (splash first, then main-menu resources, then the rest; the first world render waits on loading), an aspiration to do later in stages.
16. **GUI test suite default**: `--no-tests` opts out today. Keep running the
    suite beside the game, or make it opt-in?
    **Decided 2026-11-10:** keep running the suite beside the game, as much as we can.
17. **UI scale**: add a control on the settings screen? The 32 pt cursor
    ignores `ui_scale`.
    **Decided 2026-11-09:** yes, add a UI scale control on the settings screen; the cursor must be bigger regardless, matching the displayed tile size in the palette (Claude is doing this now; open question: the performance of a large hardware cursor).

## Concurrency and correctness (needs your judgement on the contracts)

18. **Coroutine doc findings** ([`client/client/ext/docs/coroutines.md`](../client/client/ext/docs/coroutines.md); todo
    OPEN line):
    (a) a cancelled child's stopped hook retires before its parameter copies
    are destroyed, so a parameter destructor that touches joiner-owned state
    would be a use-after-free (no known instance);
    (b) `CancelableKEventFrame` can destroy the frame after `request_stop`
    returns, which contradicts the "synchronous cancellation" comments in
    `kqueue_reactor.cpp` and `metal_event.hpp`. Fix the comments or the code?
19. **Model checker** ([`core/kqueue_reactor_model.cpp`](../client/client/core/kqueue_reactor_model.cpp)): spawn's
    `fetch_or(STARTED, release)` and the Callback's acquire garnish are not
    load-bearing in the model. Relax them, or keep them as belt-and-braces?
    **Decided 2026-11-10:** relax all the things. Done: 37c1e1b (both relaxed; the old orders kept as passing model variants).
20. **TSan reactor hook**: the global release/acquire on
    `g_reactor_tsan_helper` is broader than the kernel's per-knote ordering,
    so it can hide races. Switch to per-knote `__tsan_release(this)` /
    `__tsan_acquire(udata)`? (That also ran clean.)
    **Decided 2026-11-09:** switch to per-knote release/acquire (Claude is doing this now).
21. **`shared_event_wait`** ([`render/metal_event.mm`](../client/client/render/metal_event.mm)): a stopped wait on a
    value that is never reached leaks a small state, because Metal cannot
    withdraw a notification. Should event owners signal `UINT64_MAX` when
    abandoning? It is also unverified whether a failed command buffer still
    signals its events.
    **Decided 2026-11-09:** yes, event owners signal `UINT64_MAX` on abandonment. Leaking on a failed command buffer is fine; abort on a failed command buffer if we can detect it (Claude is doing this now).
22. **`with_deadline`**: when an outer stop request races an inner that stops
    for another reason, the result can be nullopt or stopped; this is
    deliberate. OK?
    **Explained 2026-11-10** (in chat; summary): the timer only *asks* the inner to stop. Outcomes: inner value -> value (even after the deadline or a cancel); inner error -> error; inner stopped and the outer token reads requested -> STOPPED; inner stopped otherwise (timer, its own reason) -> nullopt; an inner that ignores stop overruns the deadline. The race: an unrelated outer stop concurrent with an inner stopping for another reason can be reported as nullopt (the caller runs its timeout branch, then unwinds at its next stop check) or as STOPPED. Nothing completed is ever discarded. Awaiting the OK.
23. **GC CRASH-1 trap tier**: the unpinned detector is now an assert, after
    clean sweeps. Strip the freed-object ring and quarantine tier too?
    **Explained 2026-11-10** (in chat). Recommendation: one recorded soak (mac-debug ASan suite x20 with WRY_GC_QUARANTINE=1), then strip the freed-object ring, walk labels and ASan narration (24 MiB + per-object stores in every Debug build; built only for CRASH-1), keeping the unpinned detector (move it out of the TEMP block) and keeping quarantine as a documented opt-in (~15 lines; fix it to require the value 1: today any value, 0 included, enables it).

## Waiting on something external

- `std::move_only_function`: Xcode's libc++ still lacks it.
- GenMC / relacy for the relaxed-memory checks: neither is installed, and
  installing needs the network.

## Raised 2026-11-09

24. **assert policy** (**Decided 2026-11-10:** (a), (b) and (c) as recommended; done, see done.md): the svg Release trap was the fourth round of
    side-effects-in-assert, all of them in tests (2,288 of the ~3,000 asserts
    are inside define_test bodies; every one of the 35 found today was in a
    test).  Recommendation, all three together: (a) never put side effects in
    assert, anywhere (now in CLAUDE.md); (b) keep running the suite in Release,
    since that is where the optimizer turns UB into traps (it found this one);
    (c) tests do not use assert at all: they use an always-evaluated check
    that prints expression/file/line before aborting, enforced by a lint that
    fails the build on `assert(` inside a define_test body.  (c) makes (a)
    moot where it has actually gone wrong, and means a Release run checks
    the same things as Debug.  Cost: a scripted rename of ~2,300 asserts,
    plus fixing whatever real Release-only failures the newly live checks
    turn up.

## Raised 2026-11-10

25. **DUMP under a parked truck** (found while drafting the opcode help):
    `plan_arrival` lets DUMP write into an empty cell even when another truck
    is parked on it (for example waiting on traffic); the truck then wakes
    and picks the value up. `machine_language.md` 6.7 and 8.11 say machines
    cannot write under an occupant. A truck on HALT is safe (its cell is not
    empty). Is the code or the doc right?
