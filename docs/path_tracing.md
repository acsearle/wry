# Path-traced reference renderer

Status: BUILT (2026-11-19), uncommitted.  `path-trace` in the console.

A slow, noisy, progressive path tracer for the world view, as a reference
the fast deferred renderer (shadow map, split-sum IBL, GTAO) can be judged
against.  Same scene, same camera, same materials and BRDF, same exposure
and output encoding; only the light transport differs.  So a difference
image between a `path-trace` screenshot and the normal frame is the fast
renderer's lighting error, plus Monte Carlo noise (`path-trace-status`
reports it).

Code: `render/PathTrace.metal` (kernel, resolve), `render/WryPathTracer.mm`
(host: scene upload, restart key, scheduling), `render/PathTraceTypes.h`
(shared structs), `render/path_trace_grid.{hpp,cpp}` and
`render/path_trace_bvh.{hpp,cpp}` (CPU acceleration structures, with
tests), `render/ShaderSurfaces.h` (surface code shared with the raster),
and `-encodePathTraceWithCommandBuffer:` in `WryWorldScene.mm`.


## 1. Using it

All local commands; pause the world first (a running world changes every
step, and each change restarts the accumulation).

- `path-trace [VIEW] [ANTIALIAS] [BOUNCES] [MARGIN] [TARGET]` turns it on
  and restarts the accumulation; `path-trace "off"` goes back to the fast
  renderer.  Defaults: `"beauty" false 16 4 4096`.  VIEW is a quoted
  word:
  - `beauty`: the whole estimate.
  - `direct`: emission and the sun at the first hit.  Counterpart of the
    fast renderer's Color attachment (its sun with the shadow map).
  - `sky`: the sky the first bounce reaches, through the BRDF, occluded by
    the real geometry.  Counterpart of the fast renderer's Ambient x AO.
  - `sky-open`: the same, never occluded.  Counterpart of Ambient alone
    (the split-sum IBL's own error, without the occlusion).
  - `indirect`: everything else (interreflection, which the fast renderer
    lacks).  `direct + sky + indirect = beauty`; `ambient` is
    `beauty - direct`.
  - `occlusion`: the cosine-weighted share of the sky the first hit sees,
    unbounded range.  Counterpart of `ssao "gtao-show"` (whose range is
    0.5 units).
  - `albedo`, `normal`: the first hit's, decals applied.
  - `error`: the beauty's per-pixel standard error, per channel.
- `path-trace-wait N` replies once every pixel has N samples (N at most
  the target); it fails if tracing is turned off, restarted or given a
  lower target meanwhile.  Switching between `beauty` and `error` with
  nothing else changed keeps the accumulation; any other `path-trace`
  restarts it.
- `path-trace-status`: samples per pixel, restarts (and a warning when it
  restarts every frame: the world is running), the last frame's GPU time
  and dispatch shape, and the noise (each pixel's standard error
  relative to its mean: median and 90th percentile over the image).

Offscreen recipe (see the wry-driving memory for the launch):

    load "pt_reference"
    pause
    step N                          (to a fixed head)
    screenshot "/abs/raster.png"    (the fast frame)
    path-trace "beauty" false 16 4 1024
    path-trace-wait 1024
    screenshot "/abs/traced.png"

The two PNGs compare pixel for pixel (decode sRGB to linear first), except
that path-trace mode hides the cursor tile (else every mouse move would
restart it).  Debug build, M1 Max, 1920 x 1080: about 30 ms of GPU per
sample per pixel; 1024 samples (median relative error 0.6%) take about 40 s.


## 2. What it computes

The rendering equation over the world view's actual geometry, with:

- the sun as the same delta directional light (`light_direction`,
  irradiance (1.08, 1.0, 0.84) x exposure, now one constant both renderers
  read), sampled by next-event estimation with an exact shadow ray (no map,
  no bias, no screen margin);
- the sky as the environment PNG itself (`day.png`, equirectangular,
  sRGB-decoded, bilinear at level 0, the prefilter's direction
  convention), times exposure, seen by every ray that escapes -- not the
  prefiltered cubes.  day.png has no sun disc, so nothing is counted twice;
- the same BRDF as the raster's direct light, now one function
  (`brdf_cosine` in ShaderSurfaces.h): Lambert kD * albedo / pi with
  kD = (1 - F)(1 - metallic), GGX D with alpha = roughness^2, Schlick-Smith
  G with k = (r + 1)^2 / 8, Schlick F with F0 = mix(0.04, albedo,
  metallic), and the clear coat layered as base * (1 - Fc) + Dc * Kelemen
  * Fc (rc = 0.125);
- indirect light by sampling that BRDF: a mixture of a cosine lobe, GGX
  visible normals (Heitz 2018) for the base and for the coat, picked by
  estimated reflectance, weighted by the mixture's pdf; cosine only where V
  is below the shading normal; a direction below the geometric normal ends
  the path.  Russian roulette from the fourth vertex; 16 bounces at most;
- the meshes' baked occlusion maps (the container's) applied to light
  arriving by bounces, never to the sun, since they stand for geometry the
  proxy lacks.  Emission is added as the G-buffer adds it (not exposed);
