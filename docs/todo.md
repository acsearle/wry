# Todo

## Things to build
- Terrain on demand: built 2026-11-08 (see done.md).  Left: the minimap only covers its fixed 256x256 window; the background frontier precompute, when the generator gets expensive
- `shared_event_wait` (coroutines that wait on Metal: built 2026-11-08; see done.md) follow-ups:
  - a block can wait in ~stop_callback while a requester destroys a coroutine tower, delaying the serial queue's other blocks
  - a command-buffer-completion awaitable (addCompletedHandler, before commit) if a caller wants status
- Async resource loading (answered: load everything in parallel; per-process caches; see done.md).  Left: decode the field cache PNG straight into its texture; the decal font (1.25 s Debug) is still rebuilt per scene on `caption-decals`; each world scene init places its arrow sprite into the shared SpriteAtlas again (never freed); make_starting_world and build_font run on main.  No eviction is fine (decided 2026-11-09, decisions_2026-11-08.md item 15)
- Load all static assets as early as possible, before opening a world (antony 2026-11-09, decisions_2026-11-08.md item 15; an aspiration / optimization, fine to do later in stages):
  1. load the splash image and the Metal shader to display it
  2. show the window and the splash; start the splash timer
  3. load the main-menu resources (font atlas, images)
  4. if the splash timer has expired, show the main menu
  5. start loading the remaining resources (meshes, tiles, ...)
  6. show the main menu when the splash timer expires, if not already shown
  7. the load / new world buttons are available immediately
  8. the first world render waits on asset loading completion
- SVG loading (built 2026-11-08; see done.md) follow-ups: gzip'd OT-SVG documents (no zlib in the io layer); no live use site (the atlas symbol sheet is still a PNG, and the drawBezier mesh path is still commented out); the OT-SVG root viewBox is ignored; nested <svg> is not clipped
- UI in points (built 2026-11-08; see done.md) follow-ups:
  - The cursor's size on a real display: check it looks right, and whether a large hardware cursor costs anything (decisions item 17's open question)
  - glyph outline radius and edge AA stay in pixels
