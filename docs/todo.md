# Todo

## Things to build
- Terrain on-demand when a machine nears the edge.  Generation allowed to be expensive, so what we should probably do is deterministically require it to be added at some well-defined point, but also eagerly compute a boundary of terrain in the background so we usually don't have to stall the simulation.  Chunked at some convenient size (16x16?).
  - DONE 2026-11-08: 16x16 chunks, a pure function of the new World::_seed (save v8; 0 = hand-made world, never generates).  Each step makes the missing chunks of the 3x3 block around every cell proposed for occupation (World::_advance), so >= 16 cells of terrain always lie ahead of a mover; the berth search makes chunks as it reads.  Synchronous: ~0.3 ms/chunk in Debug+ASan (terrain_chunk_cost), so the background frontier precompute is NOT built -- revisit when the generator gets expensive.  OPEN: the minimap only covers its fixed 256x256 window.
-  Coroutines that wait on Metal to finish something.  Via MTLSharedEvent?  Cancellation?
  - DONE 2026-11-08: `co_await shared_event_wait(event, value)` (render/metal_event.{hpp,mm}; a Future, so also a sender, and with_deadline / race compose): completes once signaledValue >= value (GPU encodeSignalEvent or CPU setSignaledValue), continues on the pool, never on Metal's thread; via notifyListener:atValue:block: on one serial-queue MTLSharedEventListener (polling would need a poller; the reactor has no knote for an event).  Cancellation is destruction on the requester's thread; who resumes is settled by the block resetting the stop_callback BEFORE its claim, so a callback that ran is visible and one that did not never will (protocol in the .mm).  Tests: CPU/GPU signal, a GPU readback, deadline, stop before park, stop while parked, 200-iteration fire/stop race.  No use site adopted: screenshot and the offscreen frame wait are polled / blocking on the main thread, not coroutines.  OPEN: (1) Metal cannot withdraw a notification and, measured, does not release a pending one when the event is released, so a stopped wait on a never-reached value leaks a ~100 B state (and maybe the event) -- owners abandoning such an event should signal UINT64_MAX first; (2) a block can wait in ~stop_callback while a requester destroys a coroutine tower, delaying the serial queue's other blocks; (3) whether a FAILED command buffer still signals its events is unverified (a hang if not; with_deadline is the guard); (4) a command-buffer-completion awaitable (addCompletedHandler, before commit) if a caller wants status
