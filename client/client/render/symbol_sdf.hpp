//
//  symbol_sdf.hpp
//  client
//
//  Created by Antony Searle on 2026-10-29.
//
//  The signed distance field beside the symbol atlas (assets.png): one
//  texture of the same cell layout (kCells x kCells cells), each texel holding the
//  signed distance to the silhouette of the symbol in its cell and the
//  unit gradient of that distance, so a shader can thicken the flat symbol
//  into a filleted slab (Shaders.metal, deferred::symbol_fragment_function)
//  and shade it with smooth normals.
//
//  The silhouette is the atlas's alpha channel at the 0.5 contour, for
//  every cell alike; multi-colour symbols thicken by their outermost edge.
//  Distances are measured in source (atlas) texels, so the cell is
//  always 256 texels across whatever resolution the field is stored at;
//  the glyph's own frame is the central half of its cell and the margin
//  around it is empty by construction (tools/make_assets.py), so the
//  distances stay valid for several stroke widths beyond every glyph;
//  the field is built from an exact Euclidean distance transform of the
//  full-resolution silhouette, then box-downsampled, so a half-resolution
//  field carries sub-texel edge placement.
//
//  Texel encoding (RGBA8Unorm, no sRGB):
//      r  = distance / (2 * kRange) + 0.5, distance clamped to +-kRange
//           source texels: an eighth of a texel, for the slab's walls (a
//           clamped field is still a lower bound, which is all a ray
//           march needs)
//      gb = gradient * 0.5 + 0.5, the unit outward gradient in image
//           axes (x right, y down); zero where the distance is clamped or
//           undefined, which decodes as (0, 0)
//      a  = distance / (2 * kFarRange) + 0.5, the same distance clamped
//           to +-kFarRange: a texel's precision over the whole cell, for
//           the slab's ambient-occlusion decal, whose tail runs several
//           stroke widths out
//
//  Per cell, the stroke width -- twice the deepest interior distance the
//  field holds, the width of the widest stroke -- sizes the slab: the
//  renderer makes a symbol as thick as its stroke, with top and bottom
//  fillets of a quarter stroke, so every stroke keeps a flat band between
//  its fillets.  It is derived from the field (stroke_widths), so a cached
//  field needs nothing beside the texels; the box filter shaves up to
//  about a texel off a thin stroke's true width.
//
//  Building takes seconds in a Debug build, so the field is cached as a
//  PNG beside the atlas (symbol_sdf_for_atlas), rebuilt when the atlas is
//  newer.

#ifndef symbol_sdf_hpp
#define symbol_sdf_hpp

#include <cstdint>
#include <filesystem>
#include <vector>

#include "image.hpp"
#include "matrix.hpp"

namespace wry {

    struct alignas(4) SymbolSDFTexel {
        std::uint8_t distance;
        std::uint8_t gradient_x;
        std::uint8_t gradient_y;
        std::uint8_t far_distance;
    };

    struct SymbolSDF {
        // The atlas is kCells x kCells cells (tools/make_assets.py GRID)
        static constexpr int kCells = 16;
        // Source texels represented by the distance channels; must agree
        // with WRY_SYMBOL_SDF_RANGE / WRY_SYMBOL_SDF_FAR_RANGE in
        // ShaderTypes.h
        static constexpr float kRange = 16.0f;
        static constexpr float kFarRange = 128.0f;

        int source_cell = 0;            // source texels per cell (256)
        int cell = 0;                   // field texels per cell
        matrix<SymbolSDFTexel> image;   // kCells * cell square
        // Per cell, row-major (cell y, cell x): the stroke width in source
        // texels, 0 for a cell with no silhouette (stroke_widths)
        std::vector<float> stroke_width;

        float stroke_width_at(int cell_x, int cell_y) const {
            return stroke_width[(std::size_t)cell_y * kCells + (std::size_t)cell_x];
        }
    };

    // Per cell, twice the deepest interior distance in the field (the
    // fine channel, or the coarse one where the fine is clamped): what
    // sizes the slab
    std::vector<float> stroke_widths(matrix_view<const SymbolSDFTexel> image);

    // Build the field from the atlas (a kCells * source_cell square), at
    // `cell` field texels per cell; source_cell must be a multiple of cell.
    SymbolSDF build_symbol_sdf(matrix_view<const RGBA8Unorm_sRGB> atlas, int cell);

    // The field for the atlas PNG at `atlas_path`, at half its resolution:
    // from the cache PNG beside it when that is newer than the atlas,
    // else built and cached.
    SymbolSDF symbol_sdf_for_atlas(std::filesystem::path const& atlas_path);

    // The signed distance of a texel's centre from the edge of a half-plane
    // covering fraction `a` of it, the edge's unit normal being (gx, gy)
    // in any signs: the inverse of the covered area of a unit square cut
    // by a line, which is linear in the line's offset while the chord
    // runs wall to wall and quadratic once it cuts a corner (Gustavson's
    // edtaa3 edgedf).  Positive when the centre is outside (a < 0.5).
    float edge_distance(float a, float gx, float gy);

    // The signed distance of each texel centre to the 0.5 contour of
    // `coverage`, in texels, negative inside: the contour's crossing
    // points, placed within the boundary texels by their coverage, swept
    // out to every texel.  All kSymbolSDFFar if the image is empty.
    constexpr float kSymbolSDFFar = 1e6f;
    matrix<float> signed_distance(matrix_view<const float> coverage);

} // namespace wry

#endif /* symbol_sdf_hpp */
