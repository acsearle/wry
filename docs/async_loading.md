# Async resource loading

The todo item: "Async resource loading; do we just load everything in
parallel or is there a benefit to serializing parts (like disk access).
Cache?"  Measured 2026-11-08 on branch todo/async-loading, with the
answer, what was changed, and what is left.

## How to measure

`WRY_LOAD_TIMING=1` in the environment prints one line per span on
stderr (core/load_timing.hpp):

    [load] +    234.7 ms main         9.7 ms  wait for asset preload

the time since main began, the thread, the span, its label.  Off, a timer
is a static load and a branch.  The spans cover main's start, settings,
the render context (shader library, fonts), the scenes' inits, the
WorldState (make_starting_world, hosting), every texture (decode,
premultiply, mip generation), every mesh, the symbol atlas and its field,
and each preload task.  `WRY_BENCH=1 client --test-only symbol_sdf_parallel`
times a symbol field build from the shipped atlas both ways (nothing is
written).

The runs: `--offscreen --no-tests --skip-splash`, `frame 30`, `new-game`,
`frame 60`, `quit fast`; medians of 5 Release (mac-release, no
sanitizers, -Os) and 3 Debug (mac-debug, ASan + UBSan, -O0) runs.  The
machine was shared and loaded (load average 100+), so single spans are
noisy to tens of percent; the shape is not.  The assets were in the page
cache (warm); a cold read could not be forced on a shared machine.

## Measurements

What the main thread does, in ms (median).  "Before" is main with only
the timers added; "after" is this branch.  Launch to menu did not change
(the 168 -> 138 is noise).

| Span (main thread)                                  | Release before | Release after | Debug before | Debug after |
|-----------------------------------------------------|---------------:|--------------:|-------------:|------------:|
| process start -> main menu built                    |            168 |           138 |          357 |         356 |
|   of which WryRenderContext init                    |             61 |            59 |          264 |         260 |
|     of which build_font (FreeType -> SpriteAtlas)   |             56 |            55 |          246 |         240 |
|   of which before the shader library (AppKit, device) |          ~100 |           ~80 |          ~95 |         ~97 |
| New Game: wait for asset preload (new)              |              - |            10 |            - |         273 |
| New Game: WorldState construction                   |             25 |            22 |          507 |         447 |
|   of which make_starting_world                      |             24 |            21 |          434 |         375 |
| New Game: WryWorldScene init                        |            770 |           326 |         1619 |         389 |
|   PNG decode (from_png, 12 large textures)          |            230 |             5 |          491 |           9 |
|   premultiply alpha                                 |              9 |           0.5 |          179 |          11 |
|   OBJ parse + vertexify (8 meshes)                  |             12 |             0 |          227 |           0 |
|   symbol field (from its cache PNG)                 |            143 |             2 |          398 |           3 |
|   label stamping + assets.json                      |             43 |            10 |           69 |          62 |
|   pipelines and device state                        |             19 |            13 |           53 |          33 |
|   GPU: the first blocking wait (see below)          |            250 |           263 |          201 |         249 |

The preload tasks, on the pool (4 workers), Release: about 230 ms of
work (two 4096^2 atlases ~40 ms each, the field from cache ~68 ms, ten
smaller images 3-16 ms each, eight meshes ~10 ms) finishing ~97 ms after
they are issued; Debug: ~1.4 s of work in ~390 ms.  The scripted run
issues `new-game` ~100 ms after the menu, so Debug still waits ~270 ms for
the tail; a person at the menu never does.

The symbol field cache miss (any atlas change), built from
assets_tile.png:

| Field build                  | Release | Debug (ASan) |
|------------------------------|--------:|-------------:|
| sequential (before)          |  557 ms |     61169 ms |
| one child task per cell      |  172 ms |     16611 ms |

Both builds are byte-identical (asserted by the test).

## The answer

- **Disk is not the cost.**  Everything the world scene reads is ~11 MB of
  PNG and OBJ (the 4096^2 symbol atlases are 0.4 MB files that decode to
  64 MiB each).  At SSD rates that is a few ms; the 37-51 ms Release
  decode of a 0.4 MB atlas is all inflate, unfilter and copy.  So there is
  nothing to gain from serializing reads: load everything in parallel.
- **The CPU work is decode and compute, and it parallelizes.**  PNG
  inflate, premultiplication, OBJ parsing, the symbol field: pure
  functions of their files, independent of each other and (the field) of
  each other's cells.  They now run as pool tasks started at launch, and
  the field build is one task per cell.  The output is the same bytes, so
  determinism is untouched.
- **What stays on the main thread** is not forced by Metal (device object
  creation and texture uploads are thread-safe) but by the code: the
  render context, its SpriteAtlas and font, the preload table and the
  scene's ivars are single-threaded, and the opcode labels are stamped
  into the atlas texture in place.  These Metal-side steps are cheap
  (texture creation + upload ~0.6 ms each, 8 ms for a 4096^2 atlas).
