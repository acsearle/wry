# Radiance cascades for wry

Status: INVESTIGATED, NOT DECIDED (2026-11-08). Nothing built; no numbers
here are measured on this renderer except the GTAO ones quoted from
`docs/todo.md`. Answers the TODO "Investigate radiance cascade as a GTAO
alternative. Can we ditch the shadow map and directional light entirely?"

Short answer: radiance cascades (RC) can replace GTAO *and* the irradiance
cube lookup with one estimator that integrates the sky per direction (the
BACK POCKET "directional occlusion" note, done at full-screen range for a
near-constant cost), and can add one bounce for free-ish. They should *not*
carry the sun: a 0.5 degree source ten thousand times brighter per steradian
than the sky is exactly the case the method handles worst. The shadow map
can go only if something else gives the sun a world-space visibility answer;
in this game the natural something is a top-down heightfield that a
game-specific RC variant needs anyway. Recommendation and the decisions
needed are at the end.


## 1. How the scene is lit today

Read alongside `client/client/render/Shaders.metal`,
`client/client/render/WryWorldScene.mm` (the frame in `-drawWithCommandBuffer:`)
and `WorldState::_regenerate_uniforms()` in `client/client/game/world_state.cpp`.

### The scene

- Camera: fixed orientation, offset (0, -8, 16) from the focus (63 degrees
  above the horizon, 17.9 units away at zoom 1), horizontal field of view
  about 53 degrees, so roughly 18 x 10 world units are on screen at 16:9.
  It pans (the focus moves, sub-tile fraction carried in render space) and
  dollies (zoom scales the offset); it never rotates.
- Geometry: a flat board at z = 0 (one quad over the kind map,
  `terrain_fragment_function`), the sea, and on it a few hundred to a few
  thousand low pieces -- "child's wooden blocks": the symbol slabs (raised
  above the ground, glyphs marched in their fragment shader), number tiles
  with printed decals, furnace / mine / truck / container / sandbox meshes,
  and the three-part ship. Little is taller than a unit or two; almost
  nothing overhangs except the slabs and the ship.
- Sun: `light_direction = normalize(-2, -1, 3)`, 53 degrees up, from the
  left and the camera side, so shadows fall right and up the screen, about
  0.75 h long for a caster of height h. Irradiance ~1 at exposure, warm
  (1.08, 1.0, 0.84).
- Sky: `day.png`, a daytime desert cube; its cosine-weighted mean radiance is
  about 0.2, so a sunlit white surface gets roughly equal shares from sun
  and sky (the balance comment in WryWorldScene.mm).

### The passes

1. **Shadow map** (Depth32Float, drawable size x 1.25 rounded to 256). The
   projection is the clever part (`_regenerate_uniforms`): shear the world so
   the sun shines straight down -z, then apply the camera's view-projection
   to x, y, w only. The ground plane maps texel-per-pixel onto the screen,
   so ground shadows are exact; the 25% margin catches casters above the
   ground near the screen edge. The board is not drawn (it cannot cast);
   symbol slabs march their glyph along the light rays
   (`symbol_shadow_fragment_function`); meshes use `shadow_fragment_function`
   (alpha test only). Slope-scaled bias 1.
2. **G-buffer** (one render pass, raster order groups): albedo, normal,
   material (r = baked/decal occlusion, g = roughness, b = metallic), linear
   view depth (0 = sky). Symbol decals multiply the analytic stroke
   occlusion into material.r; print decals write albedo.
3. **Image-based light** in the same pass, into its own attachment
   (`AAPLColorIndexAmbient`): `kD * albedo * irradianceCube(N) +
   splitSum specular * prefilteredCube(R, lod)`, all times material.r.
   The irradiance cube is the cosine-weighted hemisphere mean (the 1/pi bug
   fixed 2026-10-30).
4. **Directional light**, still in pass 1, into the Color attachment: GGX /
   Smith / Schlick times `step(z, shadowSample)` -- a hard, nearest-sampled,
   unfiltered shadow. Off the map counts as lit.
