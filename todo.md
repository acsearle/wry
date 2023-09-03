# Todo

Bite-sized
- wrap up perspective frustms better
- wrap up depth handling / reconstruction better
  - can we do an orthographic projection with 1/z depth somehow?
- uniforms seem to be out of scope of meshes after all
  - can we set them up once and for all
- shadow z-fighting apply depth bias and frontface

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
