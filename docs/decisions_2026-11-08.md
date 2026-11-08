# Decisions waiting on Antony, 2026-11-08

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
2. **Agent worktrees.** `~/Desktop/wry-wt-*` hold one branch each (all merged
   or recorded below) plus uncommitted comparison screenshots in their
   `docs/` folders. Their `build/` folders are already deleted. Remove them
   all with `git worktree remove` once you have looked at the screenshots?

## Look and feel (screenshots in assets/ or the worktrees' docs/)

3. **Tile captions.** `caption-decals 100` draws Noto Sans Condensed (OFL)
   captions as decals instead of the Futura baked into `assets_tile.png`.
   Make it the default and drop Futura? Comparison:
   `wry-wt-strings/docs/captions_zoom{100,200}.png`. That needs the palette
   and cursor captions done the same way first, and Noto is about 40% wider.
4. **Bloom.** It is no longer computed unless shown (`toggle-bloom`). As built
   it blurs the HUD too and, with no HDR highlights, reads as a haze
   (`assets/bloom_off_vs_on.png`). Delete it, or move it before the GUI with
   a threshold?
5. **Indoor lighting.** `environment room` uses a procedural room
   (`assets/tools/make_room_env.py`). The effect is mild while the sun
   dominates (`assets/environment_day_vs_room.png`). Move the sun to the
   window's direction and dim it indoors? Use an HDR window?
6. **Clear coat** defaults to 50% on tiles and tokens (`coat TILES TOKEN`), at
   roughness 1/8. Keep it?
7. **UI font** is now Noto Sans Medium (`assets/ui_font_weights.png`
   compares Light, Regular and Medium). Keep it?
8. **Ambient occlusion: pick one** (the existing DECIDE): GTAO, Luft, or the
   symbol decals. Radiance cascades are a fourth candidate
   (`docs/radiance_cascades.md`: ambient yes, sun no; it lists 7 sub-decisions).
   A scale-free GTAO variant is also possible: the radius turned out to be
   world-space, not pixels.
9. **Feedback item "U-turn is prominent and unhelpful"**: which U-turn? The
   TURN BACK tile's place in the palette, the U-turn in the DUMP glyph, or
   something else?

## Design

10. **Data-driven tables**: the four decisions in `docs/data_driven.md`
    (presentation data now and simulation data later? ruleset hash or rules
    in the World? where the files live? who drafts the opcode help text?).
    Context help on hover depends on these.
11. **"Explode the stack" when following a truck**: what should it look
    like? (Click-to-follow is built.)
12. **Player write lost on a same-cell conflict**: retry by peek-then-pop,
    re-propose on abort, or accept the drop?
13. **Console output scrolling**: the log fills the screen, so scrolling it
    would take the scroll wheel from world panning while the console is open.
14. **Named saves** (`save NAME`): saves are named only by file today, and
    names would need a format change.
15. **Asset cache memory**: decoded assets and the Metal objects made from
    them are now kept for the whole process (WryRenderContext.assetCache).
    That makes a second New Game's scene setup 10 ms instead of 1.3 s
    (Debug), and an `atlas` switch no longer stalls frames. The cache holds
    about 237 MiB after the first world, plus about 101 MiB for each other
    atlas shown, held even at the main menu, with no eviction. Is that
    acceptable, or should it be evicted when you return to the menu?
16. **GUI test suite default**: `--no-tests` opts out today. Keep running the
    suite beside the game, or make it opt-in?
17. **UI scale**: add a control on the settings screen? The 32 pt cursor
    ignores `ui_scale`.

## Concurrency and correctness (needs your judgement on the contracts)

18. **Coroutine doc findings** (`client/client/ext/docs/coroutines.md`; todo
    OPEN line):
    (a) a cancelled child's stopped hook retires before its parameter copies
    are destroyed, so a parameter destructor that touches joiner-owned state
    would be a use-after-free (no known instance);
    (b) `CancelableKEventFrame` can destroy the frame after `request_stop`
    returns, which contradicts the "synchronous cancellation" comments in
    `kqueue_reactor.cpp` and `metal_event.hpp`. Fix the comments or the code?
19. **Model checker** (`core/kqueue_reactor_model.cpp`): spawn's
    `fetch_or(STARTED, release)` and the Callback's acquire garnish are not
    load-bearing in the model. Relax them, or keep them as belt-and-braces?
20. **TSan reactor hook**: the global release/acquire on
    `g_reactor_tsan_helper` is broader than the kernel's per-knote ordering,
    so it can hide races. Switch to per-knote `__tsan_release(this)` /
    `__tsan_acquire(udata)`? (That also ran clean.)
21. **`shared_event_wait`** (`render/metal_event.mm`): a stopped wait on a
    value that is never reached leaks a small state, because Metal cannot
    withdraw a notification. Should event owners signal `UINT64_MAX` when
    abandoning? It is also unverified whether a failed command buffer still
    signals its events.
22. **`with_deadline`**: when an outer stop request races an inner that stops
    for another reason, the result can be nullopt or stopped; this is
    deliberate. OK?
23. **GC CRASH-1 trap tier**: the unpinned detector is now an assert, after
    clean sweeps. Strip the freed-object ring and quarantine tier too?

## Waiting on something external

- `std::move_only_function`: Xcode's libc++ still lacks it.
- GenMC / relacy for the relaxed-memory checks: neither is installed, and
  installing needs the network.
