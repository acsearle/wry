# Metal 4 migration plan

Status: PLANNED, DEFERRED (2026-07-25). Judged too big a change to justify by
HUD/diagnostics coexistence alone. Revisit when MTL4 features (explicit
residency, MTL4Compiler, tensors, mesh pipeline descriptors) are wanted on
their own merits. Nothing here is urgent; the workaround below is in force.

## Motivation and context

Enabling Metal Diagnostics + Metal Performance HUD together crashes
intermittently (first seen 2026-07-25, triggered by the New Game scene
transition). Root cause is Apple's, established by static analysis of
GPUToolsDiagnostics.framework (macOS 26.5.2 / Xcode 26 era):

- GPUTools::Diag::SwizzleMTLTracker keeps global unordered_map<unsigned long,
  vector<unsigned long>> state (object pointer -> globalTraceObjectID list,
  the machinery behind "encoder N, draw M" attribution).
- The swizzled _MTLCommandEncoder_endEncoding / _MTLCommandBuffer_commit
  paths do find/erase/insert (std::__next_prime rehash inlined, called on the
  swizzle's own caller thread) with NO synchronization: the whole binary
  imports no locking primitives (no os_unfair_lock, pthread_mutex, objc_sync,
  std::mutex, dispatch_once), and the endEncoding function body contains zero
  atomic instructions.
- dispatch_sync IS used, but only in the MTL4GPUDebug* swizzle paths. The new
  API's tracking is thread-safe; the classic API's is not.
- The HUD draws its overlay on its own serial queue (com.apple.libMTLHud) and
  ends its encoder through the same classic swizzle, concurrently with the
  app. Encoder-churn bursts (scene transition: shadow + G-buffer + lighting
  encoders created back to back) force map growth exactly when the HUD thread
  walks the buckets. Crash stack: HUD thread in __hash_table find/rehash under
  SwizzleMTLTracker::_MTLCommandEncoder_endEncoding.
- The async texture loader committing its upload command buffers is a second,
  rarer app-side race pair through the same unsynchronized commit swizzle.
  This one exists even with the HUD off (diagnostics alone was stable in
  practice; the window is tiny and loads are bursty).

Workaround in force: run Metal Diagnostics OR the Performance HUD, not both.

Stage 7 hypothesis (empirical, not promised): libMTLHud is MTL4-capable (it
references MTL4CommandBuffer, MTL4RenderPassDescriptor, MTL4CommitOptions,
allocates its own MTL4CounterHeap), so a fully-MTL4 app plausibly routes both
app and HUD tracker traffic through the dispatch_sync'd MTL4 swizzles,
eliminating the race. Verify at the end of the migration; file a Feedback
with the analysis above regardless (not filed as of 2026-07-25).

## Verified facts (macOS 26 SDK headers, checked 2026-07-25)

- Apple M1 Max: [device supportsFamily:MTLGPUFamilyMetal4] == YES
  (enum value 5002, API_AVAILABLE macos(26.0)).
- MTL4Compiler returns ordinary id<MTLRenderPipelineState> and id<MTLLibrary>;
  only creation changes, every setRenderPipelineState: call site survives.
  Sync and async (MTL4CompilerTask) variants exist.
- MTL4ArgumentTable: setAddress:(MTLGPUAddress)atIndex:,
  setResource:/setTexture:(MTLResourceID)atIndex:, setSamplerState:.
  There is NO setBytes -- inline constants need an app-side transient buffer.
- MTL4RenderCommandEncoder keeps the fixed-function setters (setCullMode,
  setTriangleFillMode, setDepthStencilState, setViewport, store actions) and
  the draw[Indexed]Primitives family. Resource binding is argument-table-only
  via setArgumentTable:.
- MTL4ComputeCommandEncoder subsumes blit: copyFromBuffer/Texture,
  generateMipmapsForTexture:, fillBuffer, optimizeContentsFor*Access.
- Barriers (implicit hazard tracking is gone):
  barrierAfterQueueStages:beforeQueueStages: (+ AfterStages / EncoderStages
  variants), or MTLFence via updateFence:/waitForFence:.
- MTL4CommandQueue: commit:count: / commit:count:options:,
  waitForDrawable: / signalDrawable:, addResidencySet:, signalEvent: /
  waitForEvent:.
- MTL4CommandBuffer: beginCommandBufferWithAllocator: / endCommandBuffer,
  renderCommandEncoderWithDescriptor:(MTL4RenderPassDescriptor*),
  computeCommandEncoder, useResidencySet:.
- MTL4CommandAllocator from [device newCommandAllocator]; reset and reuse
  per frame in flight.
- CAMetalLayer has a read-only residencySet property covering its drawables;
  attach it to the MTL4 queue alongside the app's own set.
- Completion: MTL4CommitOptions addFeedbackHandler: (MTL4CommitFeedback),
  feedbackQueue property. Replaces addCompletedHandler-style pacing.

## Inventory (2026-07-25 -- re-grep before starting, numbers will drift)

- Bind sites: 30 setFragmentTexture, 16 setVertexBuffer, 15 setVertexBytes,
  6 setFragmentBytes, 1 setFragmentBuffer; 2 setMeshBuffer + 1 setMeshBytes
  (dormant drawBezier: mesh-shader reference); 15 setRenderPipelineState,
  3 setDepthStencilState.
- 15 PSO creations (WryRenderContext helper + WryWorldScene's 9 + others).
- 6 render-pass creation sites: 4 in WryWorldScene (upload/shadow/G-buffer/
  lighting), 1 splash, 1 main menu.
- View layer: custom CAMetalLayer (WryMetalView), no MTKView.
- No MTLVertexDescriptor anywhere (manual vertex pulling from [[buffer(n)]]);
  constexpr samplers only. Shaders.metal needs ZERO edits; binding indices
  map 1:1 to argument-table slots.
- Async texture loader: own MTL3 command buffers on the shared context queue,
  blit generateMipmaps, waitUntilCompleted-then-publish handoff.

## What does not change

- Shaders.metal, all of it.
- MTLTexture/MTLBuffer objects, replaceRegion:, SpriteAtlas CPU mutation.
- MTLDepthStencilState objects and their use.
- PSO and library object types (creation moves to MTL4Compiler).
- CAMetalLayer + WryMetalView + WryScene structure; nextDrawable.
- Fixed-function encoder state calls and draw call shapes.

## Stages

Each stage builds and runs; MTL3 path stays alive as the differential oracle
until the final soak. Vertical slice first (Splash), then the world.

### Stage 0: gate and toggle (about an hour)
Runtime flag (--mtl4 or similar) selecting the path in WryRenderContext.
Log the supportsFamily result. MTL3 path untouched.

### Stage 1: SplashScene vertical slice (one session)
Every concept in miniature on the 1-encoder 1-texture scene: MTL4CommandQueue,
one allocator, begin/endCommandBuffer, MTL4RenderPassDescriptor, one argument
table, one MTL4Compiler PSO, residency (app set + layer.residencySet), and
present discipline: waitForDrawable -> commit -> signalDrawable -> present.
Also proves MTL3/MTL4 queue coexistence (WorldScene still MTL3 behind the
same layer across a scene switch). Exit: A/B toggle pixel-identical splash.

### Stage 2: transient-constant arena (half session)
Per-frame bump allocator over a shared MTLBuffer ring (K = frames in flight)
replacing all set*Bytes: memcpy in, then
[table setAddress:buf.gpuAddress + offset atIndex:n].
House style: build as a standalone brute-force-tested primitive first
(wraparound, alignment, reuse-under-fence), then wire in.

### Stage 3: WorldScene command path (one session, the big one)
Four pass descriptors converted; explicit barriers between passes (linear
graph: upload -> shadow -> G-buffer -> lighting -> present) via
barrierAfterQueueStages: or per-pair MTLFence; WryMesh binding conversion;
the setFragmentTexture/setVertexBuffer sites become argument-table writes;
WryMesh addCompletedHandler -> MTL4CommitOptions feedback handler, which also
gates allocator rotation (2-3 allocators). MainMenuScene rides along.
Verify here: argument-table rebind-between-draws capture semantics against
the header note (rewrite slot / draw / rewrite / draw is the intended
pattern; confirm the capture point before mass conversion).

### Stage 4: PSO creation (half session, mechanical)
15 sites -> MTL4Compiler newRenderPipelineStateWithDescriptor:; functions
become name-based MTL4LibraryFunctionDescriptor instead of MTLFunction.
WryRenderContext helper funnels several sites so real edit count is lower.
drawBezier: mesh pipeline stays dormant; MTL4MeshRenderPipeline.h exists for
when it is revived.

### Stage 5: residency sweep (half session)
Register every allocation at creation: WorldScene textures, map textures,
terrain atlases, SpriteAtlas, WryMesh buffers, the arena. One global
MTLResidencySet attached to the queue. Then run under Metal Diagnostics:
non-resident accesses fault with the resource named, making the diagnostics
layer the completeness checker.

### Stage 6: loader modernization (optional, later)
Loader stays on its MTL3 queue until here (legal; the existing
waitUntilCompleted-then-publish handoff already orders cross-queue).
Convert to MTL4ComputeCommandEncoder (copyFromBuffer, generateMipmaps,
optimizeContents) + MTLEvent handoff when wanted. Note: until this stage the
loader still touches the unsynchronized classic diagnostics swizzle, so the
stage-7 soak is not airtight during load bursts.

### Stage 7: soak and decide
HUD + Metal Diagnostics both on; hammer New Game transitions. If stable, the
coexistence hypothesis held. File the Apple Feedback either way. After a
stable period, delete the MTL3 path (or keep behind a compile flag for one
cycle).

## Sizing

Stages 0-5: four to five focused sessions. Stage 6: one more. Confined to
render/ and the Wry*.mm platform layer per the existing per-header Metal
boundary; nothing sim- or save-visible.

## Open questions (resolve en route)

- Argument-table capture semantics between draws (stage 3, header note).
- MTL4RenderPassDescriptor field parity for the 4-MRT G-buffer + depth
  (expect parity; verify while converting).
- Whether the HUD actually rides the MTL4 swizzles once the app is MTL4
  (stage 7, empirical).
