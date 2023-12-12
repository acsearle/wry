# Todo

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