- **The GPU is now the largest single item.**  The first blocking
  `waitUntilCompleted` in the scene's init (the terrain albedo's mip
  generation) waits ~250 ms behind the image-based-lighting prefilter
  (prefilteredEnvironmentMapFromResource: 5 levels of a 256^2 RGBA32Float
  cube, rendered by accumulation, committed without waiting; measured
  ~270 ms of GPU time by waiting on it directly).  That is per New Game,
  for a pure function of day.png.
- **What is worth caching**: the symbol field already is (on disk, beside
  the atlas).  The IBL cube is the next candidate (per process in the
  render context, or on disk like the field).  Decoded images are not:
  re-decoding is cheaper than keeping 64 MiB atlases around, and the
  preload consumes each decode once.

## What changed

- `render/asset_preload.{hpp,cpp}`: AssetPreload, owned by
  WryRenderContext.  `image` / `mesh` / `symbol_field` start a task on the
  process scope; `take_*` waits for it (std::mutex + condition_variable:
  the publisher's unlock happens-before the taker's lock) and consumes the
  entry; `wait()` waits for everything outstanding.  A task destroyed
  unrun (scope stopped) still publishes (empty) from its parameter's
  destructor, so a take never hangs.  A missing file publishes empty and
  the synchronous path reports it as it always did (from_png aborts).
- `+[WryWorldScene preloadAssetsWithContext:]` requests the scene's large
  textures, meshes and field; WryDelegate calls it right after the render
  context exists.  `newTextureFromResource:...` takes a preloaded decode
  if one matches (path and premultiplication), the scene's mesh loader and
  `-loadSymbolAtlasNamed:` likewise; everything falls back to the old
  synchronous load, so a second New Game (preload consumed) behaves as
  before.
- `build_symbol_sdf_parallel` and `symbol_sdf_for_atlas_parallel`
  (render/symbol_sdf): the per-cell body factored out
  (`build_symbol_sdf_cell`, which writes only its own cell's block), one
  child per cell in a Nursery, joined before stroke_widths reads the
  image (the join's acquire of the children's release decrements orders
  their writes).  The blocking `symbol_sdf_for_atlas` is now a sync_wait
  on it, for the main thread (the `atlas NAME` switch); it must not be
  called on a pool worker.
- `GuiContext::on_before_world`: `new-game` / `load` at the menu call it
  before building the WorldState, and the windowed host waits for its
  preload there.  The self-host's handshake deadline (5 s) starts when the
  WorldState opens the connection, and the client's HELLO is only sent
  from main's frame loop (RemoteServer::take_steps), so a cache miss used
  to stall main inside the scene's init and get the client kicked.  Now
  the long work is either done before the clock starts or is not on the
  main thread at all.  (Not reproduced end to end: forcing a miss means
  touching the assets folder.  The ordering is the argument: nothing
  between WorldState construction and the first world frame waits on the
  pool any more, unless an asset was not preloaded.)
- `ext/coroutine.cpp`: `global_work_queue_schedule_destroy_by_address`
  was `inline` in a .cpp, so the Release build failed to link
  (metal_event.mm calls it); the `inline` is gone.

Costs and caveats:

- Memory: the preloaded decodes are held from launch until New Game,
  ~174 MiB (two 64 MiB atlases, the 16 MiB field, ~30 MiB of 1024^2
  textures) plus the meshes.  A session that never leaves the menu holds
  them.  While a field is being rebuilt the atlas is decoded twice at once
  (once for the texture, once for the build).
- Quitting from the menu in the first moments now joins the preload tasks
  (~0.1 s Release; up to ~17 s in Debug on a field miss; `quit fast`
  skips it).
- The preload competes with the background test suite for the pool in a
  live process without `--no-tests`.
- `day.png` is decoded twice by the scene (prefiltered map and irradiance
  map); only the first is preloaded.

## Follow-up: reuse across scenes, the `atlas` switch off main

Measured 2026-11-08 on branch todo/loading-reuse, the same way (Debug:
`frame 300`, `new-game`, `frame 60`, `main-menu`, `frame 10`, `new-game`,
`frame 60`, `atlas assets`, `frame 1`...; 3 runs Debug, 5 Release,
medians; the machine shared, load average 5-36).

The preload served the first world scene only: a second New Game (after
`main-menu`, or a load) decoded and uploaded everything again.  Now what
the scene makes from its files is kept for the process in
`WryRenderContext.assetCache` (beside environmentCache), keyed by file,
modification time and recipe (pixel format, alpha, mip filter), and
shared by every later scene:

- the mipmapped textures (terrain, container, sandbox, ship palette):
  every newMipmappedTextureFromResource variant now caches; the five
  constant-colour textures through the new sharedTextureFromResource;
- each mesh's vertex and index buffers (written once by
  newBufferWithBytes, only read by draws); the WryMesh around them, with
  its instance array and textures, is still made per scene;
