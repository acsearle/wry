//
//  PathTraceTypes.h
//  client
//
//  The path-traced reference's GPU structs and binding indices, shared by
//  C++ (WryPathTracer.mm, path_trace_bvh.cpp) and Metal (PathTrace.metal).
//  docs/path_tracing.md.  Everything is in render space, like the raster
//  passes: world units, z up, the focus tile at the origin.
//

#ifndef PathTraceTypes_h
#define PathTraceTypes_h

#include <simd/simd.h>

// WryPathTraceUniforms::view (the `path-trace` command); 0 is off and
// never reaches the GPU
#define WRY_PT_VIEW_OFF 0
#define WRY_PT_VIEW_BEAUTY 1        // the whole estimate
#define WRY_PT_VIEW_DIRECT 2        // emission and the sun at the first hit: the fast renderer's Color
#define WRY_PT_VIEW_AMBIENT 3       // everything but `direct`
#define WRY_PT_VIEW_SKY 4           // the sky the first bounce reaches: the fast renderer's Ambient x AO
#define WRY_PT_VIEW_SKY_OPEN 5      // the same with nothing in the way: its Ambient with AO 1
#define WRY_PT_VIEW_INDIRECT 6      // everything but `direct` and `sky`: the interreflection it lacks
#define WRY_PT_VIEW_OCCLUSION 7     // the cosine-weighted share of the sky the first hit sees (ssao gtao-show)
#define WRY_PT_VIEW_ALBEDO 8        // the first hit's albedo, decals applied
#define WRY_PT_VIEW_NORMAL 9        // the first hit's shading normal, n * 0.5 + 0.5
#define WRY_PT_VIEW_ERROR 10        // the beauty's standard error per channel (resolve only)

// Meshes the tracer can shade (WryWorldScene has eight)
#define WRY_PT_MAX_MESHES 8

// A grid cell's item: a symbol slab's index, or with this bit a mesh
// instance's; with WRY_PT_ITEM_OFFSCREEN the item lies beyond what the
// raster passes would have drawn (it is in the margin), so the primary
// rays pass it and only light and shadow see it
#define WRY_PT_ITEM_MESH 0x80000000u
#define WRY_PT_ITEM_OFFSCREEN 0x40000000u
#define WRY_PT_ITEM_INDEX 0x3FFFFFFFu

enum WryPathTraceBufferIndex {
    WryPathTraceBufferUniforms,
    WryPathTraceBufferBoard,            // WryTerrainUniforms
    WryPathTraceBufferKinds,            // uchar per board cell
    WryPathTraceBufferSymbols,          // WrySymbolInstance
    WryPathTraceBufferDecals,           // WryDecalInstance: the prints, then the strings
    WryPathTraceBufferMeshInstances,    // WryPathTraceMeshInstance
    WryPathTraceBufferMeshes,           // WryPathTraceMesh
    WryPathTraceBufferNodes,            // WryPathTraceNode, every mesh's
    WryPathTraceBufferTriangles,        // WryPathTraceTriangle, every mesh's
    WryPathTraceBufferVertices,         // MeshVertex, every mesh's
    WryPathTraceBufferCellStart,        // uint per grid cell, and one more: its items' range
    WryPathTraceBufferCellItems,        // uint per item (WRY_PT_ITEM_*)
    WryPathTraceBufferCellZ,            // float2 per grid cell: its items' z range
    WryPathTraceBufferDecalStart,       // uint per grid cell, and one more
    WryPathTraceBufferDecalItems,       // uint: decal index, ascending per cell
    WryPathTraceBufferAccumulator,      // float4 x 2 per pixel: (sum, count), (sum of squares, dropped)
};

