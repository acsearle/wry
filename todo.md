# Todo

Bite-sized
- wrap up perspective frustms better
- wrap up depth handling / reconstruction better
  - can we do an orthographic projection with 1/z depth somehow?
- uniforms seem to be out of scope of meshes after all
  - can we set them up once and for all

Serialization
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

Computer science
- How do we receive OS events and hand off a consistent view to the render
  thread?  Make our own message queue?  Same problem with model thread.
- Do we even need the DisplayLink timer or can we just block the render thread
  on the drawable?  Does this increase latency too much?
  

System
- Police decoupling of platform-specific features
- Contraverse, leverage platform for ModelIO etc.?

Renderer
- Convolution post-processing - bloom, scatter, flare, bokeh
- Object-mesh pipeline for generating debug normals
- Instanced small lights
- Cloud/smoke and partial-shadow-map
- Parallax mapping
- HDR render target
- Multisampling
- ClearCoat, iridescence
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