- the cutout glyphs (assets.png, straight, kernel mips);
- each symbol atlas, finished: the texture with its opcode labels stamped
  and its mips generated, the field texture, and the stroke widths, keyed
  also by assets.json's modification time (it names the stamped cells).
  A cached atlas is installed by pointer swap; the scene still reads
  assets.json for its own coordinate tables, without stamping.

Why sharing is safe: nothing writes any of these after it is cached.  The
label stamping and the mip generation (blocking) happen before insertion;
the `atlas` switch replaces the scene's references, it does not edit the
textures; `caption-decals` swaps to another file (another key);
`ui-scale` rebuilds the font in the SpriteAtlas, which is not cached; the
per-frame state (mesh instances, the world map double buffer, G-buffers)
stays in the scene.  Frames still in flight from an old scene sample the
same immutable textures as the new one.

The `atlas NAME` switch no longer blocks the frame loop.  A cached atlas
swaps at once; otherwise -requestSymbolAtlasNamed: asks the preload for a
detached request (AssetPreload::symbol_atlas_detached: a slot outside the
table, so New Game's wait() never waits for it, held only by the scene
and the task), keeps the old atlas up, and polls the slot each frame
(Slot::ready(), under the mutex the task published under, so the decode's
writes happen-before the main thread's reads; take() cannot block once
ready).  The install on main (texture upload, stamping, mips) is the only
main-thread part.  Asking for another name, or the old one, abandons the
request: the task finishes and frees the slot.  On a field cache miss the
cells are built two at a time (build_symbol_sdf_parallel's `in_flight`):
the pool's queue is FIFO and the world step sync_waits on pool tasks of
its own every frame (WorldState::_apply_step), so 256 cells queued at once
stalled frames for the whole build (24 s in one run) although main never
waited on the build itself.  Two of the four workers: the build takes
about twice as long, and frames keep their pace.

A symbol atlas is now one preload request (AssetPreload::symbol_atlas):
on a field cache hit the image decode runs as a child while the field is
read from its cache; on a miss the field is built from the one decode,
not a second (symbol_sdf_for_atlas_parallel's `decoded`).  The field
cache's temporary is unique per call (pid + counter), so two builds of
one atlas at once (an abandoned request still running as a later scene
asks again) cannot write one file.

| Main thread, ms (median)                        | Debug before | Debug after | Release before | Release after |
|-------------------------------------------------|-------------:|------------:|---------------:|--------------:|
| first New Game: wait for asset preload          |          115 |         118 |              0 |             0 |
| first New Game: WryWorldScene init              |          419 |         418 |            351 |           351 |
| second New Game: WryWorldScene init             |         1305 |          10 |            274 |           1.2 |
| `atlas` switch, field cache hit: frame stalled  | 834 (2 runs) |   52 (none) |            196 |     49 (none) |
|   of which the install on main                  |         ~785 |          81 |           ~150 |            33 |
| `atlas` switch, field cache miss: main blocked  |        15200 |          81 |            ~550 |            33 |

The stalled-frame rows are the round trip of `frame 1` after the switch;
an undisturbed `frame 1` is ~50 ms Debug, ~44 ms Release.  After, the
switch lands ~0.46 s (Debug, hit), ~31 s (Debug, miss, two cells at a
time), ~0.65 s (Release, miss) after it is asked; during the Debug miss
build `frame 10` took 91 ms median, 131 ms max (undisturbed ~85).  The
misses are single runs (an atlas copied to a temporary name).

Memory: the cache holds 25 entries, 237 MiB, after the first world scene
(two 4096^2 RGBA8 atlases with mips, 85 MiB each: assets_tile stamped and
the assets glyphs; the 16 MiB field; ~40 MiB of 1024^2 mipped textures;
the 8 MiB prefiltered sky; the meshes), and ~101 MiB more for each other
atlas shown (338 MiB after one `atlas assets`).  While a world is up this
is no more than before (the scene held the same objects); what is new is
that it stays held at the main menu after the world is gone, where before
only the prefiltered sky (~8 MiB) stayed.  Nothing evicts: a session that
cycles through every atlas keeps them all.

## Plan (not done)

1. The IBL prefilter: compute it once per process (in the render context,
   at launch, without waiting) or cache it on disk; either removes ~250 ms
   of GPU wait from New Game.  Independently, the scene's mip generation
   need not block (one queue executes in commit order); with
   `shared_event_wait` (render/metal_event.hpp) it could await instead.
2. DONE (the follow-up above): `atlas NAME` decodes and builds on the
   pool and swaps when it lands.
3. make_starting_world (21 ms Release, ~400 ms Debug) runs on main at New
   Game; it is a pure function of the seed and could be built on the pool
   while the scene initializes, the hosting started after.
4. build_font (55 ms Release, 240 ms Debug, before the first frame):
   FreeType rasterization could run on the pool with a face per task,
   placing into the SpriteAtlas on main.
5. DONE (the follow-up above): one decode of the atlas serves the
   texture and a field rebuild.  Still open: decode the field cache PNG
   into the texture directly (it is copied once into a SymbolSDF).
