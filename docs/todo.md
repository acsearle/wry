# Todo

List of one-line reminders of things to think and do
- LATER (2026-10-11): _symbols atlas cells abut; box-filtered mips don't bleed (power-of-two cells), but a wider mip kernel or bilinear taps at deep levels would -- pad/gutter the atlas cells (tools/make_assets.py) before adopting a custom Gaussian mip kernel
- CHECK (2026-10-10): SpriteAtlas::commit rotates a ring of 4 shared MTLBuffers with no fence/semaphore, and the menu scene commits twice per frame; with up to 3 drawables in flight the CPU may overwrite a buffer the GPU is still reading.  Pre-existing, unobserved; fix with a frame-in-flight semaphore or a per-commit offset into a per-frame buffer
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
    - OPEN: a tool reading clang's AST to check every GarbageCollected class's members against its _fields (deferred by Antony 2026-10-20); loaders are still hand-written field by field; the save header does not yet carry the World hash for the loader to verify after load; unified_leaf still applies modifiers one at a time through insert (transient leaves, same fix as rebuild_serial); seal-at-commit hashes the entity on the leaf's thread with a cold cache (a hot-cache seal at the end of notify would need the interpreter to finish the clone before proposing it)
  - IDEA 2026-09-24 (not now): hash the LIVE World every step, not only the save; clients upload it per step as proof of sync and liveness
    - Augment AMT nodes with a hash field and let the rebuild maintain it: untouched subtrees keep theirs, each assembled node recombines its children's.  Cost O(nodes rebuilt per step), which the rebuild already touches.  Nodes are immutable once published, so compute at assembly (or lazily into a mutable cached field, cf. the ReadyValue late-write policy)
    - Definition (Antony): a combining tree in code order, WORD-ary to match the trie's fanout: leaf = combine of (code, value) hashes in index order, internal = combine of child hashes in index order.  Content-only ONLY BECAUSE shape is canonical (the erase fix above is load-bearing); tied to SYMBOL_WIDTH, which is fine for peers on one build
    - Alternative (Claude): commutative and associative per-entry combine (128-bit sum of mix(code, value)); node hash = sum of children.  Content-only by construction and structure-agnostic, and equal to the hash of the flat code-ordered save list, so the loader can verify a file by summing as it reads.  Collision resistance is adequate for a non-adversarial tripwire (a poisoned hash is a self-kick)
    - Value hashes must be content hashes, never pointers: Entity and HeapTerm get a virtual hash cached at freeze; Term OBJECT payloads hash by target content; PersistentStack cells cache a hash (O(1) per push); World = combine(map roots, time, entity id source, ready folded into the time wheel exactly as the save does) so live hash == save hash
