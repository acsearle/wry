//
//  path_trace_bvh.hpp
//  client
//
//  The path tracer's per-mesh bounding volume hierarchy (docs/path_tracing.md
//  section 3): built once per mesh on the CPU, walked by
//  PathTrace.metal's mesh_intersect in the mesh's own space.
//

#ifndef path_trace_bvh_hpp
#define path_trace_bvh_hpp

#include <cstddef>
#include <cstdint>
#include <vector>

#include "PathTraceTypes.h"

namespace wry {

    // One mesh's hierarchy.  nodes[0] is the root (when there is at least
    // one triangle); an interior node's children are nodes[first] and
    // nodes[first + 1]; a leaf's triangles are triangles[first ..
    // first + count).  Each triangle keeps the index of the triangle it
    // was in the input, which the shader uses to fetch its vertices'
    // attributes.
    struct PathTraceBVH {
        std::vector<WryPathTraceNode> nodes;
        std::vector<WryPathTraceTriangle> triangles;
    };

    // Build over `count` triangles whose corners are positions[3 * k + c]
    // (xyz, w ignored).  Binned surface-area heuristic over centroids;
    // leaves of at most kMaxLeaf triangles, fewer where splitting does not
    // pay.  Deterministic: a pure function of the positions.
    PathTraceBVH build_path_trace_bvh(const simd_float4* positions, std::size_t count);

    // The hierarchy's depth (a lone root is 1, empty 0)
    int path_trace_bvh_depth(const PathTraceBVH& bvh);

    // The nearest hit of the ray origin + t * direction, t in (t_min,
    // t_max), against the hierarchy (two-sided, Moller-Trumbore, the same
    // arithmetic as the shader's); the input index of the hit triangle, or
    // -1.  For tests.
    std::int64_t path_trace_bvh_intersect(const PathTraceBVH& bvh,
                                          simd_float3 origin, simd_float3 direction,
                                          float t_min, float& t_max);

} // namespace wry

#endif /* path_trace_bvh_hpp */