- material channels quantized to 8 bits as the G-buffer stores them, and
  decals painted at half precision as the G-buffer holds them.

Output is linear radiance x exposure in the frame's colour texture, so
`screenshot` encodes it exactly as it encodes a normal frame (clip, sRGB
OETF), with the GUI on top.  A non-finite sample is dropped (and counted).
The RNG is pcg4d of (pixel, the pixel's sample number, dimension): an
image is a pure function of the scene and the sample count.


## 3. Primary rays and texture filtering

`antialias false` (default): one ray through each pixel centre, from the
same `camera_ray_centre / dx / dy` basis the G-buffer is rasterized with,
between the projection's near and far planes, so silhouettes, glyphs and
decals land on the same pixels as the fast frame.  Texture footprints at
the primary hit come from ray differentials on the hit's tangent plane and
stand in for the raster's screen derivatives: the board's `fwidth(p)`, the
slab print's mip level (from the ray's entry into the slab's cuboid, the
point the raster's `fwidth` differentiates), the decals' uv gradients and
the meshes' (barycentric differentials, sampled with `gradient2d`).
Measured on the board's lines: the same ink and the same lit board, with
partial-coverage edge pixels a little different (the raster's 2 x 2-quad
derivatives against analytic ones).

`antialias true`: rays jittered over the pixel and every footprint zero
(texture level 0, the board's lines and marks as hard edges): the
supersampled, unfiltered truth.  Secondary hits always use footprint zero.

Primary rays pass what the raster would not draw: mesh back faces (it
culls them) and everything gathered only for the margin (below).


## 4. The scene and its acceleration

The scene is the frame's own instance lists -- the symbol slabs, the print
and string decals, every WryMesh's instances, the board's kind map and
look -- gathered, in path-trace mode, over the view's cells widened by the
margin (default 4 cells) for objects and by 32 cells for the board's kinds,
so casters and reflectors a few units off screen exist.  Each slab and mesh
instance is flagged with whether the raster would have drawn it (its
entity is located in view, or its term's cell is), and primary rays skip
the rest.  Beyond the kind map the board is a hole, like an ungenerated
cell.

Nearly everything sits in one cell of the board's grid, so:

- A 2D uniform grid of world cells over the items' bounding boxes
  (`path_trace_grid`).  Every intersectable (slab, mesh instance) is listed
  in each cell its world box overlaps (the mine spans two cells north of
  its Source, a moving token three, a ship's modules may lie well off
  screen), with the cell's z range.  Rays walk it with a 2D DDA
  (Amanatides and Woo) after clipping to the grid's 3D box, skipping a cell
  when the ray's heights across it miss the cell's z range, so a ray
  climbing toward the sun leaves within a cell or two.  An item found from
  an earlier cell than its hit's is kept as a candidate until the walk
  passes it.
- The board is the analytic plane z = 0, intersected once per ray, capping
  the walk.
- Decals are listed per cell in a second grid, prints then strings in
  raster order; at any hit, those of the hit's cell are applied in that
  order, so a print shows on its tile in reflections too.
- Each mesh (OBJ) gets a BVH built once on the CPU (binned SAH,
  `path_trace_bvh`), all meshes concatenated into one node, triangle and
  vertex buffer; an instance is tested in object space without
  renormalizing the direction, so its distances are the world's.
- A slab is tested with the raster's own `symbol_march`, in slab space;
  light and shadow rays get 512 march steps where the raster's 160 would
  leak grazing rays.  Secondary rays start 0.001 units (0.13 slab texels)
  off the surface, clear of the march's 0.02-texel hit threshold.

Shared shading code (BRDF, the slab field and march, `terrain_albedo` as a
function of the ground point and a footprint, `decal_paint` as a function
of a surface point and a footprint) lives in `render/ShaderSurfaces.h`,
included by Shaders.metal and PathTrace.metal, so the two renderers cannot
drift.  Moving it there changed 3 pixels of the raster frame by 1/255
(fast-math reassociation in the sun term).


## 5. Accumulation and scheduling

Two float4 per pixel: (sum, count) and (sum of squares, dropped), in a
shared buffer.  Each frame traces every Nth row from a rotating phase,
`samples` more per pixel, sized from the measured GPU cost per row-sample
to fit about 40 ms (windowed) or 250 ms (offscreen): a command buffer that
runs for seconds is killed, and the process with it.  A pass's samples and
stride are fixed until its phases have covered every row, so every pixel
leaves a pass with the same count; interleaving keeps each frame's cost a
fair share of the image, and the first frame after a restart already
covers the screen (the resolve fills rows not traced yet from the traced
row above).  The first pass after a restart takes one sample per pixel,
since the cost it is sized by was measured on the previous image.  (A
first version traced contiguous bands from the top: windowed, a restart
every frame left only a strip at the top of the window.)
The composite pass draws the mean straight from the accumulator, under
the GUI.  GOTCHA: that resolve must write its colour in the Lighting
raster order group, as the composite and the GUI do; written outside it,
the GUI's blends lost whole GPU tiles to it.

The accumulation restarts whenever any input of the image changes: a hash
over the uniforms, the drawable size, the settings and the `path-trace`
generation, the board's look and kinds, the instances field by field (not
their padding), the grid, and the identity of every texture bound.  The
digging shake is now a hash of the machine and the world time (it was a
fresh random offset every frame), so a paused world is static in both
renderers.  Accumulation stops at the target; the frame then shows the
converged image without tracing.


## 6. First comparison (2026-11-19)

The reference save `pt_reference` stepped to head 400, default zoom,
1024 samples, against the fast frame:

- Open board, sunlit: equal to the measurement's precision (0.632 vs 0.632
  linear luminance on a cell, 0.119 vs 0.116-0.120 on the lines).  So the
  units, exposure, sun, sky and BRDF agree, and the fast renderer's diffuse
  IBL and GTAO are close to right where little is in the way.
- Vertical faces toward the camera are too dark in the fast frame (the
  path tracer is brighter by up to ~0.15 linear): its irradiance cube
  lights them from the environment's brown lower half, where the truth is
  the bright sunlit board.  Biggest systematic error on screen.
- Colour bleeding: the yellow tokens tint the board around them in the
  reference (the `indirect` view); the fast renderer has no bounce.
- The fast frame is brighter around the dark matter piles and their tiles
  (GTAO's 0.5-unit range against the true occlusion).
- The clear-coated yellow walls throw a sun caustic onto the board: the
  slowest-converging part of the image (speckle at 1024 samples).  Real,
  not a bug: the coat BRDF also gains energy at grazing angles (directional
  albedo about 1.28 at NdotV 0.05).


## 7. Known differences that are not lighting

- Mesh alpha testing (`albedo.a < 0.5` discards in the raster) is not
  modelled; no mesh texture has alpha today.
- Raster pixels with no surface (board holes) show the prefiltered sky
  dimly; the reference shows the sky itself.  Mask them in comparisons.
- The cursor tile is hidden in path-trace mode.
- The raster culls geometry more than about a cell off screen; tall
  objects just south of the view (an exploded stack) would appear in
  neither renderer's primary view, but do cast and reflect in the
  reference.