- FIXED 2026-10-20 (uncommitted): the Release configuration did not build at HEAD -- command.hpp make_command's assert fold had no pack under NDEBUG (now `((void)p, assert(...))`), and kqueue_reactor.cpp's test-observability frame counter was declared only under !NDEBUG while three test drain loops use it unconditionally (now kept in every build: those loops are what hold a test open until its frame dies, and in Release the asserts around them compile out)
- FIXED 2026-10-20 (uncommitted), pre-existing, found by the World hash: World::step passed each rebuild its modifier as a `freeze(...)` TEMPORARY in the fork expression.  Nursery::fork runs the forked body on the current thread and schedules the caller's continuation on the pool, so step() continued on another thread, destroyed the temporary and reused its slot for the next freeze() while the rebuild's prologue (make_cursor) was still reading it.  Debug (-O0) kept each temporary in its own slot so the dead object was still intact and TSan saw no write; Release reused the slot: TSan-in-Release reported the write/read pair, the per-step hash diverged run to run, the starting world ended with 5 or 6 of its 7 entities, and the term map received an EntityID-shaped word as a Term (the hashing build then crashed dereferencing it as a HeapTerm).  Fix: the five frozen modifiers are locals of step()'s frame, alive until the join.  LESSON: never pass a temporary by reference into a forked coroutine; a Future's reference parameters must outlive the nursery join.  Worth a grep of every `fork(` for the same shape
- NOTE 2026-10-20: `xcodebuild -scheme client -configuration Release` builds WITH ThreadSanitizer and UBSan, because the scheme's Launch action has both enabled and xcodebuild honours that for any configuration (the TSan Release stepped the starting world at ~1.2 ms/step; the sanitizer-free one at ~20 us).  For profiling pass `ENABLE_THREAD_SANITIZER=NO ENABLE_ADDRESS_SANITIZER=NO` (UBSan's nullability check stays on from the project's CLANG_UNDEFINED_BEHAVIOR_SANITIZER_NULLABILITY) or use Xcode's Profile action
- FOLLOW-UP 2026-10-20: 12 more test asserts outside save_format.cpp wrap a side effect (`assert(x.try_get(...))`, try_pop_front, try_erase); NDEBUG drops them and the test then reads an unset out-param (UBSan reported one in the GUI's background Release test run).  Convert to CHECK(...) (ext/assert.hpp, always evaluates).  save_format.cpp and world_state_state_hash done
- Does AMT enforce no null values?
  - Do all desired value types have a null value?
  - Pros/cons?
- ThreadPublic should probably not be GC and be more like a Crossbeam list
- The `_ready` skiplist should probably have a per-object allocator
- Transactional non-exclusive commutative operations, like addition
- Strip the WRY_GC_DEBUG crash-trap tier once the UAF stays silent (condition in garbage_collected.cpp banner)
- Sweep the GUI with WRY-GC-UNPINNED watched, then promote the detector print to assert
- thread_public.cpp comments still say "static Root" / "drop the root" over the raw-pointer code
- Rewrite transaction.md's "Open design: deterministic EntityID assignment" (lazy-span/relative-refs scheme is obsolete) to the landed ready-set-rank + ticket design; also its "Waiting and scheduling" section still describes the old single-partition/copy-back flow
- DECIDED 2026-08-16: EntityIDs are permanent, never compacted at load (remap changes hash priorities => conflict resolution => forks the sim from its own save; also breaks peer replay and Term-embedded ids); ceiling is a design-around, not a per-session budget.  Ticket hoarding equilibrium (O(spawn-capable entities) reserved-unspent ids, burned at entity death) is therefore just accepted burn
- Worldgen pre-loads Spawner tickets: confirm every pre-loaded _free_entity_id is minted from the World cursor (generate_entity_id), never a literal, or it collides with the first dealt block
- ReadyKey requested/n and Entity _free_entity_id: the "late-written mutable field on a published-immutable object" contract lives in comments only; consider a doc paragraph naming both instances
- Coroutine / reactor / sender sessions (2026-08-29..09-11), still open, rough priority order:
- with_deadline is the last delegate-based adaptor, and it is race-shaped: a two-entrant race (inner, timer) under stopped_as_optional, except that it propagates the outer's own request instead of absorbing it -- rebuild it on RaceOperationState and give WithDeadlineSender real SenderTraits/connect (today it is a sender in name only).  2026-09-12: stopped_as_optional/shield are receiver adaptors, race is a sender algorithm over any senders, sync_wait takes senders (and lifts awaitables) and returns the Outcome, core/execution.hpp is gone
- Unwritten for want of a use: shield with a budget (shield(f, s.get_token()) + cancelable_after(s.get_token(), d, [s]{ s.request_stop(); })); a runtime-N race (the entrant pack is compile-time; a nursery covers runtime N)
- SenderAwaitable::await_suspend should return a handle: a receiver completed inside start records the outcome without resuming, and await_suspend transfers back to the outer symmetrically, so a loop over inline senders (Just, a race of Justs) does not grow the native stack; async completions nest one level, which unwinds at the next suspension.  Since the seam landed every sender boundary has been a nested resume; the 2026-09-12 peel extended that to the adaptors, which had used the Future transfer path
- Move the Senders section of coroutine.hpp (concepts, Just, Future as sender, SenderAwaitable, stopped_as_optional/shield, race, sync_wait) into ext/execution.hpp, by analogy with std::execution -- Antony 2026-09-12, when the seam settles; then and continues_on from the deleted toy layer are small to reintroduce on the real seam if a caller wants them
- get_stop_token walks the delegate chain to the root per leaf await -- cache in the promise if depth grows.  (The five Basic+Mixin delegate pairs collapsed onto OutcomeDelegate<Derived, T> 2026-09-11; MixinPromise is the remaining T/void pair, on the forwarding side, and AwaitableReceiver's set_value overloads are the receiver-side echo of it)
- Nursery interior request_stop is sticky across reuse (stop_source cannot reset): document single-join-after-request_stop or reconstruct at join
- Pending-arm accounting: an arm (cancelable_after) made from outside any wait-group task is untracked -- its knote dies silently at close(kq) and its frame leaks.  Every arm made inside a task is now cancelled by the root token (LANDED 2026-10-15; since 2026-10-18 the root is a Nursery local to main, `root`, reached through GuiContext::scope, and the wait group is gone; main requests root.request_stop() at the start of GUI/headless shutdown, not in --test-only; ControlSocket bridges the inherited token into its own source).  Open: the GUI's background test run is cut off by quit at its next reactor wait, silently -- fine for quit, but worth a log line; and sync_wait callers outside any task still see a detached token
- MultiProducerSingleConsumerQueue (SPSC is an alias; pop/take/try_pop/try_take, one awaiter): the control socket's per-connection OutQueue and its unified Event inbox (Line | Closed, 2026-10-16) are its users; the headless host idles on sync_wait(with_deadline(events().take())), with_deadline and sync_wait both lift any co_awaitable through lift_awaitable; tests mpsc_queue / mpsc_queue_cancel (in container/mpsc_queue.cpp since 2026-10-18) landed 2026-10-15 (ready, parked, move-only, parked cancel, cancel before park, delivered-before-stop).  (send_some, accept_one, connect_to landed 2026-10-15 on CancelableReadableAwaitable / CancelableWritableAwaitable; recv_some refactored onto the same; a failed recv now reports -errno as the header always said, not -1)
- SingleConsumerEvent (core/, 2026-10-18) replaced the drained MPSC queue in ControlSocket::shutdown; it is a sender in waiting (set_value on a receiver) when the seam wants it
- DONE 2026-10-21: ControlSocket accepted unbounded data per connection.  Now a Transport (io/transport.{hpp,cpp}, multiplayer stage 1) with the line codec: per connection at most 256 lines delivered and unconsumed (the reader parks; a Message holds a Credit until destroyed), at most 16 MiB of unsent replies (else kicked as a slow consumer), and at most 64 requests queued or pending (GuiContext::max_pipelined_requests, refused with an error reply); a kick stops reading, sends one farewell line, and closes after a grace.  FrameCodec (u32 length, u16 tag, per-tag caps checked before the body) and listen_tcp/connect_tcp are in place for the game server (stage 3)
- Control socket is on the reactor since 2026-10-15 (accept loop + per-connection reader/writer nurseries, SPSC queue for replies -- the queue's first user); since 2026-10-18 it runs under the root nursery's token (requested by main before the drain), so the hang this line once warned about is closed
- Coroutine design doc (ext/docs/coroutines.md): the delegate protocol (ask for the continuation before destroy; the stopped hook is the destroy word and must forward; no virtual dtor, parked delegates trivially destructible), cancellation as destruction and the pinned-requester contract, the last-touch rule, the fork-to-join rule, the reactor contract and the fire-won race, the request_stop keepalive rule, Nursery retire ordering, DebugWaitForCollectionCycles "cycles >= 3" -- today these live only in header comments and chat
- Exhaustive-interleaving test (model checker in a define_test) for CancelableKEventFrame; relacy or GenMC for the relaxed-RMW + acquire-garnish claims; a TSan pass is due -- the last clean one predates the delegation port
- set_delegate's Debug diagnostic names the colliding delegates; the one-time GUI firing (first run after pulling the port) is most likely a mixed incremental build -- if it recurs the message says which pair
- main.mm spawns the full test suite in the GUI unconditionally (4-5 s under ASan with the small world; QUIT waits for it, and must: the runner is shielded from the root token since 2026-10-17, because tests own nurseries across sleeps and a cancelled test frame asserts in ~Nursery -- the crash-on-quit of 2026-10-17): gate it behind a flag
- ext/functional.hpp: wry::move_only_function<R(Args...)> (subset: plain signature only, no in_place_type, empty call asserts) -- retire in favor of std::move_only_function when libc++ ships it
- Nits: soon(y, bar(x)) comment says "schedules foo"; test metadata prints interleave with collector logging (flaky counts in log greps, bit again 2026-09-11); co_await of an error Outcome (the rethrow path) has no direct test

- DONE 2026-10-21 (multiplayer stage 2): the step's input is explicit.  World::step(commands) stages JOIN/LEAVE on a copy of the World and advances with the actions on the TransactionContext; Player has no queue (its _queue was never saved, so a donated snapshot could diverge); World::_player_for_client (save version 6) routes a client's actions; format_action/parse_action are the action's wire form.  Open: a JOIN's Player first acts in the step after, so an action in the JOIN's own step is dropped (the server never produces one; LocalServer makes the JOIN its own step)
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