- Async resource loading; do we just load everything in parallel or is there a benefit to serializing parts (like disk access).  Cache?
- High quality mip-map generation on GPU via separable Gaussian
- SVG loading for OTF and standalone files.  Only partial support needed.  Should produce geometry, not a rasterization, and use the same on-CPU and on-GPU rasterization code as the font engine.
- An extra clear-coat term in the render: a smooth specular reflection, but mostly transmits to lower layer.
- Define UI in points not pixels, consider the device's magnification factor, and provide a user scaling factor.  We are a desktop game not a TV game, so we don't scale to the drawable size, and we do support weird aspect ratios and being in a window
- DONE 2026-11-08: the decal shader applies emissive (light attachment toward WryDecalInstance.emissive by coverage), occlusion (material.r *= 1 - occlusion * coverage) and embossed normals (coverage as a height field, four central-difference taps at the mip of emboss_radius, tilting the stored normal in the frame's tangent basis); zero fields are pixel-identical to before; numbers are raised kNumberEmboss = 0.012 world units over a 3-texel bevel (WryWorldScene.mm emit_number).  Still no normal-map image per decal
-  Use decals to draw general strings, not just decimal number.  Needs more characters in the same texture, that's fine.
- Decouple the glyphs from the tile SDF.  Draw them all as decals (or, to optimize the common case, combine two textures in the raymarcher)
- Generalize the 2D interpolated + 1D analytic SDF to support full analytic.  Superellipsoids?
- Data driven: try to move properties out of the executable and into data files as much as possible.  e.g. prices, opcode \_displayed\_ names, help text etc.
- Context help for item under cursor: hover or side panel like Factorio
- Command help: show some key suggestions, e.g. depending on symmetry of what is held, show [R], [SHIFT]+[R] or [H],[L]
  - DONE 2026-11-08: a hint line centred over the palette while holding (WorldState::hand_hint): only the reorientations that change the held opcode, by its symmetry class (direction-like: rotate both ways + the one mirror that moves it; axis-like: rotate; handedness-like: one "mirror"), with the live keymap's combos, then [click] place and [Esc] drop.  Test world_state_hand_hint
- Left-click on a TRUCK to have the camera track it, and to explode the stack to make it more viewable
  - DONE 2026-11-08 (camera half): left click with an empty hand on a truck issues `follow` (also a console/wryctl command, `follow Coordinate(x,y)`); the camera centres each frame on where the renderer drew it (or its tile, when it was off screen); panning, a click off any truck, or the machine vanishing stops it.  OPEN: "explode the stack" needs a design
- DONE 2026-11-08: Compute the font drop-shadow in a shader.  Superseded: the baked shadow is gone and the outline below is computed in quadFragmentShader
- Load/save game etc. as Commands  -> DONE 2026-11-08 (the session commands line below)
- Commutative operations on World values.  Operation is applied when its transaction commits, concurrently, in any order.  Could be an atomic add on the value in the new World, for example.  Money is an example.  Attempt a widthdrawal if enough money \_now\_; but if enough people do it on the same turn, goes negative.  Don't try to prevent that, just penalize it - apply overdraft or interest every "month" based on lowest balance.  Fancier implementation for busy variables is to apply the deltas to thread locals and merge at the end of step.
- Help text, display text, etc. needs a to have meta that allows the easy inclusion (and rendering) of glyphs and keycaps and stuff.
- If chemical formulas become an important part of the way we present stuff, we need subscript support

## Things to change
- Raise the TRUCK's lowest surface so it hovers and a tile just slides under it
  - DONE 2026-11-08: the token and its stack ride token_lift = tile slab thickness + 0.015 above the board (WryWorldScene.mm)
- Investigate radiance cascade as a GTAO alternative.  Can we ditch the shadow map and directional light entirely?
  - INVESTIGATED 2026-11-08 -> docs/radiance_cascades.md
- Tune the lighting.  Environment map is currently a daytime desert; try indoors with a window.
- Bloom is calculated but not used.  Decide.
  - 2026-11-08: no longer computed unless shown: `toggle-bloom` (off by default) runs the blur and presents light + blur/8.  As built it blurs the HUD too (it runs after the composite-and-GUI pass) and, with no HDR highlights in the scene, reads as a uniform haze (assets/bloom_off_vs_on.png).  DECIDE: delete, or move before the GUI with a threshold
- Make the palette a GUI widget
- Make the console a GUI widget
- Replace Futura Medium Condensed with something we clearly have rights to.   Noto Condensed?
- Replace the current font weight with something heavier
- DONE 2026-11-08: The pixel-scale font shadow is not enough to guarantee clarity.  We need a heavy outline for free text, and no shadow at all.  Glyphs are plain padded coverage; free text (log, console, money, dialog titles) sets WryQuadFlagOutlined and the shader dilates the coverage (max over a radius-2.25 texel disc) into a 90% black outline; text on buttons and cells is drawn plain

## Bugs
- GTAO scale is specified in pixels?  (Why does it even have a scale, should be scale-free exponentially spaced samples?)
  - ANALYSED 2026-11-08: not pixels.  `ssao gtao RADIUS` takes board texels (128 per world unit) and stores world units (_ssao_radius); the kernel projects it per pixel by depth (ssao_radius_pixels), clamped to 2..256 px, and fades occluders over the last quarter of the world radius.  So it is a world-space falloff, which GTAO needs only to bound the horizon search; a scale-free variant (exponentially spaced taps out to the screen edge through the depth pyramid, no falloff) is a design choice, not a bug fix -- DECIDE

## To be decided
- Power, water, heat want to be partial differential equations.  Not allowed--simply too expensive to compute.  But even a simple global scalar might be heavily contested.  If we think about combining trees, maybe what we can do is have discrete lumps move around.  Building has a buffer of electrictity; occasionally requests top-up electricity from its local substation; likewise the substation holds a buffer it requests from superstation etc., from a power station.  No node has too many attachments, contention is low, and blackouts are local but contagious.
- How to display matter.  Just the name, an abstract shape, a molecule, a "mineral sample", a bucket o' stuff?  (Factorio is inconsistent here)
- How to distinguish ghost Terms via some simple visual effect
- How to display temporary cosmetics to cover up server round-trip.  Possibly, a renderer-scoped table of temporary modifiers to be applied while drawing the world.
- How to handle pop-in pop-out of Terms as they are mined, shipped, picked up etc.

## Other
- 
## Won't do
Things that have been decided against
- Non-shadow-casting fairy light shader for running lights, headlights, etc.
- Fog, dust, extinction
- Cloud, smoke shadows projected by the directional light
- Tilt-shift

## 

-
List of one-line reminders of things to think and do
-  - LANDED 2026-11-05: console line editing, in two landings (render/docs/line_editing.md is the write-up: model, keys, every corner case and the choice made).  gui::LineEditor (render/gui_line_editor.{hpp,cpp}): text + cursor + anchor; motion and erasure by character / word / line on the macOS and readline keys; Shift-selection, Cmd-A; kill and yank; undo / redo (typing a word at a time); copy / cut / paste through gui::Clipboard (gui_clipboard.hpp: plain text, the system pasteboard in the windowed host only, private to the process otherwise).  gui::LineHistory (gui_line_history.{hpp,cpp}): Up / Down keep the draft and search by it as a prefix; best-effort file config/console_history.txt (append per entry, no sync, pruned to the newest 1000 once past 2000 lines; windowed host only).  command::complete (core/command.{hpp,cpp}, commands.md section 5): Tab completes command names, `name=` and argument words (Type<T>::words() for Bool / OPCODE / MATTER / Term, Param::words for mode-like Strings: ssao, quit, key, click; help takes command names), listing the candidates in the output when ambiguous; the grey suggestion after the line (history first, else the one completion) is taken with Right / End.  The console also pastes multi-line text as a script (each line break submits), places the cursor by click, selects by drag / double / triple click, and scrolls a too-long line sideways.  NOT DRIVEN BY ME: real Cmd chords through -keyDown: and the NSPasteboard read / write (no way to inject NSEvents; unit tests and the offscreen runs use the in-process clipboard) -- try Cmd-C / Cmd-V / Cmd-Z in the window once.  NOT DONE (line_editing.md section 4): dead keys / input methods and grapheme clusters (pointless while the atlas font is ASCII only), Ctrl-R, a candidate popup, a kill ring, scrolling the output, Windows / Linux spellings of copy / paste / undo.  More parameters could declare words (file names for `replay`, the atlas names)
- DONE 2026-11-06 (was BUG 2026-11-05): test host_tcp_join (game/host.cpp ~632) holds `World* loaded = load_world_from_bytes(...)` unrooted across `co_await eventually(...)` and dereferences it afterwards (`loaded->content_hash()` ~651); the collector freed it in 1 of 4 full Debug suite runs today (ASan heap-use-after-free in HeapTerm::content_hash, freed by collector_sweep_walk 137 passes earlier).  A test bug, not a collector bug: wrap it in a Root<World const*> as `snapshot` is a few lines above.  Rooted; the unrooted span became a suspension when the test's blocking waits became co_awaits (e63f6d7)
- DONE 2026-11-06 (was BUG 2026-11-06): test headless_host issued `pause`, `write`, `step 2` back to back and the `step` command read the relay's clock().paused before the relay had seen the pause, so it either stepped without the write or never stepped. Relay::step_if_paused decides in inbox order (test relay_step_if_paused); the test steps until the write reads back. Left as is: `step n` replies after the next n steps to arrive, which just after a `pause` can be ones already in flight (commands.md section 8)
- LANDED 2026-11-05: zoom (world_state.hpp _zoom / set_zoom / show_map): one scalar, default 1, applied to the camera in _regenerate_uniforms every time, never accumulated.  World view (0.5 .. 2) dollies the camera (offset and clip planes / zoom, field of view fixed); below 0.5 it is map view, the map quad scaled by the zoom about the focus under the default camera, which meets the world view exactly at 0.5.  Pinch (WryEventKindMagnify) multiplies it, anchored at the ground point under the cursor, in either view and across the boundary; scroll-pan traverses 1 / zoom; TAB (toggle-map) classifies the zoom and jumps to the other view's default (1 or 1/64).  Console: `zoom [percent]` (0 prints), `magnify PERCENT` injects a pinch at the injected pointer.  Same day: the map quad magnifies nearest (deferred::map_fragment_function, _mapGBufferRenderPipelineState) and terrain.hpp TERRAIN_COLOR_SRGB is now the board's pastels, which the board tints also read, so the map is the board seen from afar (the cut at 0.5 is abrupt by choice).  Follow-ups: map view near 0.5 should probably still show the minimap and palette; while paused, several pinch events in one frame coalesce and are then clamped to a halving / doubling.
- LANDED 2026-11-04: image decals (ShaderTypes.h WryDecalInstance, Shaders.metal deferred::decal_*, WryWorldScene.mm prints / emit_number): a box drawn after the frame's geometry with no depth test, each fragment finding the surface from the G-buffer's depth and blending the stored albedo toward tint * image by the image's coverage, and roughness / metallic toward the instance's by weighted coverage; surfaces facing away from the decal's +z past a cutoff are skipped.  Hero case: numbers are the `blank` tile (assets.json row 11 col 4, tiled by name) with their decimal string printed from the cutout atlas's digit glyphs (_glyphs, assets.png loaded straight alpha), shrunk to fit the face; a minus is the dash glyph's middle half.  Follow-ups: normals are not projected (the instance's frame would carry a normal map); very large integers just shrink (DONE 2026-11-08: number_caption, below); the cursor's held-value image still cuts a single hex digit from the atlas (DONE 2026-11-08: it composites the same caption); the hex digit row 10 is only read by the decals now
-
- FEEDBACK (2026-11-01): U-turn is prominent and unhelpful.  Tile captions too small (zoom?).  Hexadecimal is very confusing, modulus numbers are confusing (multi-digit with decals in Factorio 4-character style "2.1M"?).
  - DONE 2026-11-08 (numbers half): number tiles and the held-value cursor print render/number_caption.hpp's decimal, at most 4 characters plus a minus ("9999", "12k", "1.2M", "9.9M" for 9999999, truncated, SI k..E); the '.' and k M G T P glyphs are assets.png row 15 cols 1-6 (make_assets.py; 'E' is the hex row's).  OPEN: U-turn prominence, caption size