- Decals: a normal-map image per decal (the instance's frame would carry it); emissive, occlusion and embossed normals are done (see done.md)
- Decouple the glyphs from the tile SDF.  Draw them all as decals (or, to optimize the common case, combine two textures in the raymarcher)
  - a larger default caption size wants a smaller art panel in tools/make_tile_atlas.py
  - one decal instance per glyph (~10 per tile) is unmeasured at map-edge zooms
  - the symbols themselves are still in the tile SDF
- Generalize the 2D interpolated + 1D analytic SDF to support full analytic.  Superellipsoids?
- Data driven: try to move properties out of the executable and into data files as much as possible.  e.g. prices, opcode \_displayed\_ names, help text etc.
  - PROPOSAL 2026-11-08: docs/data_driven.md (presentation tables now as JSON; simulation tables stay compiled until a scenario needs them, then become World state; four decisions listed)
- Context help for item under cursor: hover or side panel like Factorio
- Exploded stack: from the steep default camera each item still hides about half of the one below at a one-unit pitch; tune the pitch (or tilt the items toward the camera) if faces need to be read whole
- Commutative operations on World values.  Operation is applied when its transaction commits, concurrently, in any order.  Could be an atomic add on the value in the new World, for example.  Money is an example.  Attempt a widthdrawal if enough money \_now\_; but if enough people do it on the same turn, goes negative.  Don't try to prevent that, just penalize it - apply overdraft or interest every "month" based on lowest balance.  Fancier implementation for busy variables is to apply the deltas to thread locals and merge at the end of step.
- Help text, display text, etc. needs a to have meta that allows the easy inclusion (and rendering) of glyphs and keycaps and stuff.
  - keycaps done 2026-11-08 (see done.md).  Next: glyphs (opcode / matter symbols inline)
- If chemical formulas become an important part of the way we present stuff, we need subscript support

## Things to change
- Implement radiance cascades to compare against GTAO, in a later interactive turn (decided 2026-11-09, decisions_2026-11-08.md item 8; investigation in docs/radiance_cascades.md).  With an environment map that suits them: indoors, a large-solid-angle window, relatively high up and relatively close to the view direction, giving soft short shadows; no directional sun and no shadow map at all.  Screen-space limits are fine.  Height field: not yet, since the experiment has no sun
- Tune the lighting.  Environment map is currently a daytime desert; try indoors with a window.
  - DECIDE (decisions_2026-11-08.md item 5): move the sun to the window's direction and drop its strength indoors, or an HDR window (LDR caps the window at 1).  `environment room` is built (see done.md)
- Make the palette a GUI widget
- Console widget (built 2026-11-08; see done.md) follow-ups: scrolling the output (the log fills the screen, so a scroll over it would have to stop panning the world while the console is open: a decision, decisions_2026-11-08.md item 13); dimensions are still font-derived (half a line) rather than points
- Optical weight of the UI font: Noto Sans Medium is kept (decided 2026-11-09, decisions_2026-11-08.md item 7), but the Light / Regular / Medium comparison (assets/ui_font_weights.png) shows the importance of *optical* weight; antony will think more about that

## Bugs
- Release `--test-only` traps (SIGTRAP, exit 133) in an svg.cpp test coroutine (`svg::$_6` resume + 220, a `brk #1` after only Subpath-vector destructors: at -Os with NDEBUG the compiler reduced the test body to unreachable, i.e. it proved UB on that path; likely svg_arc or svg_rasterize, whose bodies are mostly asserts).  Already present at 28e331f (found 2026-11-09; the Release suite was last seen passing before the SVG merge).  Debug passes
- GTAO scale is specified in pixels?  (Why does it even have a scale, should be scale-free exponentially spaced samples?)
  - Not pixels: a world-space falloff (analysis 2026-11-08; see done.md).  DECIDE: a scale-free variant (exponentially spaced taps out to the screen edge through the depth pyramid, no falloff) is a design choice, not a bug fix

## To be decided
- Power, water, heat want to be partial differential equations.  Not allowed--simply too expensive to compute.  But even a simple global scalar might be heavily contested.  If we think about combining trees, maybe what we can do is have discrete lumps move around.  Building has a buffer of electrictity; occasionally requests top-up electricity from its local substation; likewise the substation holds a buffer it requests from superstation etc., from a power station.  No node has too many attachments, contention is low, and blackouts are local but contagious.
- How to display matter.  Just the name, an abstract shape, a molecule, a "mineral sample", a bucket o' stuff?  (Factorio is inconsistent here)
- How to distinguish ghost Terms via some simple visual effect
- How to display temporary cosmetics to cover up server round-trip.  Possibly, a renderer-scoped table of temporary modifiers to be applied while drawing the world.
- How to handle pop-in pop-out of Terms as they are mined, shipped, picked up etc.

## Other
- IN PROGRESS 2026-11-09 (decided, decisions_2026-11-08.md item 1): rename the repo folder to wry.nosync (iCloud was writing conflict copies inside .git)
## Won't do
Things that have been decided against
- Non-shadow-casting fairy light shader for running lights, headlights, etc.
- Fog, dust, extinction
- Cloud, smoke shadows projected by the directional light
- Tilt-shift

## 

-
List of one-line reminders of things to think and do
- Console line editing (landed 2026-11-05; see done.md) follow-ups: try Cmd-C / Cmd-V / Cmd-Z in the window once (real Cmd chords through -keyDown: and the NSPasteboard read / write were never driven); NOT DONE (line_editing.md section 4): dead keys / input methods and grapheme clusters (pointless while the atlas font is ASCII only), Ctrl-R, a candidate popup, a kill ring, scrolling the output, Windows / Linux spellings of copy / paste / undo; more parameters could declare words (file names for `replay`, the atlas names)
- Zoom (landed 2026-11-05; see done.md) follow-ups: map view near 0.5 should probably still show the minimap and palette; while paused, several pinch events in one frame coalesce and are then clamped to a halving / doubling
- FEEDBACK (2026-11-01): Tile captions too small (zoom?).  (2026-11-08: `caption-decals 130` tries larger ones; see the glyph-decoupling entry.)  The numbers half is done (see done.md)
- REMINDER (for much later; decisions_2026-11-08.md item 9): the FEEDBACK (2026-11-01) "U-turn is prominent and unhelpful" means the TURN BACK tile
- TODO: point light shader.
- TODO: Draw tiles in one pass and symbols as decals, because: we need combinatorial numbers, strings.
- TODO (2026-11-01): the look-iteration console commands are debugging aids to retire once the look is decided: `board`, `exposure`, `atlas`, `token`, `toggle-decals` (world_state.cpp) and their WorldState fields (_board_*, _exposure, _symbol_atlas, _token_*, _symbol_decals); fold the chosen values into constants.  The runtime opcode-label stamping in WryWorldScene -loadSymbolAtlasNamed: writes into the cell margin the slabs never show and is superseded by the captions tools/make_tile_atlas.py prints on the tiles; remove it with them
- Symbol slabs (landed 2026-10-29; see done.md) follow-ups: slab edges are aliased (one march per pixel, no coverage); thickness = stroke width capped at 20 texels (solid tiles; 32 put their undersides below the ground) -- revisit the cap and whether fillet radii should be global rather than per symbol
- GTAO follow-ups: retire the measuring aids `pyramid 0` and `ssao-repeat` when no longer wanted.  Unpursued lever if the GTAO floor matters: fold the upsample's depth test into fewer reads, or run the composite's AO fetch at half res via a sampler (see done.md)
- BACK POCKET (2026-10-30): directional occlusion in place of AO * IBL.  The ambient term is the hemisphere integral of V * L * f * cos; AO times an irradiance lookup factors it and is only exact for an untextured sky.  Cut the middleman: keep the GTAO slice march but read the sky per visible sector (visibility-bitmask slices, Therrien/Levesque/Gilet 2023, after HBIL and SSDO) at the mip the sector's solid angle implies (Colbert-Krivanek filtered importance sampling), one-bounce colour from the pixels the march hits, and for specular a GGX visible-normal direction marched in screen space (SSR on hit, prefiltered cube on miss).  Diffuse part can run at half res if it outputs irradiance and the composite multiplies by full-res albedo.  Steps in order: bent normal into the irradiance lookup (free from GTAO), bitmask sectors with sky lookups, VNDF march on the metals, then temporal reprojection (no motion vectors or history buffer today) once the noise demands it.  Worth it only if the final sky cube is strongly textured (the dark room with a big window) or low-roughness surfaces are common; the prize is dropping the directional light and shadow maps and having lighting just work for our simple scenes.  Not decided
- HOLD (2026-10-29, Antony): the opcode palette is old, retrofitted as a widget; invisible to `widgets`/`press` (coordinates or `hold` reach it); make it a proper widget later
- Session commands (built 2026-11-08; see done.md) follow-ups: user-named saves (`save name`; decisions_2026-11-08.md item 14), a `delete-save` command (the Load list's Delete calls delete_game directly), a real save picker for the main menu's Load Game (still the newest)
- IDEA (2026-10-29): screenshot could grow crop / per-widget / off-resolution options (today cropping is the consumer's job on the PNG it writes)
- WryWorldScene _mapTexture[2] double buffer: the same hazard as the SpriteAtlas::commit one (fixed 2026-11-08; see done.md), NOT fixed: replaceRegion into the back texture is safe only because map builds are >= 1 s apart, not by a GPU edge (stamp the texture with the arena's frame value when sampled and defer the upload until signaledValue reaches it).  Also: if SpriteAtlas::release is ever called, the freed region needs the same stamp before reuse
- Full implications of the next_ready GC bug; clean rederive from epoch formulation
- The save's inline waitset count is still found by a second traversal of each tiny inner set (flat AMT saves landed 2026-09-25; see done.md): switch to ArrayMappedTrie::_size (landed 2026-09-28), or reserve a u32 and patch it after the members (IDEA 2026-09-27; Antony wanted to understand the format better first)
- DEFERRED 2026-09-27 (Antony): the Saver buffers each record body and then copies it into the in-memory stream: one extra copy of every byte, and transient memory equal to the largest open body (the biggest map record).  Write-then-patch lengths are incompatible with a compressed output stream.  Options when it matters: chunk big maps into bounded records (SortedBuilder streams across chunks), stream closed records through the compressor as they close (peak = one open body, not the whole stream), or compute lengths up front
- Hash follow-ups (the live World hash landed 2026-10-20; see done.md): a tool reading clang's AST to check every GarbageCollected class's members against its _fields (deferred by Antony 2026-10-20); loaders are still hand-written field by field; seal-at-commit hashes the entity on the leaf's thread with a cold cache (a hot-cache seal at the end of notify would need the interpreter to finish the clone before proposing it)
- Grep every `fork(` for a temporary passed by reference into a forked coroutine (the LESSON of the 2026-10-20 World::step fix (see done.md): a Future's reference parameters must outlive the nursery join)
- NOTE 2026-10-20: `xcodebuild -scheme client -configuration Release` builds WITH ThreadSanitizer and UBSan, because the scheme's Launch action has both enabled and xcodebuild honours that for any configuration (the TSan Release stepped the starting world at ~1.2 ms/step; the sanitizer-free one at ~20 us).  For profiling pass `ENABLE_THREAD_SANITIZER=NO ENABLE_ADDRESS_SANITIZER=NO` (UBSan's nullability check stays on from the project's CLANG_UNDEFINED_BEHAVIOR_SANITIZER_NULLABILITY) or use Xcode's Profile action
- Does AMT enforce no null values?
  - Do all desired value types have a null value?
  - Pros/cons?
- ThreadPublic should probably not be GC and be more like a Crossbeam list
- The `_ready` skiplist should probably have a per-object allocator
- Transactional non-exclusive commutative operations, like addition
- Strip the WRY_GC_DEBUG crash-trap tier once the UAF stays silent (condition in garbage_collected.cpp banner)
- Coroutine / reactor / sender sessions (2026-08-29..09-11), still open, rough priority order:
- Unwritten for want of a use: shield with a budget (shield(f, s.get_token()) + cancelable_after(s.get_token(), d, [s]{ s.request_stop(); })); a runtime-N race (the entrant pack is compile-time; a nursery covers runtime N)
- Move the Senders section of coroutine.hpp (concepts, Just, Future as sender, SenderAwaitable, stopped_as_optional/shield, race, sync_wait) into ext/execution.hpp, by analogy with std::execution -- Antony 2026-09-12, when the seam settles; then and continues_on from the deleted toy layer are small to reintroduce on the real seam if a caller wants them
- get_stop_token walks the delegate chain to the root per leaf await -- cache in the promise if depth grows.  (The five Basic+Mixin delegate pairs collapsed onto OutcomeDelegate<Derived, T> 2026-09-11; MixinPromise is the remaining T/void pair, on the forwarding side, and AwaitableReceiver's set_value overloads are the receiver-side echo of it)
- Pending-arm accounting: an arm (cancelable_after) made from outside any wait-group task is untracked -- its knote dies silently at close(kq) and its frame leaks (arms made inside a task are cancelled by the root token since 2026-10-15; see done.md).  Open: the GUI's background test run is cut off by quit at its next reactor wait, silently -- fine for quit, but worth a log line; and sync_wait callers outside any task still see a detached token
- Coroutine doc findings (OPEN 2026-11-08, unverified; the doc client/client/ext/docs/coroutines.md is done (see done.md); decisions_2026-11-08.md item 18): (a) the stopped hooks (nursery.hpp:37, :286-292) retire from inside the child's ~BasicPromise, before its parameter copies are destroyed, so a cancelled child whose parameter destructor touches joiner-owned state would be a use-after-free (no known instance); (b) CancelableKEventFrame can destroy the frame on the pool or in spawn after request_stop returns (fire-won / arming windows, kqueue_reactor.cpp:184-204, 321-345), contradicting the "cancellation is synchronous" claims at kqueue_reactor.cpp:713 and metal_event.hpp:29-31 -- fix the comments or the code; (c) ctrie.cpp:88-152 reuses one Nursery across three joins (dead test code after a co_return)
- Reactor TSan / model-checker follow-ups (TSan pass and model checker done 2026-11-08; see done.md):
  - stale tsan_interface.h include in ext/coroutine.cpp
  - spawn's fetch_or(STARTED, release) and the Callback's acquire garnish are not load-bearing in the model: relax them, or keep them as belt and braces (decisions_2026-11-08.md item 19)
  - GenMC / relacy for the relaxed-memory claims: not installed and not installable offline
- set_delegate's Debug diagnostic names the colliding delegates; the one-time GUI firing (first run after pulling the port) is most likely a mixed incremental build -- if it recurs the message says which pair
- main.mm spawns the full test suite in the GUI unconditionally (4-5 s under ASan with the small world; QUIT waits for it, and must: the runner is shielded from the root token since 2026-10-17, because tests own nurseries across sleeps and a cancelled test frame asserts in ~Nursery -- the crash-on-quit of 2026-10-17): gate it behind a flag
  - NOTE 2026-11-08: a flag exists (--no-tests opts out); what is undecided is the default -- keep running the suite beside the game, or make it opt-in
- ext/functional.hpp: wry::move_only_function<R(Args...)> (subset: plain signature only, no in_place_type, empty call asserts) -- retire in favor of std::move_only_function when libc++ ships it
  - CHECKED 2026-11-08: Xcode's libc++ still lacks std::move_only_function under -std=c++23
- Nits: test metadata prints interleave with collector logging (flaky counts in log greps, bit again 2026-09-11)

- Multiplayer follow-ups (stages 2-6 and the 2026-10-25 review are done; see done.md):
  - a JOIN's Player first acts in the step after, so an action in the JOIN's own step is dropped (the server never produces one; LocalServer makes the JOIN its own step)
  - liveness: a PING while paused is unsent; a liveness timeout on the hash stream (deferred: a stalled host frame must not kick the host's own client); a liveness exit after HELLO (slot exhaustion by idle connections)
  - transfer flow control (multiplayer.md 11.12); semantic validation of a loaded World (11.13)
  - the checkpoint is serialized on the main thread at install (11.14); no in-process diff of two dumps (an external diff)
  - build_id is wire.cpp's compile time; quit closes sessions brutally (clients see EOF, not SHUTDOWN); log rotation can unlink another process's live log; a second concurrent `replay` confuses the first's pending reply; frame caps are per tag not per direction
- Player drops a queued write when its transaction loses a same-cell conflict (popped before resolution, never re-proposed); liveness fixed 2026-09-18 with WAIT_ALWAYS, retry is a design choice: peek-then-pop on the next notify, or WAIT_ON_ABORT re-propose, vs. accept the drop as the exclusive-write contract

## Old below

Clean this up sometime:

Interface

- How to hide delay of anything drawn in Metal to represent cursor

- Improve text rendering 
  - linebreaking
  - italics and subscripts
    - italics for vessel names
    - subscripts for molecules
    - in fact, basic latex / markdown / html?
  - sdf
  - glyph-on-demand
  - packer as cache?

Transactions
- Inspired by transactional memory, detect conflicts and retry
- Each tick processes a maximal (but not necessarily optimal) set
  of ready operations that are (?)commutative / orthogonal / compatible with
  reordering.  Conflicting operations are retried next tick.
  - Nice feature: speed of light.  Information can't travel more than one
    entity's reach per tick.
- This has the potential to be both deterministic and parallelizable, if we
  can do the partition right.  Determinism paramount, we can sacrifice some
  fairness.
  - Partition into spatial pattern, relying on locality of reads.  Strips for
    example
- We need to mark "things" as read or written this tick.  We can't overwrite
  values that somebody else has read, nor can we read values that somebody
  else has written.   Thus each memory cell is read by many xor written by one
  (or the most common case, untouched).
  - Seems bad to mark reads, is there an alternative? 


IDs vs pointers

- For serialization and lookup, we have unique IDs (and unique Coordinates)

- In some contexts we can use pointers directly if we can guarantee "iterator
  stability".  Seems likely for agents, not for tiles
  - Interaction with checkpointing mechanism?
  
- Cold storage by ID for things not used by simulation
- Hot storage in main struct

- Classic SoA vs AoS problem; ECS; eventually, database?

Checkpointing

- How do we maintain a checkpoint while we save it, without imposing undue
  overhead on the simulation?
  - Copy on write will produce a big spike of activity on next frame
  - Can we maintain a pending write list or an undo list? 
  - Can we maintain two simulations, one that progresses more fitfully?
    - Is holding a checkpoint gonna be equally bad in the worst case as just
      having two copies of the simulation anyway (?!!)
    - How much stuff is immutable or rarely changing?
  - Persistent data structure with one extra cell trick? 


sim::Tile

- how do we efficiently store the rarely-nonempty queues associated with tiles
  - side tables (again ECS)
- are chunks good?  Do we actually use spatial locality a lot given that we are
  working on scattered agents?
  - spatial locality is a huge deal for visualization, which is perhaps less
    important (since it is bounded)
- transactional memory / atomic actions seems to be neat

- do we need to support pimpl, or can we use ECS/side table for everything 

sim::Value
  
- opcodes can be flipped or rotated, so there are families and rings of
  successors to key through.  table of pred and succ?  triggered by
  R or shift-R (or Q/E), or INCREMENT if we let trucks edit opcodes
  
- if sorting is going to be important, we are going to get mixed things
  - some materials will have quantities, purities etc.
  - where to store and how to deal with this "metadata"?
  - important if we want everything to break down without impossibility
  
- scattered access patterns ; can we do anything as a bulk join type thing?


sim::Entity

- Trucks have been thought about extensively


- It's attractive to have all entities interact strictly via the cell grid,
  but is it powerful enough?  Seems very awkward for some structures
  - For example, a stack.  Put onto input cell.  Stack takes it, puts on
    output cell, and internalizes the prior contents of output.  Take from
    output cell.  Stack notices it is empty, takes from internal storage and
    puts something on output.  This is weird double-handling where stuff gets
    moved unncessarily.
- Alternatives:
  - Can push into or pull from entities in adjacent cells?
    - "Sideloading"?
      - snap on processing chains
    - Does this become a Sokoban-style thing?
    - Note that Sokoban chains are prohbited by dogma (non-local, stiff)
  - Can load down onto waiting (or passing) truck 
  
  


- Static producers are simple
  - Oil well, artesian well, shaft mine, air separation plant, desalination plant
  - deplete a patch or a global thing

- Processing facilities
  - Wait on input cells
  - Take inputs
  - When all inputs present, wait for process to complete
  - Wait for clear output cell
  - Internal buffers may decouple these processes somewhat
  
- Can we make them plug-and-play; e.g., 
  - one output is heat, needs to be connected to cooling tower, or engine, or
    radiator, or injected into ground
  - output is CO2, place chimney there, then scrubber, then injector
  - Chloralkali plant produces H2, burn it off; then cryo-plant

- Area facilities
  - surface mines
    - bagger 288!
    - combine harvester-style surface miner
  - logging and agriculture
  - salt evaporation
  - Really hard to see how to square these with grid cells
  - Do trucks drive up to the side, snap on, and then go slow as they are
    filled?  How do trucks find them?  Do they just plough a row back and forth?  Good half-measure
    Do they write their own instructions for truck pickup?  Too smart.  Do they
    dump at the end of their row for aggregation by a column thing (like a belt)
  
- Buffers
  - One in cell, one out cell?
    - FIFO?  Stacks are more useful but also a bit ambiguous in what goes
      where
    - Notably, you can make a fifo from two stacks but only by blocking them
      to transfer-reverse all of the input when the output is empty.
  - Storage tanks, silos, hoppers all easy
  - Yards and piles take varying area
  
- Bulk material conveyors
  - Slow teleport
  - Straight-line only
  - No compacting
  - Again, layers (stilts or underground) with gentle slopes
- Smart floors?
  - Microscale, more like factorio, they slide stuff around
  
- How do we handle gas liquid heat and electricity flow?
  - Option 1, just make them global resource pools
  - Option 2, make them local resource pools by proximity to radiators or explicit connections
    - These connections probably require underground / aboveground layers or they will block roads  
  - Option 3, make them special roads where the resources move slowly and truck-like
  - Option 4, pre-emptively ruled out by dogma, ODEs


Text assets

- Description panes for opcodes tc.
- Story treatment, storyboard


Graphics assets

- Reference scrapbook

- Decide on if we commit to a signed distance field for
  - fonts
  - symbols
  - hard surface alpha
  - Note that signed distance field alpha can be combined with regular rgb
    images, might actually be awesome (no lumpy diagonals); a good fit for
    hard-edged things that need to be z-buffered
  
- Quality symbols
  - Define in SVG or similar?

- Placeholder 3D, beginning with just cubes


Graphics engine

- Instanced non-shadow-casting light sources (headlights etc)
- Decals
  - Full material and normal replacement, as in tire tracks
    - After GBuffer but before lighting
    - Reads Z, writes everything else
  - "Augmented Reality" that is more like an overlay projected onto (and
    obstructed by) geometry
    - After GBuffer, after other decals, lighting agnostic (cuts albedo)
    - Reads Z only, writes light-buffer

- Nice placeholder cubes

- Generate missing textures from test pattern + plus name rendered on  

- Simple shaped smoke  
  - Decals draw from sun perspective in shadow stage onto an irradiance map
    - Gated by z but does not write to it
  - Use to illuminate solid surfaces in other stage

- Smoke/dust/fog extinction and emission
  - "Easy", just sort draw order

- Hard shadows cast onto smoke:
  Each pixel=ray has to sample the shadow map several times, in a way that
  resists artifacts
  - If sampling already, volumetric?
  
- Smoke self-shadowing... hard!  Analytic expression for a 3 gaussian of density?
  - In particular, self-shadowing needs us to sample across 2D - depth into
    the smoke, and along the ray from those ray depths to the sun
  
- Efficient mesh instanced object, the 3d analog of Atlas.  Contains e.g. wheel,
  not a whole model, perhaps.  One draw call, many objects.
  
- Vs, bone rigged models.  Per vertex index of transform matrices.   



## Serialization

- Lessons from Serde: 
  - hint what we expect for non-self-describing formats
  - handle poor matches to it
  - avoid N^2 by passing through vocabulary types like int, string, array
- Streaming
  - For example, strings may be huge binary blobs or nested formats; we can't
    pass a string once
- interface should be something like
   - array begins, strong begins with bytes, string ends, 
     string begin with bytes, string continues with bytes, string ends with bytes, array ends
- these are interacting produce-consumer state machines that can be chained up
    to parse files to memory to json to string to base64
- coroutines would be nice but typed multiple entry points may be too valuable
    how do we pass type information around without exploding interfaces too
    much?



Bite-sized
- uniforms seem to be out of scope of meshes after all
  - can we set them up once and for all


Computer science
- How do we receive OS events and hand off a consistent view to the render
  thread?  Make our own message queue?  Same problem with model thread.
- Do we even need the DisplayLink timer or can we just block the render thread
  on the drawable?  Does this increase latency too much?
  

System
- Police decoupling of platform-specific features
- Contraverse, leverage platform for ModelIO etc.?

Renderer
- Bloom shape?  JJ Abrams long / CCD ?  (bloom itself is being deleted, 2026-11-09)
- Object-mesh pipeline for generating debug normals
- Object-mesh pipeline for generating big cell grids just from 2d arrays of central texCoords
- Instanced small lights
- Cloud/smoke and partial-shadow-map
- Parallax mapping
- Multisampling
- Iridescence (ClearCoat is done; see done.md)
  - Specular colors vs Metalicity?
- Understand what tiles can do
- Bones vs instances; smooth blending requires bones, but we're not doing much 
  nonrigid materials

Mesh
- serialize/deserialize
- texture format conversion / combination
- uv unwrapping
- normal map / height map / geometry to and from
- nonconvex faces from edge soup
- (vector) ambient occlusion baking?
- higher structures than triangles?  parametric, implicit surfaces, simple
  solids
- parallax surrogates?
- CSG with polygon/rays etc