enum WryPathTraceTextureIndex {
    WryPathTraceTextureField,           // the symbol field
    WryPathTraceTextureSymbols,         // the symbol atlas
    WryPathTraceTextureGlyphs,          // the print decals' image
    WryPathTraceTextureDecalFont,       // the string decals' image
    WryPathTraceTextureEnvironment,     // the equirectangular sky
    // then WRY_PT_MAX_MESHES each of the meshes' maps
    WryPathTraceTextureMeshAlbedo,
    WryPathTraceTextureMeshNormal = WryPathTraceTextureMeshAlbedo + WRY_PT_MAX_MESHES,
    WryPathTraceTextureMeshRoughness = WryPathTraceTextureMeshNormal + WRY_PT_MAX_MESHES,
    WryPathTraceTextureMeshMetallic = WryPathTraceTextureMeshRoughness + WRY_PT_MAX_MESHES,
    WryPathTraceTextureMeshEmissive = WryPathTraceTextureMeshMetallic + WRY_PT_MAX_MESHES,
    WryPathTraceTextureMeshOcclusion = WryPathTraceTextureMeshEmissive + WRY_PT_MAX_MESHES,
};

typedef struct {
    // The camera and its ray basis, as MeshUniforms has them: the ray
    // through NDC (x, y) is centre + x dx + y dy, with unit component
    // along the view axis
    simd_float4 camera_position_world;
    simd_float4 camera_ray_centre;
    simd_float4 camera_ray_dx;
    simd_float4 camera_ray_dy;
    simd_float4 light_direction;        // xyz: unit, toward the sun
    simd_float4 sun_irradiance;         // rgb: at normal incidence, times exposure
    simd_float4 environment_scale;      // rgb: the sky's texels to radiance (the exposure)
    simd_int2 viewport;                 // pixels
    simd_int2 grid_origin;              // the objects' grid: its first cell (render space)
    simd_int2 grid_size;                // cells
    float grid_z_lo;                    // the grid's items lie within these heights
    float grid_z_hi;
    float near_depth;                   // the raster's clip planes, as view depth
    float far_depth;
    unsigned int row_begin;             // this dispatch's first pixel row
    unsigned int row_stride;            // and every row_stride-th row after it
    unsigned int samples;               // to add to each pixel this dispatch
    unsigned int bounces;               // indirect bounces at most
    unsigned int view;                  // WRY_PT_VIEW_*
    unsigned int antialias;             // 0: pixel centres, filtered as the raster; 1: jittered, unfiltered
    unsigned int print_count;           // decals below this index read the glyphs, the rest the font
    unsigned int _padding[3];
} WryPathTraceUniforms;

typedef struct {
    matrix_float4x4 model_transform;                    // object to render space
    matrix_float4x4 inverse_model_transform;
    matrix_float4x4 inverse_transpose_model_transform;  // as MeshInstanced has it (the raster transforms T, B, N by it)
    simd_float4 tint;                                   // MeshInstanced::albedo
    unsigned int mesh;                                  // WryPathTraceMesh index, and its maps' slot
    unsigned int _padding[3];
} WryPathTraceMeshInstance;

typedef struct {
    unsigned int node_offset;           // its root in the nodes buffer
    unsigned int triangle_offset;       // its first triangle
    unsigned int vertex_offset;         // its first vertex: triangle k is vertices 3k, 3k + 1, 3k + 2 after it
    unsigned int node_count;            // 0: no triangles
} WryPathTraceMesh;

// A BVH node (render/path_trace_bvh.hpp): count 0 is interior, with
// children first and first + 1 (relative to the mesh's node_offset);
// else a leaf of triangles first .. first + count (relative to its
// triangle_offset)
typedef struct {
    float lo[3];
    unsigned int first;
    float hi[3];
    unsigned int count;
} WryPathTraceNode;

// A triangle's corners in object space, and its index among the mesh's
// input triangles (its vertices' attributes)
typedef struct {
    float p0[3];
    float p1[3];
    float p2[3];
    unsigned int index;
} WryPathTraceTriangle;

// The resolve (PathTrace.metal resolve_fragment): the accumulator to the
// frame's colour
typedef struct {
    simd_int2 viewport;
    unsigned int view;
    unsigned int row_stride;            // a pixel with no samples yet shows the nearest above within this
} WryPathTraceResolveUniforms;

#endif /* PathTraceTypes_h */