- - DONE 2026-11-08: decal shader with normal perturbation for embossing (see the decal entry at the top).  TODO: point light shader.
- TODO: Draw tiles in one pass and symbols as decals, because: we need combinatorial numbers, strings.
- TODO (2026-11-01): build the symbol field cache during asset loading, under the splash and main menu, not at New Game.  Today a cache miss (any atlas change; ~49 s in Debug, single-threaded) happens when WryWorldScene loads the atlas, which stalls the new game's self-host handshake, the client is kicked and the world has no machines (frames identical before and after `resume`).  The atlas name is known at startup (WorldState::_symbol_atlas), so build or validate the cache there, and rebuild on a worker when `atlas NAME` switches at runtime
- TODO (2026-11-01): the look-iteration console commands are debugging aids to retire once the look is decided: `board`, `exposure`, `atlas`, `token`, `toggle-decals` (world_state.cpp) and their WorldState fields (_board_*, _exposure, _symbol_atlas, _token_*, _symbol_decals); fold the chosen values into constants.  The runtime opcode-label stamping in WryWorldScene -loadSymbolAtlasNamed: writes into the cell margin the slabs never show and is superseded by the captions tools/make_tile_atlas.py prints on the tiles; remove it with them
- LANDED 2026-10-29: symbols are filleted slabs ray-marched from a 2D SDF (render/symbol_sdf.{hpp,cpp}, deferred::symbol_* in Shaders.metal, WrySymbolInstance); field cached as assets/assets_field_v2.png (generated, never hand-edit; rebuilt when assets.png is newer; v2 = alpha is a coarse +-128 texel distance, stroke widths derived from the field).  Follow-ups: slab edges are aliased (one march per pixel, no coverage); thickness = stroke width capped at 20 texels (solid tiles; 32 put their undersides below the ground) -- revisit the cap and whether fillet radii should be global rather than per symbol; the field build is ~45 s in Debug since the 256 px cells (single thread, ASan) -- parallelize per cell or move to a worker if the cache ever has to miss at startup; symbols sample the atlas at the march hit, so the stamped opcode labels (outside the silhouette) never show on slabs
- LANDED 2026-10-29: symbol ambient-occlusion decals (deferred::symbol_decal_* in Shaders.metal): flat slabs near the ground multiply the G-buffer's occlusion channel by the exact 2D stroke-section occlusion (three corner-disc tangents, algebraic), reach from the (h + t/2) t / (2 d^2) tail at 2%, last stroke width faded; ambient only (the IBL pass is the one reader of material.r).  Follow-ups: stack glyphs get none (the infinite-stroke model overstates a glyph's far field; a per-glyph soft disc would do); ends, corners and junctions are off by the far-edge term; the stroke width in the far-edge term is the cell's widest, so serifs over-occlude; measure the decal fill cost once there are many ground symbols on screen
- DONE 2026-10-30: symbol field margins.  The atlas is now 16 x 16 cells of 256 px with each glyph's 24-unit frame centred in 64 px of empty margin (tools/make_assets.py PITCH/MARGIN, assets.json re-laid out, backup in assets/backup-2026-10-30); the renderer's grid size lives only in SymbolSDF::kCells, the frame is the world unit (texels_per_unit = texels_per_cell / 2), the slab cuboid covers the frame, field and decal reach the cell edge, palette and cursor cut the central half; the decal halos are no longer cut square.  Cost: a field cache miss is ~45 s in Debug now (4x the pixels per cell) -- parallelize signed_distance per cell when it matters; the sketch blob and the small-digit row are no longer carried / still unnamed
- EXPERIMENT 2026-10-30: screen-space ambient occlusion behind `ssao off|gtao|luft[-show] [radius texels] [strength %]` (GTAO on by default since the same day; Luft's halos read as a dark cloud floating with the foreground object when animating).  The image-based light now accumulates in its own attachment (AAPLColorIndexAmbient); depth and normal G-buffers are stored; two compute kernels (deferred::ssao_gtao_kernel, a 4-slice 8-step cosine-weighted horizon integral in render space, and ssao_luft_kernel, Luft's depth unsharp mask) plus a depth-aware 5x5 blur run between the passes, and a second render pass composites direct + ao * ambient before the GUI.  GTAO at radius 64 texels reproduces the stroke decals' halos on the ground (profile across a 7: 0.365 vs 0.354 removed at the wall, same tails) and also handles the meshes; decals and GTAO double-count when both are on.  Costs: storing normal+depth, 2 kernels + blur at full res, the extra pass (the overlay now draws there)
- DONE 2026-10-30: the Depth G-buffer holds linear view depth (clip w; 0 = sky) and every reader recovers the surface as camera + ray * depth from the MeshUniforms ray basis (camera_ray_centre/dx/dy, affine in NDC, computed in _regenerate_uniforms, test world_state_camera_rays); no shader multiplies by a 4x4 any more, the generic decal_fragment_function is gone.  Pixel-identical output.  It bought NO time: GTAO at 1920x1080 on the M1 Max is 2.6 ms (off) -> 6.6 ms, Luft 6.3, and the GTAO cost is flat from radius 4 to 256 texels, so neither arithmetic nor cache locality; halving the steps (64 -> 32 reads per pixel of 116 incl. the blur's 50) took the delta from 4.0 to 3.3 ms.  Model: ~0.02 ms per texture read per pixel at 1080p (~100 G reads/s) plus ~1.5 ms for the blur and the two dispatches.  Levers, in order: half resolution with a bilateral upsample (/4 on everything), separable or threadgroup-tiled blur (50 -> ~20 reads), 4 steps per side, then a depth mip so fewer steps cover the radius
- DONE 2026-10-30: occlusion at half resolution.  The two estimators run one thread per 2 x 2 block (reading the block's top-left texel, taps still over the full-res depth) into an RG32Float (visibility, depth) texture, the blur works on those pairs (25 reads, no depth texture), and the composite upsamples bilaterally (4 blocks, bilinear x depth-similarity, plain bilinear where no block sits on the surface) reading the stored depth G-buffer as a texture.  For that the second pass now binds ONLY Color + Ambient + the depth buffer, and the composite / overlay / overlay-quad pipelines declare exactly those (WryRenderContext.mm).  Halos identical to full res; silhouettes clean.  Time: GTAO delta 4.0 -> 3.05 ms at 1080p (off 2.4, gtao 5.45, luft 5.88) for a quarter of the fetches; 8 -> 4 steps earlier gave 4.0 -> 3.3; an EMPTY compute encoder in the off path costs nothing (2.31) unless the driver elides it.  So ~2.5 ms is neither fetches, nor arithmetic, nor the pass break as far as these blind experiments can tell; the 1080p HUD numbers may also be confounded by GPU clock state at 30% load (Antony's 4K figure, 30 FPS, scales like per-pixel work).  Xcode GPU capture (Antony, 2026-10-31, half-res GTAO at 1080p): the Screen-space occlusion encoder is 49% of the frame's GPU time (GTAO kernel 43% = 1.27 ms under replay, blur 5%; the whole replayed frame was 2.9 ms at performance state MEDIUM, so the HUD's absolute ms at 1080p ARE clock-inflated and only the percentages carry).  Inside the kernel the 2 x 8-step inner loop is ~85%; the depth fetch alone (line 'float sd = depthTexture.read(sg).r') is 31% (16% memory sample, 6% waiting on memory), the viewport any()/break 10%, ssao_position per tap 8%, the t computation 8%; the per-tap arithmetic is about as much as the fetch.  Limiters: ALU (~100%) and Last Level Cache, texture-read cache limiter high: 64 taps per thread (4 slices x 2 sides x 8 steps) fanning out over an 8 MB R32Float depth at per-pixel-rotated directions miss the near caches.  Occupancy 41%.  Insight 'Screen-space occlusion (half resolution) stored and never used again' is the encoder-level false positive (the blur reads it in the SAME encoder, serial dispatch order); 'MTLComputeCommandEncoder 2: Unused Texture x 8, 20.69 MiB' is NOT ours (the frame path creates one compute encoder) - driver/HUD work, unidentified.  LEVERS FROM THE DATA, in order: (1) tap budget 4x8 -> 3x4 or 2x4 (XeGTAO 'high' is 3 slices x 3 steps = 18 taps; we take 64) - cuts fetch AND ALU together; (2) R16Float depth mip pyramid (XeGTAO's prefilter, ~5 levels, mip picked by tap distance) so far taps hit a texture that fits the near caches - attacks the LLC limiter; (3) loop ALU: tap position as P + t * (per-slice increment) instead of ssao_position per tap, clamp instead of any()/break, rsqrt(dot) instead of length + divide; (4) occupancy: check register count in the shader profiler
- DONE 2026-10-31: levers (1) and (2), measured each.  (1) the tap budget is runtime now, `ssao gtao [radius] [strength] [slices] [steps] [pyramid]`, default 3 x 4 (was 4 x 8 compiled in).  (2) a depth pyramid, deferred::ssao_depth_pyramid_kernel (XeGTAO's prefilter: one 16 x 16 threadgroup per 32 x 32 tile, 5 levels in one dispatch via threadgroup memory, each coarser texel the 2 x 2 below it weighted toward its FAR side with sky dropped), R32Float full-res + mips, built at the head of the occlusion encoder; the taps sample(nearest, clamp_to_zero) the level log2(distance_px) - 3.3, which also retires the viewport break.  NOT R16Float: the ground plane quantizes into terraces whose steps the taps read as horizons (horizontal banding at native res).  MEASURING: windowed + display-synced command-buffer spans are useless (queued buffers overlap: 150 ms "frames" at 120 fps; and a lightly loaded GPU clocks down, so one pass reads 2.8 ms at 1080p windowed vs 1.2 offscreen vs 0.5 when 16 passes saturate the GPU).  So: `--offscreen` (one buffer in flight, no sync), `window-size w h` (now resizes the offscreen drawable too), `gpu-time` (mean GPU span since last asked), `ssao-repeat n` (for saturation checks; repeats are cache-warm, so n = 1 is the honest number).  RESULTS, offscreen, pass cost = frame - off, M1 Max: 1080p (off 2.1): 4x8 flat 1.76 | 3x4 flat 1.15 | 3x4 + pyramid 1.23 | 2x4 + pyramid 0.99.  Native 3456 x 2080 (off 3.8): 4x8 flat 7.2 | 3x4 flat 4.4 | 3x4 + pyramid 3.3 | 2x4 + pyramid 2.7 | 4x8 + pyramid 6.2.  So at native res the two together take the pass from 7.2 to 3.3 ms (-54%); at 1080p the pyramid is a wash (the depth fits the caches; the prefilter costs ~0.1).  Quality: 3x4 + pyramid vs 4x8 flat differs only at the glyph halos (mean 0.7 grey levels at native res), the blur hides the sparser taps.  NEXT LEVER: the floor.  Extrapolating taps to zero leaves ~1.5 ms at native res = the blur (25 reads per half-res pixel) + prefilter + encoder; a separable 5 + 5 blur is the obvious cut.  Also: `pyramid 0` and `ssao-repeat` are measuring aids to delete when the AO mechanism is decided (DECIDE line above).  Seen once in ten launches today: the background reactor self-test assertion (kqueue_reactor.cpp:841, g_cancelable_kevent_frame_count == 0) when an offscreen launch followed a `quit fast` within a second; unrelated to this work
- DONE 2026-10-31: the blur, three kernels of the same depth-aware window behind `ssao ... [blur] [blur-radius]` (0 direct, 1 separable = DEFAULT, 2 over a threadgroup-memory tile; 1..4 taps per side, default 2 = 5 x 5): deferred::ssao_blur_kernel / ssao_blur_axis_kernel (across into the blurred texture, down back into the occlusion texture, which the composite then reads) / ssao_blur_tiled_kernel.  MEASURED offscreen at native res (pass cost, GTAO 3x4 + pyramid; off 3.9): window 3x3 direct 3.21 | separable 3.05 | tiled 3.10; 5x5 direct 3.47 | sep 3.30 | tiled 3.35; 7x7 direct 4.12 | sep 3.26 | tiled 3.92; 9x9 direct 4.53 | sep 3.33 | tiled 4.13.  So the direct window costs ~0.018 ms per read per half-res block at native res (0.46 ms for 5 x 5), the separable blur is flat in the radius (~0.2 ms) and wins already at 3 x 3 (no crossover: the second dispatch's write + read of a 2-channel half-res texture is cheaper than three extra reads per block); the threadgroup tile only helps from 7 x 7 and never beats separable, because its cost is the (2r+1)^2 threadgroup-memory loop, not the fetches.  At 1080p everything is within the noise (0.7-1.2 ms pass, run-to-run scatter 0.3).  Quality: separable vs direct differs by at most 9 grey levels on 0.2% of ground pixels; tiled is bit-exact.  FLOOR (separate run, native, noisier: off 4.6): 3x4 taps 2.48 | 1x1 taps 1.15 | 1x1 flat depth 1.14 | 1x1 + 3x3 blur 1.14.  So ~0.06 ms per tap and ~1.1 ms that is not taps, not the prefilter (~0.03) and not the blur radius: the GTAO kernel's per-block setup, the composite's 5-read bilateral upsample at FULL res (~0.3 by the read model), the blur's ~0.2 and three dispatches.  Next lever if the floor matters: fold the upsample's depth test into fewer reads, or run the composite's AO fetch at half res via a sampler; not pursued.  Run-to-run drift between offscreen runs is ~25% on the pass (the same config read 3.30 and 2.48 in two runs an hour apart); only compare within one interleaved run.  Reactor self-test assertion (kqueue_reactor.cpp:841) seen a SECOND time today, again on an offscreen launch 3 s after a `quit fast` of the previous instance: that run's setup commands all replied (error) from the start, so the two may be one failure (the previous instance's socket still bound?) - 2 of ~17 launches
- DONE 2026-10-31: the reactor self-test assertion (kqueue_reactor.cpp, g_cancelable_kevent_frame_count == 0) in driven apps.  Cause: the count is process-wide and the kqueue tests assumed the reactor was theirs alone; with --control-socket the listener's accept frame (re-armed per connection), each connection's recv frame and the idle host's with_deadline timer keep it nonzero, so the tests' unbounded drain loops spun a pool worker for the app's whole life (the suite NEVER finished in any driven run, and the hot spin starved startup: that is why one run's setup commands all replied (error)), and the one capped wait asserted whenever a connection was open as it ran.  Fix: FrameCensus in the test section -- each count-reading test first waits (<= 100 ms) for the count to rest at zero and requires zero throughout 20 ms (one zero is not enough: the listener re-arms through a microsecond gap at each accept), else its count checks stand down with a printed note; every drain is bounded (1 s, then assert), so a regression fails instead of hanging.  Headless --test-only still runs every check (no socket).  Residual: a non-driven app whose user starts a save or a session within the suite's first seconds could trip a drained() assert; not seen.  Verified: 6 driven launches with commands arriving through the suite, 0 asserts; headless kqueue group 0 skips
- TEST ISOLATION (2026-11-01): the unit tests carry assumptions from when they ran on a thread of their own.  As concurrency and coroutines grew the tests leaked work onto the shared pool, and with networking they now share the live reactor (control socket listener + connections, the host's deadline timers) with the app they run inside, which is what the FrameCensus skip above papers over.  Future options: mock the reactor / pool for the tests that count frames or time waits (a test-owned reactor instance, or a per-task frame census propagated like the stop token through the Delegate chain in coroutine.hpp / nursery.hpp / execution.hpp, ~15 forwarding sites), or simply disable the environment-sensitive tests in headful mode and keep them for --test-only.  Not started
- DONE 2026-11-02 (the TEST ISOLATION line above): tests share the pool and reactor by contract now, io/test_support.hpp.  No test blocks a worker (the sleep/poll `eventually` helpers in control_socket, transport, relay, host, world_state, headless_host and gui_context are coroutine waits on the reactor), deadlines are 30 s hang detectors not performance claims, transport and control-socket semantics are tested over socket pairs (Transport::adopt, new ControlSocket::make/adopt) with one path-binding test, the seven kqueue frame-count tests are tagged test_exclusive and skipped in a live process, and `--no-tests` turns the background suite off (the smoke scripts pass it).  Primitive fixes found on the way: listen_unix flipped the process umask around bind (two concurrent calls could leave it 0177 for good; now bind-tmp, chmod, rename) and unlinked whatever was at the path (now a probe refuses a live listener, and close unlinks only our own inode).  Not done: a per-task frame census; the exclusive tag made it unnecessary
- DECIDE (2026-10-30): three ambient-occlusion mechanisms are live at once -- GTAO (default), Luft (toggle) and the SDF stroke decals (toggle-symbol-decals, on) -- and GTAO and the decals double-count on the ground.  Pick one and remove the others.  If the decals win, the ambient attachment, the stored normal/depth, the compute stage and the second pass can all go and the pipeline returns to its single-pass pre-2026-10-30 shape (overlay back in pass 1); if GTAO wins, symbol_decal_* and _show_symbol_decals go, and the decal maths stays only as the oracle for the kernel under a grey sky
- BACK POCKET (2026-10-30): directional occlusion in place of AO * IBL.  The ambient term is the hemisphere integral of V * L * f * cos; AO times an irradiance lookup factors it and is only exact for an untextured sky.  Cut the middleman: keep the GTAO slice march but read the sky per visible sector (visibility-bitmask slices, Therrien/Levesque/Gilet 2023, after HBIL and SSDO) at the mip the sector's solid angle implies (Colbert-Krivanek filtered importance sampling), one-bounce colour from the pixels the march hits, and for specular a GGX visible-normal direction marched in screen space (SSR on hit, prefiltered cube on miss).  Diffuse part can run at half res if it outputs irradiance and the composite multiplies by full-res albedo.  Steps in order: bent normal into the irradiance lookup (free from GTAO), bitmask sectors with sky lookups, VNDF march on the metals, then temporal reprojection (no motion vectors or history buffer today) once the noise demands it.  Worth it only if the final sky cube is strongly textured (the dark room with a big window) or low-roughness surfaces are common; the prize is dropping the directional light and shadow maps and having lighting just work for our simple scenes.  Not decided
- FIXED 2026-10-30: the image-based diffuse term carried a spurious 1/pi (the prefiltered map holds radiance averages, so Lambert is albedo * L, not albedo * L / pi): the sky lit surfaces 3.1x too dimly next to the sun and next to its own specular reflection in metals; also metals now get no sky diffuse (kD * (1 - metallic), as the sun pass already had).  Verified on white ground: shadowed = the map's level-4 average (0.178 vs 0.180 predicted), sunlit = that + albedo * NdotL / pi.  Same day: the diffuse term now reads a 32 px irradiance cube (CubeFilterIrradiance, cosine-weighted hemisphere mean per texel) instead of the prefiltered map's roughest level, which is the WHOLE-SPHERE mean of the map (0.18, 0.19, 0.24 for day.png vs the hemisphere's 0.10, 0.16, 0.30; the ground half greyed the sky light); verified sunlit white ground = 0.96 * E/pi + 0.255 to 0.002 per channel.  The specular prefilter weights by NdotL (Karis) since the same day
- BUG (2026-10-29): the reactor unit test at kqueue_reactor.cpp:664 asserts the GLOBAL g_cancelable_kevent_frame_count is 0, so the windowed app aborts if a game session (its own deadline frames) is live when the background suite reaches it; the test should count its own frames only, or the suite should not share the live reactor  -> FIXED 2026-10-31 (the DONE line above: FrameCensus stands the count checks down when the reactor is shared)
- HOLD (2026-10-29, Antony): the opcode palette is old, retrofitted as a widget; invisible to `widgets`/`press` (coordinates or `hold` reach it); make it a proper widget later
- DONE 2026-11-08 (was HOLD 2026-10-29): the session commands `new-game`, `save`, `load name`, `list-saves`, `main-menu` (game/world_state.cpp, commands.md section 7; tests world_state_session_commands, headless_host_main_menu).  Both menus' buttons issue them; at the main menu new-game / load build a WorldState and hand it to the host (GuiContext::on_world), in a game they replace the world in place as before.  A save's name is its file's (save_ID.wry): `save` takes no name and replies with the file once written (save_game's callback now reports the id).  The in-game menu has Quit to Main Menu (`main-menu`: GuiContext::on_main_menu, the host drops the world after the frame and its destructor ends the session; WryDelegate builds a fresh WryMainMenuScene, HeadlessHost shows its menu).  OPEN: user-named saves (`save name`), a `delete-save` command (the Load list's Delete calls delete_game directly), a real save picker for the main menu's Load Game (still the newest)
- IDEA (2026-10-29): screenshot could grow crop / per-widget / off-resolution options (today cropping is the consumer's job on the PNG it writes)
- DONE 2026-10-28 (915ef0e): mutation-in-assert hygiene, vacuous in Release (no crash, coverage silently lost), grep-clean now: gui_context.cpp pop_front/issue inside assert (lines 1298-1375), relay.cpp kick_reason x9, try_get out-params in player/host/save/persistent_map, persistent_map erase, command.cpp run("greet"); hoist as in 67904b6
- DONE 2026-10-28 (97457b6): the driven-mode fatal-signal handler took SIGSEGV/SIGBUS as well, only where their disposition is still the default (a sanitizer's handler is kept); a stack overflow still escapes it
- NUISANCE (2026-10-27): every headless_host test run leaves a session-*.wrylog in Desktop/assets/saves (ten from one afternoon of suite runs); point test sessions at a temp dir, or sweep them
  - DONE 2026-11-08: GuiContext::session_log_directory, empty as constructed (no log, so tests write nothing); configure_from_arguments sets it to saves/ for the real hosts; SessionLog with an empty directory writes nothing
- LATER (2026-10-11): _symbols atlas cells abut; box-filtered mips don't bleed (power-of-two cells), but a wider mip kernel or bilinear taps at deep levels would -- pad/gutter the atlas cells (tools/make_assets.py) before adopting a custom Gaussian mip kernel
- DONE 2026-11-08 (was CHECK 2026-10-10; fixed in f05ab1e "Buffer reuse", ordering argument now commented in WryBufferArena.mm): SpriteAtlas::commit copies each commit into a fresh WryBufferArena slice; chunks are stamped with the frame's MTLSharedEvent value, signalled once per frame command buffer (WryDelegate -render), reused only at stamp <= signaledValue, else the pool grows (test transient_arena).  Offscreen menu + world: one 4 MiB chunk total.  Same hazard, NOT fixed: WryWorldScene _mapTexture[2] double buffer -- replaceRegion into the back texture is safe only because map builds are >= 1 s apart, not by a GPU edge (stamp the texture with the arena's frame value when sampled and defer the upload until signaledValue reaches it).  Checked safe: SpriteAtlas::place writes only never-drawn atlas space (release is never called; if it ever is, the freed region needs the same stamp before reuse); symbol atlas / field reloads make new textures; WryWorldScene _buffers ring is dead code (commented out)
  - was: SpriteAtlas::commit rotates a ring of 4 shared MTLBuffers with no fence/semaphore, and the menu scene commits twice per frame; with up to 3 drawables in flight the CPU may overwrite a buffer the GPU is still reading.  Pre-existing, unobserved; fix with a frame-in-flight semaphore or a per-commit offset into a per-frame buffer
  - DESIGN (from the 2026-07-22 "Big world stress test" session, transcript 4d922ba7): prefer an MTLSharedEvent-stamped pool polled at acquire over addCompletedHandler -- stamp each buffer with the frame's event value, signal once per command buffer, reuse anything stamped <= signaledValue (wait or grow otherwise).  Maps 1:1 to Vulkan timeline semaphores / D3D12 fences; no Metal-owned callback thread.  Same arena serves metal4_migration.md stage 2 (set*Bytes replacement)
- Full implications of the next_ready GC bug; clean rederive from epoch formulation
- DONE 2026-09-25: Saving AMTs saves their in-memory structure, which is strange and brittle
  - Save as a flat key-value list.  AMT structure is unique at the moment but
    that's not a guarantee for all structures. 
    - LANDED 2026-09-25 (TERM_SAVE_VERSION 5): one flat code-ordered (code, value) record per trie, ki waitsets inline, loader rebuilds via SortedBuilder; save_format_world_roundtrip asserts loaded shape == original shape; old v4 files are hard-rejected
    - IDEA 2026-09-27 (not yet; Antony wants to understand the format better first): the inline waitset count is found by a second traversal of each tiny inner set; alternative is reserve a u32 in the open record body and patch it after the members (the body is an in-memory vector until the record closes, so the patch is an O(1) memcpy, same mechanism as the back-edge fixups).  Subtree-size-per-node was rejected for this: +8 bytes per node everywhere, only worth it if size() is wanted elsewhere
      - LANDED 2026-09-28: ArrayMappedTrie::_size (subtree entry count, +8 bytes per node) maintained at every construction site, checked against children by _assert_invariant_shallow, size() on PersistentMap/PersistentSet; the save's inline waitset count still uses the second traversal (not yet switched)
    - FIXED 2026-09-24: clone_and_erase_key used to keep an emptied leaf in place, so shape depended on erase history (structural save bytes and any structural hash inherited that).  Now an emptied leaf becomes nullptr and a one-child parent collapses; tests pin shape == sorted-insertion build (amt_erase_canonical + the rebuild differentials).  Old save files may still carry empty leaves
    - DECIDED 2026-09-24: serialize in code order (not domain-key order); a key-encoding change is a save-breaking version bump, accepted to keep sorting off the save and load paths
    - LANDED 2026-09-24 as ArrayMappedTrie::SortedBuilder + build_from_sorted (test amt_sorted_builder); the save-format swap to flat code-ordered records is the remaining step.  Design: bulk build from the code-ordered (code, value) list, left to right and bottom up on the divergence shift between consecutive codes (stack of open nodes; leaves are 32-aligned blocks; >= 2 children falls out; exact capacities from a lookahead count); never n x insert, which path-copies ~depth nodes per key.  Reusable as rebuild_serial's null-source case.  Test: same_shape vs the insertion build plus a content oracle
- DONE 2026-09-27 (ea586f2): name the EntityID set by role.  WaitSet (waitable_map.hpp) is really PersistentSet<EntityID>, and _located_for_coordinate stores occupants in it, which misled a review ("waiters are never removed individually" is true of the waiter role only).  Plan: EntityIDSet as the underlying alias, WaitSet kept as an alias for the ki waiter index, EntityIDSet for the located map and its transaction verbs; same type, so no format or behavior change
- DEFERRED 2026-09-27 (Antony): the Saver buffers each record body and then copies it into the in-memory stream: one extra copy of every byte, and transient memory equal to the largest open body (the biggest map record).  Write-then-patch lengths are incompatible with a compressed output stream.  Options when it matters: chunk big maps into bounded records (SortedBuilder streams across chunks), stream closed records through the compressor as they close (peak = one open body, not the whole stream), or compute lengths up front
- Hash of data structures for file self-validation and multiplayer desync detection
  - Hash, like save, must be content only, not relying on structure
  - LANDED 2026-10-20 (WIP commit 40b71eb + uncommitted refactor, review pending): the sum form below, live every step.  ArrayMappedTrie::_hash (+8 bytes per node, eager at every construction site, checked by _assert_invariant_shallow; hash() on PersistentMap/PersistentSet); entry_hash/code_hash/content_hash in core/hash.hpp (pinned constants, test amt_hash).  Record-like GC objects enumerate their fields ONCE in `_fields(V&)` with roles state/transient/derived (core/fields.hpp); the collector's scan, the debug print, the save body (SaveFieldVisitor, save_format.cpp) and the content hash (HashFieldVisitor) are visitors over it, the first three behind the `Fields<Derived, Base>` mixin (term.hpp).  HeapTerm::_content_hash is a plain field written once by seal(): values in their constructors, entities by the committer in World::step's entity-map action after the ticket write (NOT at proposal: Machine::notify keeps building the clone after handing it to the transaction), the World at the end of step() and hack_repair_invariant; Debug asserts on reading unsealed and on a changed re-seal.  PersistentStack cells hash at push; World's _ready is enumerated as transient and saved/hashed through the TimeWheelFold view.  `state-hash` command + Hash(0x...) literal; rebuild_serial's leaf/null cases are a two-pointer merge through SortedBuilder (no transient nodes), mods as std::span.  Tests: amt_hash, hash asserts in every save round trip (loaded == live == world_content_hash_oracle), world_step_cost (us/step and the combine cost), world_state_state_hash
    - OPEN: a tool reading clang's AST to check every GarbageCollected class's members against its _fields (deferred by Antony 2026-10-20); loaders are still hand-written field by field; the save header does not yet carry the World hash for the loader to verify after load [DONE 2026-11-08, save v9: header [world hash u64] after the record count; Loader::load_and_commission refuses a World that does not reproduce it; test save_format_header_hash]; unified_leaf still applies modifiers one at a time through insert (transient leaves, same fix as rebuild_serial) [DONE 2026-11-08: gathers the leaf range's kv and ki actions and applies each list through rebuild_serial (SortedBuilder merge); world hash unchanged; starting-world step 82-113 us vs 99-197 us before, under heavy machine load, so unmeasured really]; seal-at-commit hashes the entity on the leaf's thread with a cold cache (a hot-cache seal at the end of notify would need the interpreter to finish the clone before proposing it)
  - IDEA 2026-09-24 (not now): hash the LIVE World every step, not only the save; clients upload it per step as proof of sync and liveness
    - Augment AMT nodes with a hash field and let the rebuild maintain it: untouched subtrees keep theirs, each assembled node recombines its children's.  Cost O(nodes rebuilt per step), which the rebuild already touches.  Nodes are immutable once published, so compute at assembly (or lazily into a mutable cached field, cf. the ReadyValue late-write policy)
    - Definition (Antony): a combining tree in code order, WORD-ary to match the trie's fanout: leaf = combine of (code, value) hashes in index order, internal = combine of child hashes in index order.  Content-only ONLY BECAUSE shape is canonical (the erase fix above is load-bearing); tied to SYMBOL_WIDTH, which is fine for peers on one build
    - Alternative (Claude): commutative and associative per-entry combine (128-bit sum of mix(code, value)); node hash = sum of children.  Content-only by construction and structure-agnostic, and equal to the hash of the flat code-ordered save list, so the loader can verify a file by summing as it reads.  Collision resistance is adequate for a non-adversarial tripwire (a poisoned hash is a self-kick)
    - Value hashes must be content hashes, never pointers: Entity and HeapTerm get a virtual hash cached at freeze; Term OBJECT payloads hash by target content; PersistentStack cells cache a hash (O(1) per push); World = combine(map roots, time, entity id source, ready folded into the time wheel exactly as the save does) so live hash == save hash
- FIXED 2026-10-20 (uncommitted): the Release configuration did not build at HEAD -- command.hpp make_command's assert fold had no pack under NDEBUG (now `((void)p, assert(...))`), and kqueue_reactor.cpp's test-observability frame counter was declared only under !NDEBUG while three test drain loops use it unconditionally (now kept in every build: those loops are what hold a test open until its frame dies, and in Release the asserts around them compile out)
- FIXED 2026-10-20 (uncommitted), pre-existing, found by the World hash: World::step passed each rebuild its modifier as a `freeze(...)` TEMPORARY in the fork expression.  Nursery::fork runs the forked body on the current thread and schedules the caller's continuation on the pool, so step() continued on another thread, destroyed the temporary and reused its slot for the next freeze() while the rebuild's prologue (make_cursor) was still reading it.  Debug (-O0) kept each temporary in its own slot so the dead object was still intact and TSan saw no write; Release reused the slot: TSan-in-Release reported the write/read pair, the per-step hash diverged run to run, the starting world ended with 5 or 6 of its 7 entities, and the term map received an EntityID-shaped word as a Term (the hashing build then crashed dereferencing it as a HeapTerm).  Fix: the five frozen modifiers are locals of step()'s frame, alive until the join.  LESSON: never pass a temporary by reference into a forked coroutine; a Future's reference parameters must outlive the nursery join.  Worth a grep of every `fork(` for the same shape
- NOTE 2026-10-20: `xcodebuild -scheme client -configuration Release` builds WITH ThreadSanitizer and UBSan, because the scheme's Launch action has both enabled and xcodebuild honours that for any configuration (the TSan Release stepped the starting world at ~1.2 ms/step; the sanitizer-free one at ~20 us).  For profiling pass `ENABLE_THREAD_SANITIZER=NO ENABLE_ADDRESS_SANITIZER=NO` (UBSan's nullability check stays on from the project's CLANG_UNDEFINED_BEHAVIOR_SANITIZER_NULLABILITY) or use Xcode's Profile action
- FOLLOW-UP 2026-10-20: 12 more test asserts outside save_format.cpp wrap a side effect (`assert(x.try_get(...))`, try_pop_front, try_erase); NDEBUG drops them and the test then reads an unset out-param (UBSan reported one in the GUI's background Release test run).  Convert to CHECK(...) (ext/assert.hpp, always evaluates).  save_format.cpp and world_state_state_hash done
  - DONE 2026-11-08: 20 asserts wrapping try_get/pop_front/issue/try_lookup_cumulant in machine, terrain, world, world_state and gui_context tests are CHECK now (grep-clean outside save_format.cpp)
- Does AMT enforce no null values?
  - Do all desired value types have a null value?
  - Pros/cons?
- ThreadPublic should probably not be GC and be more like a Crossbeam list
- The `_ready` skiplist should probably have a per-object allocator
- Transactional non-exclusive commutative operations, like addition
- Strip the WRY_GC_DEBUG crash-trap tier once the UAF stays silent (condition in garbage_collected.cpp banner)
- Sweep the GUI with WRY-GC-UNPINNED watched, then promote the detector print to assert
  - DONE 2026-11-08: offscreen sweeps (new game, map, follow, writes, save, main menu, second game, resize; WRY_GC_QUARANTINE 0 and 1) and the suite printed nothing; _debug_note_unpinned now prints the site and asserts.  The CRASH-1 trap tier (the line above) is NOT stripped: it needs Antony's call on "silent long enough"
- thread_public.cpp comments still say "static Root" / "drop the root" over the raw-pointer code
  - DONE 2026-11-08: both comments now describe the raw pointer + one-time root-set entry (sentinel) and the ring's strong edge (deregister)
- Rewrite transaction.md's "Open design: deterministic EntityID assignment" (lazy-span/relative-refs scheme is obsolete) to the landed ready-set-rank + ticket design; also its "Waiting and scheduling" section still describes the old single-partition/copy-back flow
  - DONE 2026-11-08: "Deterministic EntityID assignment" now describes request (notify's return) / EntityID-ordered ready-set rank (rank_frame, try_lookup_cumulant) / ticket (_free_entity_id); "Waiting and scheduling" describes WAIT_* conditions, ki wake/park, the time wheel and the parallel next_ready build; stale barrier, rebuild-serial and write_entity_id_for_coordinate notes fixed
- DECIDED 2026-08-16: EntityIDs are permanent, never compacted at load (remap changes hash priorities => conflict resolution => forks the sim from its own save; also breaks peer replay and Term-embedded ids); ceiling is a design-around, not a per-session budget.  Ticket hoarding equilibrium (O(spawn-capable entities) reserved-unspent ids, burned at entity death) is therefore just accepted burn
- Worldgen pre-loads Spawner tickets: confirm every pre-loaded _free_entity_id is minted from the World cursor (generate_entity_id), never a literal, or it collides with the first dealt block
  - DONE 2026-11-08 (checked): the one worldgen Spawner (make_starting_world) mints both ids from generate_entity_id; make_starting_world_big has none; no literal ids outside test fixtures
- ReadyKey requested/n and Entity _free_entity_id: the "late-written mutable field on a published-immutable object" contract lives in comments only; consider a doc paragraph naming both instances
  - DONE 2026-11-08: transaction.md "Late-written fields on published objects" names both, with the single-writer + join argument and the unenforced one-_advance-per-_ready contract
- Coroutine / reactor / sender sessions (2026-08-29..09-11), still open, rough priority order:
- with_deadline is the last delegate-based adaptor, and it is race-shaped: a two-entrant race (inner, timer) under stopped_as_optional, except that it propagates the outer's own request instead of absorbing it -- rebuild it on RaceOperationState and give WithDeadlineSender real SenderTraits/connect (today it is a sender in name only).  2026-09-12: stopped_as_optional/shield are receiver adaptors, race is a sender algorithm over any senders, sync_wait takes senders (and lifts awaitables) and returns the Outcome, core/execution.hpp is gone
- Unwritten for want of a use: shield with a budget (shield(f, s.get_token()) + cancelable_after(s.get_token(), d, [s]{ s.request_stop(); })); a runtime-N race (the entrant pack is compile-time; a nursery covers runtime N)
- SenderAwaitable::await_suspend should return a handle: a receiver completed inside start records the outcome without resuming, and await_suspend transfers back to the outer symmetrically, so a loop over inline senders (Just, a race of Justs) does not grow the native stack; async completions nest one level, which unwinds at the next suspension.  Since the seam landed every sender boundary has been a nested resume; the 2026-09-12 peel extended that to the adaptors, which had used the Future transfer path
- Move the Senders section of coroutine.hpp (concepts, Just, Future as sender, SenderAwaitable, stopped_as_optional/shield, race, sync_wait) into ext/execution.hpp, by analogy with std::execution -- Antony 2026-09-12, when the seam settles; then and continues_on from the deleted toy layer are small to reintroduce on the real seam if a caller wants them
- get_stop_token walks the delegate chain to the root per leaf await -- cache in the promise if depth grows.  (The five Basic+Mixin delegate pairs collapsed onto OutcomeDelegate<Derived, T> 2026-09-11; MixinPromise is the remaining T/void pair, on the forwarding side, and AwaitableReceiver's set_value overloads are the receiver-side echo of it)
- Nursery interior request_stop is sticky across reuse (stop_source cannot reset): document single-join-after-request_stop or reconstruct at join
  - DONE 2026-11-08 (documented): a Nursery is single-use by construction (join leaves _counter at -1; fork asserts count >= 0; ~Nursery asserts the join), so there is no reuse for the stop to leak across; comment on Nursery::request_stop
- Pending-arm accounting: an arm (cancelable_after) made from outside any wait-group task is untracked -- its knote dies silently at close(kq) and its frame leaks.  Every arm made inside a task is now cancelled by the root token (LANDED 2026-10-15; since 2026-10-18 the root is a Nursery local to main, `root`, reached through GuiContext::scope, and the wait group is gone; main requests root.request_stop() at the start of GUI/headless shutdown, not in --test-only; ControlSocket bridges the inherited token into its own source).  Open: the GUI's background test run is cut off by quit at its next reactor wait, silently -- fine for quit, but worth a log line; and sync_wait callers outside any task still see a detached token
- MultiProducerSingleConsumerQueue (SPSC is an alias; pop/take/try_pop/try_take, one awaiter): the control socket's per-connection OutQueue and its unified Event inbox (Line | Closed, 2026-10-16) are its users; the headless host idles on sync_wait(with_deadline(events().take())), with_deadline and sync_wait both lift any co_awaitable through lift_awaitable; tests mpsc_queue / mpsc_queue_cancel (in container/mpsc_queue.cpp since 2026-10-18) landed 2026-10-15 (ready, parked, move-only, parked cancel, cancel before park, delivered-before-stop).  (send_some, accept_one, connect_to landed 2026-10-15 on CancelableReadableAwaitable / CancelableWritableAwaitable; recv_some refactored onto the same; a failed recv now reports -errno as the header always said, not -1)
- SingleConsumerEvent (core/, 2026-10-18) replaced the drained MPSC queue in ControlSocket::shutdown; it is a sender in waiting (set_value on a receiver) when the seam wants it
- DONE 2026-10-21: ControlSocket accepted unbounded data per connection.  Now a Transport (io/transport.{hpp,cpp}, multiplayer stage 1) with the line codec: per connection at most 256 lines delivered and unconsumed (the reader parks; a Message holds a Credit until destroyed), at most 16 MiB of unsent replies (else kicked as a slow consumer), and at most 64 requests queued or pending (GuiContext::max_pipelined_requests, refused with an error reply); a kick stops reading, sends one farewell line, and closes after a grace.  FrameCodec (u32 length, u16 tag, per-tag caps checked before the body) and listen_tcp/connect_tcp are in place for the game server (stage 3)
- Control socket is on the reactor since 2026-10-15 (accept loop + per-connection reader/writer nurseries, SPSC queue for replies -- the queue's first user); since 2026-10-18 it runs under the root nursery's token (requested by main before the drain), so the hang this line once warned about is closed
- Coroutine design doc (ext/docs/coroutines.md): the delegate protocol (ask for the continuation before destroy; the stopped hook is the destroy word and must forward; no virtual dtor, parked delegates trivially destructible), cancellation as destruction and the pinned-requester contract, the last-touch rule, the fork-to-join rule, the reactor contract and the fire-won race, the request_stop keepalive rule, Nursery retire ordering, DebugWaitForCollectionCycles "cycles >= 3" -- today these live only in header comments and chat
- Exhaustive-interleaving test (model checker in a define_test) for CancelableKEventFrame; relacy or GenMC for the relaxed-RMW + acquire-garnish claims; a TSan pass is due -- the last clean one predates the delegation port
  - DONE 2026-11-08: TSan pass clean (docs/tsan_2026-11-08.md): 0 reports in 9 full --test-only runs at ef15e22 (idle and loaded), suite complete each time, no hangs; mac-debug also clean.  Follow-ups: the reactor's global g_reactor_tsan_helper edge over-approximates the per-knote kernel edge and can mask races (a per-udata __tsan_release/__tsan_acquire experiment was also clean) -- keep or switch, Antony's call; stale tsan_interface.h include in ext/coroutine.cpp; the CancelableKEventFrame model checker is still the open part of this item
- set_delegate's Debug diagnostic names the colliding delegates; the one-time GUI firing (first run after pulling the port) is most likely a mixed incremental build -- if it recurs the message says which pair
- main.mm spawns the full test suite in the GUI unconditionally (4-5 s under ASan with the small world; QUIT waits for it, and must: the runner is shielded from the root token since 2026-10-17, because tests own nurseries across sleeps and a cancelled test frame asserts in ~Nursery -- the crash-on-quit of 2026-10-17): gate it behind a flag
  - NOTE 2026-11-08: a flag exists (--no-tests opts out); what is undecided is the default -- keep running the suite beside the game, or make it opt-in
- ext/functional.hpp: wry::move_only_function<R(Args...)> (subset: plain signature only, no in_place_type, empty call asserts) -- retire in favor of std::move_only_function when libc++ ships it
  - CHECKED 2026-11-08: Xcode's libc++ still lacks std::move_only_function under -std=c++23
- Nits: soon(y, bar(x)) comment says "schedules foo"; test metadata prints interleave with collector logging (flaky counts in log greps, bit again 2026-09-11); co_await of an error Outcome (the rethrow path) has no direct test
  - DONE 2026-11-08: the soon comment already reads "schedules bar"; the rethrow path has a direct test now (nursery_outcome_rethrow: a child's throw rethrows at co_await of its Outcome, which is left empty).  The log-interleaving nit stands

- DONE 2026-10-21 (multiplayer stage 2): the step's input is explicit.  World::step(commands) stages JOIN/LEAVE on a copy of the World and advances with the actions on the TransactionContext; Player has no queue (its _queue was never saved, so a donated snapshot could diverge); World::_player_for_client (save version 6) routes a client's actions; format_action/parse_action are the action's wire form.  Open: a JOIN's Player first acts in the step after, so an action in the JOIN's own step is dropped (the server never produces one; LocalServer makes the JOIN its own step)
- DONE 2026-10-21 (multiplayer stage 3): the relay (game/relay.{hpp,cpp}: one task over its transport's inbox, timers and controls posted into the same queue; per-client state machine, sequence cursor, per-step quota, flood and handshake-deadline kicks; the whole stream kept and replayed to subscribers), the wire protocol (game/wire.{hpp,cpp}), RemoteServer and Host (game/host.{hpp,cpp}); single player is a Host over a socket pair; `host`/`join`/`leave` and --host/--join; tools/wry_net_smoke.py drives two headless processes over TCP in lockstep.  Open: a joiner replays from genesis until stage 5 (donation); a PING while paused is unsent (liveness comes with the hashes, stage 4); relay-side tests must keep stepping while paused since a posted step overtakes a request still on the socket
- DONE 2026-10-21 (multiplayer stage 4): the hash tripwire.  Every client reports its World hash after each applied step (Server::report_hash -> HASH frame); the relay keeps a 256-step window per client and compares each report with the reference client's (the host's own, designated through the inbox by Relay::adopt(fd, true)) in either arrival order; a disagreement kicks "desync at step n"; a report for an unfinalized step kicks.  Open: liveness timeout on the hash stream (deferred: a stalled host frame must not kick the host's own client); the join gate on hashes (stage 5)
- DONE 2026-10-21 (multiplayer stage 5): join by donation.  REQUEST_SAVE rides the reference's connection after STEP boundary; WorldState serves it from the displayed World once at the boundary (serialize_world on the pool, SAVE frames through the relay's transfer table); the joiner holds its stream until the save lands, loads it (load_world_from_bytes: a Loader that fails instead of asserting; dynamic_cast-checked references; sorted-entry and bounds checks; save_format_hostile_bytes fuzzes it), reports the hash, and the relay admits its JOIN once a report is within join_window of the head.  Open: transfer flow control (11.12), semantic validation of a loaded World (11.13), the replay-from-log path survives only for relays without a reference (tests)
- DONE 2026-10-21 (multiplayer stage 6): the diagnosis harness.  Every session writes saves/session-*.wrylog (game/session_log.{hpp,cpp}: a transcript in the wire protocol's frames: HELLO, WELCOME, the installed World as SAVE frames, its HASH, then a STEP and a HASH per applied step; the newest ten kept); `replay LOG [STEP]` re-runs it from the checkpoint checking every recorded hash (sessionless; `host` resumes play), `compare A B` names the first step where two logs' streams or hashes differ, `dump [FILE]` is the World as pointer-free text (game/world_dump.{hpp,cpp}, the _fields enumerator's fourth visitor), `corrupt` is the deliberate desync; wry_net_smoke.py ends with a kick diagnosed.  Open: the checkpoint is serialized on the main thread at install (multiplayer.md 11.14); no in-process diff of two dumps (an external diff); a divergence from outside the stream is reported, not reproduced (by design)
- DONE 2026-10-25 (multiplayer review fixes): the relay's per-client quota is ONE action per step (relay.hpp; a client's actions sequence at step boundaries; bulk is one action with many effects); the join gate counts only a report the relay compared with the reference's and found equal (Client::verified); the kept step log is dropped once the reference has had its replay; WorldState::_end_session clears _awaiting_save and _donations (a new game after an abandoned join was frozen); Transport::admit queues the arrival under the table's lock and the loop flips+drains under it (an arrival during a stop was stranded); the dial bridges the scope's stop (quit no longer waits for a black-holed connect); SO_KEEPALIVE; a writer that died on a send error ends after the kick grace; FrameCodec/LineCodec split on a ContiguousDeque<char> buffer (constant-time front consumption); the loader refuses a scheduled entity not in the world, an entity under the wrong id or at/beyond the cursor, a machine phase out of range, a Term referring to the World, and a wrong magic; step_from_wire caps entry bodies; printable() refuses unknown OPCODE/MATTER codes (and the GUI's opcode labels guard null names); Server::submit reports what cannot travel; the JOIN's Player sets _free_entity_id; Relay::make asserts the coupled caps.  STILL OPEN from the review: liveness exit after HELLO (slot exhaustion by idle connections; PING unsent); build_id is wire.cpp's compile time; quit closes sessions brutally (clients see EOF, not SHUTDOWN); log rotation can unlink another process's live log; a second concurrent `replay` confuses the first's pending reply; frame caps are per tag not per direction; `step` on a joined client while the host is paused pends (doc now says so)
- FIXED 2026-11-08: the Spawner had the same trap as the Player below: its spawn writes are exclusive and its waits commit-only, so a truck winning the Spawner's cell in the spawn tick silenced it forever.  Now on_abort_retry (test machine_spawner_survives_lost_spawn; the first spawn loses in 3 of its 8 id shifts).  Found by the transaction.md rewrite
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
- Bloom shape?  JJ Abrams long / CCD ?
- Object-mesh pipeline for generating debug normals
- Object-mesh pipeline for generating big cell grids just from 2d arrays of central texCoords
- Instanced small lights
- Cloud/smoke and partial-shadow-map
- Parallax mapping
- Multisampling
- ClearCoat, iridescence
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