5. **Screen-space occlusion** (compute): depth pyramid, GTAO (default 3
   slices x 4 steps per side, half resolution, one thread per 2 x 2 block)
   or Luft, then a separable depth-aware blur.
6. **Composite** (second render pass): `color + ambient * ao`, ao upsampled
   bilaterally, then the GUI.
7. **Bloom**: an MPS Gaussian into `_addedTexture`, computed and discarded
   (the TODO "Bloom is calculated but not used"). Orthogonal to everything
   here.

So the ambient term is the factored approximation `AO x E(N)`, exact only
under a uniform sky; three occlusion mechanisms coexist (GTAO, Luft, the
stroke decals -- the DECIDE line); the sun is hard-shadowed by a map that
costs a full extra geometry pass.

### What GTAO costs

From the 2026-10-30/31 DONE notes, offscreen, M1 Max, pass cost = frame
minus `ssao off`: about 1 ms at 1080p and 2.5-3.3 ms at native 3456 x 2080
(the "~4 ms" figure is the earlier windowed, clock-inflated number), with a
~1.1 ms floor that is not taps (setup, three dispatches, the full-res
upsample), ~0.06 ms per tap per half-res block at native, and ~25%
run-to-run drift. The shadow pass has never been timed on its own.


## 2. Radiance cascades

### What they compute

Sannikov's observation (the "penumbra condition"): to resolve the
light arriving from distance D you need probes spaced about D apart in
position and about 1/D in angle. Near light needs dense probes and few
directions; far light needs many directions but sparse probes. So store the
radiance field as N cascades; cascade i holds radiance over the interval
[t_i, t_i+1] with t_i ~ 2^i, probe spacing ~ 2^i, angular spacing ~ 2^-i.
In 2D each cascade costs about the same as cascade 0 (or halves), so N
cascades cost under ~2x one, while the equivalent ray count grows as 2^N.

Each cascade texel marches its own short interval and stores radiance plus
transmittance. The **merge** then runs from the top cascade down:
`L(a, c) = L(a, b) + beta(a, b) * L(b, c)` -- each interval's radiance plus
its transmittance times the interpolated radiance of the matching
directions of the next cascade's nearest probes. Cascade 0 ends up holding
the full radiance field to infinity; rays that leave the last interval read
the environment, which is how a sky at infinity enters. Single-shot: no
stochastic rays, no noise, no temporal accumulation, zero latency, constant
cost independent of the number of lights.

Primary sources:
- Sannikov, "Radiance Cascades: A Novel Approach to Calculating Global
  Illumination" [WIP, never submitted], https://github.com/Raikiri/RadianceCascadesPaper
  (PDF at `out_latexmk2/RadianceCascades.pdf`)
- Osborne and Sannikov 2024, the astrophysics formalisation, with the
  bilinear fix: https://arxiv.org/abs/2408.14425
- Freeman, Sannikov and Margel 2025, "Holographic Radiance Cascades for 2D
  Global Illumination": https://arxiv.org/abs/2505.02041
- Freeman, Sannikov and Pitt-Francis 2026, "Split Radiance Cascades:
  Real-Time Global Illumination via Sparse Radiance Probes" (world-space
  3D): https://arxiv.org/abs/2607.20384
- Sannikov's ExileCon 2023 talk "Rendering Path of Exile 2"
  (https://youtu.be/TrHHTQqmAaM, link as given on Hacker News; not
  re-watched for this note); summary at
  https://80.lv/articles/radiance-cascades-new-approach-to-calculating-global-illumination

### Variants

- **2D (flatland).** The clean case and where the method is unambiguously
  good: every pixel a probe, light from anywhere on screen, penumbrae
  for free. Holographic RC (2025) restructures the cascades to cut the
  redundancy and fixes the original's trouble with hard shadows; 1.85 ms at
  512^2 and 7.67 ms at 1024^2 on an RTX 3080 Laptop, and the authors name the
  memory of the transmittance cascades as the obstacle to 3D.
