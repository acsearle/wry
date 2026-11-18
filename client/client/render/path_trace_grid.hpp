//
//  path_trace_grid.hpp
//  client
//
//  The path tracer's acceleration grid (docs/path_tracing.md section 3): a
//  2D uniform grid of world cells, render space (cell (i, j) spans
//  i +- 0.5, j +- 0.5), each cell listing the items whose bounding boxes
//  overlap it, as compressed rows.  Nearly everything in the world view
//  sits in one cell, so a ray walking the cells (a 2D DDA) tests a couple
//  of items per cell.
//

#ifndef path_trace_grid_hpp
#define path_trace_grid_hpp

#include <cstddef>
#include <cstdint>
#include <vector>

#include <simd/simd.h>

namespace wry {

    struct PathTraceGrid {
        simd_int2 origin = {};     // the first cell
        simd_int2 size = {};       // cells; at least 1 x 1
        // Cell c = j * size.x + i (i, j from the origin) lists
        // items[start[c] .. start[c + 1]), in the order the items were
        // added (so ascending for one kind of item added in order)
        std::vector<std::uint32_t> start;
        std::vector<std::uint32_t> items;
        // The union of the z extents of the cell's items (lo > hi: none)
        std::vector<simd_float2> z;
        // Of every item that overlaps the grid
        float z_lo = 0.0f;
        float z_hi = 0.0f;
    };

    // Collects items with their render-space boxes, then packs them
    class PathTraceGridBuilder {
    public:
        PathTraceGridBuilder(simd_int2 origin, simd_int2 size);
        // An item with this box; ignored if it misses the grid in x or y
        void add(std::uint32_t item, simd_float3 lo, simd_float3 hi);
        PathTraceGrid build() const;
    private:
        struct Entry {
            std::uint32_t item;
            simd_int2 a, b;      // cells, inclusive, from the origin
            float z0, z1;
        };
        simd_int2 _origin, _size;
        std::vector<Entry> _entries;
    };

    // The render-space box of the box [lo, hi] (some space) through the
    // affine `transform` (columns; that space to render space)
    void transform_box(const simd_float4x4& transform, simd_float3 lo, simd_float3 hi,
                       simd_float3& out_lo, simd_float3& out_hi);

    // A 64-bit hash of bytes (FNV-1a over 8-byte words): the accumulation's
    // reset key folds every input of the frame through it
    std::uint64_t path_trace_hash(std::uint64_t h, const void* data, std::size_t length);

} // namespace wry

#endif /* path_trace_grid_hpp */