- **Screen-space "2.5D" (Path of Exile 2).** Probes on the screen grid, the
  hierarchy flatland-like (~2^i x 1 per cascade per the paper), intervals
  marched through the depth buffer, four rays at a time, occlusion kept as
  a 128-bit bitmask per texel -- the same horizon-sector idea as visibility
  bitmasks (Therrien, Levesque and Gilet 2023,
  https://arxiv.org/abs/2301.11376). A GGG lead on Hacker News: the source
  data is screen space but "generates a full 3d data structure by ray
  marching the depth buffer", 0.8 ms at 4K "High" on a 4090, no temporal
  accumulation (https://news.ycombinator.com/item?id=41957008); the paper
  gives ~3 ms on a GTX 1050. Its stated limits: "no data about any geometry
  behind the depth buffer", off-screen lights cast no light and off-screen
  occluders no shadow. I found nothing published on how Path of Exile 2
  shadows its sun, and no source anywhere that carries a sun through RC.
- **Screen-space probes, world-space intervals.** Probes from the depth
  buffer, rays traced against an SDF / BVH / hardware RT so off-screen
  geometry counts. The paper calls it "much less efficient"; Gavras's
  Chalmers thesis (2025) studies it and the depth-aware ("Bilinear 3D")
  upsample: https://odr.chalmers.se/items/3ee9fb4e-1880-46c2-802a-a660e38dc9ee/full.
  three-rc does this with a BVH: https://github.com/CodyJasonBennett/three-rc
- **3D world space.** Full probe volumes make cascade 0 "practically
  equivalent to voxelizing the entire scene"; Split RC (2026) stores sparse
  probes in a hashmap near visible surfaces and still reports leaks that
  brighten shadows and over-blurred hard shadow detail. For probes on a
  *surface* (a 2D manifold in 3D) the paper notes each cascade costs as
  much as cascade 0, so memory grows linearly in N -- this is the case that
  matters below.

### Failure modes

- **Ringing at the merge.** Far-cascade probes sit elsewhere than the near
  probe whose interval they continue (parallax), so the joined ray bends;
  energy is not conserved where cascades meet. Worst case: a small, very
  bright source -- the sun. The **bilinear fix** traces each of the four
  bilinear neighbours' intervals toward that neighbour's own probe, 4x the
  rays on every cascade but the top; it also cures leaks through small
  occluders. Implementations offer it as stochastic / nearest / full 4x
  (e.g. https://github.com/entropylost/amida). Introduction:
  https://m4xc.dev/articles/fundamental-rc/
- **Leaks.** Merge-time interpolation lets light through thin occluders,
  and far cascades marching a coarse depth mip skip thin walls (our depth
  pyramid deliberately weights toward the far side, which is the leaky
  direction for this purpose).
- **Depth discontinuities.** Interpolating between a probe on a block top
  and one on the ground beside it mixes two unrelated hemispheres: halos at
  silhouettes unless the upsample and the merge are depth-weighted.
- **Thickness.** A depth buffer is a surface, not a solid; screen-space
  marches must assume something behind it (infinite: heightfield-like
  over-occlusion; fixed: leaks behind thick things).
- **Off screen.** As above, in every screen-space variant.
- **Flicker under motion.** Abou Dan and Jensen's Chalmers thesis (screen
  probes + visibility bitmask, 2025: ~2 / 6 / 26 ms for low / medium / high,
  GPU not stated on the abstract page) found flicker during small camera
  movements the worst artefact: the probe grid slides across the scene
  (https://odr.chalmers.se/items/9e42007a-bb60-440f-ba28-04410822758f/full).
- **Penumbra hypothesis.** Unproven; it fails for infinitesimal sources,
  and cascade 0's parameters "take some amount of fiddling".


## 3. For this game

### Ambient: yes, plausibly better than GTAO x E(N)

What RC would replace: GTAO (and Luft, and eventually the stroke decals),
the irradiance cube lookup, and the `AO x E` factorisation. For each
receiver it integrates the environment's radiance over exactly the
directions that escape, so a strongly textured sky (the "indoors with a
window" TODO) lights a block from the window side and not the wall side --
the thing AO x E cannot do, and the stated prize of the BACK POCKET note.
Rays that hit the depth buffer can return the Color attachment, which at
compute time already holds the sunlit direct term (pass 1 finishes before
the occlusion encoder), so one diffuse bounce -- red ground under a red
block -- costs a read per hit and no history buffer.

Under a uniform grey sky it must reduce to GTAO; the stroke decals'
analytic maths stays the oracle, as it was for GTAO.

Specular is not RC's job. The metals and the sea keep the prefiltered cube;
its occlusion can stay as today (the scalar times the specular) or, later,
read the cascade in the reflection direction for rough surfaces (the talk
reportedly covers specular GI).

### The sun: keep it analytic

Putting the sun into the environment at infinity and letting the cascades
carry it is possible in principle and wrong in practice here:

- **Resolution.** The sun's solid angle is ~7e-5 sr, ~1e-5 of the
  hemisphere. No cascade has bins that small; its energy lands in one
  top-cascade bin and is interpolated across neighbours, so its direction is
  quantised and NdotL near the terminator is smeared.
- **Shadow look.** A shadow edge from an occluder at distance d is blurred
  by the spatial interpolation of the cascade covering d, whose probe
  spacing grows like d. So the penumbra grows linearly with occluder
  distance: contact-hardening, as from an area sun of angular size about
  (probe spacing / interval start), a design constant of several degrees
  -- the paper's "as if they were cast by an area light source". The real
  sun's penumbra at 1 unit is ~1 cm, ~2 px at native: today's hard shadows
  are physically right. A soft, contact-hardening "lamp over the toy
  table" look might suit the blocks, but it is an art decision, not a
  correction.
- **Artefacts.** The sun is the textbook ringing case and needs the
  bilinear fix at 4x rays on every cascade to be clean.
- **Energy.** It would make half the image's light depend on the
  cascades' least reliable part.

Keep the analytic GGX sun and give it a visibility term from somewhere. If
the art direction wants soft sun shadows, filter that visibility (PCSS-style
or by marching a cone), which is cheap and controllable.

### Can the shadow map go?

Only if the sun's visibility comes from elsewhere. Options, in order of
how much they fit this game:

- **A top-down heightfield** (two-layer: top and underside per texel, to
  keep the slabs' and the ship's overhangs) rendered each frame orthographic
  straight down over the screen's ground footprint plus a margin. A sun ray
  marched over it gives hard shadows with off-screen casters included. That
  is still one geometry pass -- it replaces the sheared shadow map rather
  than eliminating rendering -- but it is the same input a heightfield RC
  needs (below), so the frame ends with one world-space depth render serving
  sun, sky and bounce.
- **Screen-space sun rays** (Bend's contact shadows,
  https://bendstudio.com/blog/inside-bend-screen-space-shadows): march the
  depth buffer along L. Shadows from casters off the left and bottom edges
  (the sun side) vanish within ~0.75 h of the edge and pop in when panning;
  the ship near the bottom edge is the visible case. Days Gone uses this to
  complement shadow maps, not replace them.
- **The sun inside the cascades**: see above; no.

Answer to the TODO: we can ditch the *directional-light shadow map* (for a
heightfield we need anyway), not the *directional light*.

### What the fixed camera buys

- **No rotation, constant tilt.** World distance maps to nearly constant
  pixels (the GTAO note already relies on it), so cascade intervals can be
  set in world units without per-pixel surprises.
- **Scenes are low.** From a point on the board, a ray above ~20 degrees
  clears every piece within a few units. Long intervals see sky almost
  always, so the hierarchy can stop at a few units and hand the rest to the
  environment lookup: fewer cascades than a full-screen solution, and the
  failure modes of the far cascades (leaks through coarse mips) mostly do
  not arise.
- **Stable grids, if anchored.** The flicker the Chalmers thesis reports
  comes from the grid sliding under the scene. With a fixed orientation and
  a flat board we can anchor probes to the world (to board texels) and snap
  pans to whole probe spacings, zooms to powers of two; then a static board
  produces bit-identical cascades frame to frame. In screen space this is
  only approximately possible (probes are screen pixels; we can offset the
  grid by the focus's sub-probe fraction).
- **A heightfield is almost exact.** The scene is a board with things on it.
  That suggests a variant I have not seen published: **heightfield RC** --
  probes on the heightfield's top surface on a world-anchored grid
  (the paper's "2D manifold in 3D" case), intervals marched over the
  two-layer heightfield in azimuth with an elevation bitmask (the PoE2
  trick), the sky at infinity, bounce from a top-down albedo x direct-light
  layer. Cost independent of display resolution, off-screen casters
  included within the margin, no screen-edge or behind-the-depth-buffer
  gaps. What it does badly: vertical faces (the south faces the camera sees
  at 63 degrees are a real share of the screen) are not on the probe
  surface and must interpolate from the probes at their foot and top using
  the stored directional radiance against their own normal -- approximate,
  wrong by up to a block height. Overhangs beyond two layers (ship
  interiors, stacked slabs) leak.

### What breaks (screen-space variant)

- Casters off the sun-side edges and tall objects near any edge (the
  ship): no occlusion from what is not on screen.
- The over-occluded "shadow" behind each block as seen from the camera
  (infinite thickness): ~0.5 h of ground behind a block of height h at this
  tilt. GTAO has the same today.
- Floating slabs: the gap under them is invisible to the camera, so the
  ground under a slab edge reads as enclosed. The stroke decals model this
  gap correctly today; GTAO does not either.
- Pans: grid slide flicker unless snapped as above.
- Resolution: cost scales with pixels; native 3456 x 2080 is 3.5x 1080p.

### Estimated cost on M-class GPUs

Not measured; order-of-magnitude, to be replaced by the prototype's numbers.

- **From the literature.** 0.8 ms at 4K on a 4090 (GGG); the M1 Max is
  roughly 1/5 to 1/8 of a 4090 in ALU and bandwidth, so 4-6 ms at 4K-class
  resolutions, i.e. ~3.5-5 ms at native and ~1-1.5 ms at 1080p. ~3 ms on a
  GTX 1050 (resolution unstated), which the M1 Max outruns ~5x: ~0.6 ms.
- **From our own GTAO model** (~0.06 ms per tap per half-res block at
  native). Screen-space RC at half resolution with ~1.8 M texels per cascade
  (constant per cascade: spatial halving times angular branching), ~4
  depth-pyramid taps per interval (intervals and mip levels both double, as
  the GTAO taps already exploit) and ~4 filtered reads per texel for the
  merge (direction-first layout so one bilinear fetch serves four probes):
  ~0.3-0.5 ms per cascade at native. Six to eight cascades cover the few
  units that matter (2 px to ~500 half-res px), so ~2-4 ms at native and
  ~0.7-1.2 ms at 1080p, plus the existing ~1 ms floor (upsample,
  dispatches), plus ~0.3 ms of bandwidth per cascade for an RGBA16F
  write-read at native. Without the bilinear fix; with it, the march part
  x4 below the top cascade.
- **Heightfield RC.** At 1/16-unit cascade-0 spacing over 24 x 16 units
  (zoom 1 plus margin) ~1.6 M texels per cascade, about the same as the
  screen-space half-res case at native but fixed regardless of resolution;
  plus the heightfield render (replacing the shadow pass) and a full- or
  half-res gather.

So: comparable to today's GTAO (2.5-3.3 ms native, ~1 ms 1080p), within a
factor of two either way, while also replacing the irradiance lookup and
adding bounce; with the heightfield variant also retiring the shadow pass.
Not a performance win on its own; a quality win at about the same price.
If cost alone is the concern, GTAO's remaining levers (the floor, the
upsample) are cheaper to pull.

### Memory

Screen-space, half res at native: ~1.8 M texels x 8 bytes (RGBA16F,
radiance + transmittance; bitmasks elsewhere) ~15 MB per cascade, two
ping-ponged for the merge plus cascade 0 kept: ~45 MB. Negligible against
unified memory; the bandwidth, not the footprint, is the cost.


## 4. Recommendation

Prototype screen-space RC for the *ambient* only, behind the existing
`ssao` toggle, and keep the analytic sun and its shadow map untouched until
the screenshots argue for more. Decide the heightfield variant (and with it
the shadow map's fate) on that evidence. Do not put the sun in the cascades.

Staged plan; each stage ends in a screenshot comparison with `ssao gtao`
on the same frames (the offscreen client + `wryctl` recipe), under `day.png`
and under a strongly directional sky (the indoor-window map from the
lighting TODO), and a timing with `gpu-time` offscreen at 1080p and native.

1. **Plumbing: an RGB ambient factor.** Let the occlusion output be RGB
   (the ratio of occluded to unoccluded sky irradiance per channel) so the
   composite stays `color + ambient * factor` and every estimator is a
   drop-in. GTAO writes grey. Small, and the same interface the BACK POCKET
   bitmask variant would need.
2. **`ssao rc`: sky only, no bounce.** Screen-space probes at half res,
   world-unit intervals, depth-pyramid marches with an elevation bitmask,
   the environment cube (at the mip of each bin's solid angle) for escaping
   rays, merge top-down, depth-aware upsample into the factor. Grid offset
   by the focus's sub-probe fraction to test pan stability. Check under a
   uniform grey sky against GTAO and the stroke decals (the oracle).
3. **Bounce.** Hits return the Color attachment (direct sun light), times
   albedo at the receiver. Look for colour bleeding and over-brightening of
   the background (the Chalmers result).
4. **Heightfield variant, only if 2-3 win but the screen edges, the slabs
   or the pans visibly lose:** a two-layer top-down heightfield render, the
   sun's visibility from it (retire the sheared shadow map and the symbol
   slab shadow march), the cascades on a world-anchored grid over it. Time
   the old shadow pass first, so the comparison is honest.
5. **Clean-up** whichever way it goes: settle the DECIDE line (one AO
   mechanism), delete the losers and the measuring aids.

## Decisions for Antony

- **The AO DECIDE line first.** RC is a fourth candidate; settling GTAO vs
  the decals (vs RC) is the same decision, and the prototype only makes
  sense if the ambient stays a separate, screen-space-composited term.
- **Sun shadow look.** Physically hard (today), or the soft
  contact-hardening "lamp over a toy table"? Only the latter would justify
  any RC-carried or filtered sun.
- **Target sky.** Is the game lit by a textured sky (window, dawn/dusk
  blends -- the `ibl_transform` "resurrect blending" TODO) or a near-uniform
  one? Under a near-uniform sky RC buys little over GTAO x E but bounce.
- **Is bounce light wanted** (colour bleeding between bright blocks)? It is
  the cheapest extra RC offers and changes the look most.
- **Budget and resolution.** What ms at native is acceptable for ambient,
  and is native the target (it favours the resolution-independent
  heightfield variant)?
- **Screen-space limits.** Are missing occlusion from off-screen casters
  and screen-edge popping acceptable for ambient (probably) and for the sun
  (probably not)? This decides whether stage 4 is needed.
- **Heightfield as a world representation.** Accepting "the world is a
  two-layer heightfield" constrains future content (bridges, interiors,
  stacked pieces); worth deciding as a design rule rather than discovering.
