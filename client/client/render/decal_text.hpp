//
//  decal_text.hpp
//  client
//
//  Created by Antony Searle on 2026-11-08.
//

// Strings printed onto the world as image decals (WryDecalInstance,
// Shaders.metal deferred::decal_*).  A DecalFont is a face rasterized
// once, large, into its own straight-alpha texture: white everywhere,
// alpha the glyph's coverage, each glyph padded by kPad transparent
// texels so the mip chain (the binomial kernel, whole texture one cell)
// does not bleed one glyph into the next until the glyphs are a few
// texels tall.  layout_decal_text sets a run of it on a baseline;
// tile_caption_layout is the opcode tiles' caption rule, the same as
// tools/make_tile_atlas.py's Captioner (that tool lays the tiles out for
// the caption, the game prints it).

#ifndef decal_text_hpp
#define decal_text_hpp

#include <simd/simd.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace wry {

    struct DecalFont {

        struct Glyph {
            bool present = false;
            // The glyph's padded image in the texture: top-left uv, then
            // its uv extent; WryDecalInstance.uv_rect as is
            simd_float4 uv_rect = {};
            // The image's top-left from the pen on the baseline, and its
            // size, in ems (y up)
            simd_float2 offset = {};
            simd_float2 size = {};
            float advance = 0.0f;   // ems
        };

        // Printable ASCII; the rest are absent (laid out as nothing)
        std::array<Glyph, 128> glyphs;
        float cap_height = 0.7f;   // ems, measured from 'H'

        // The rasterization: texels to the em, and the texture's size
        // (width, height), which the caller allocates and fills from
        // `rgba` (RGBA8 sRGB, straight alpha, row-major, top row first)
        float em_texels = 0.0f;
        int width = 0;
        int height = 0;
        std::vector<unsigned char> rgba;

        static constexpr int kPad = 16;   // transparent texels around each glyph

        const Glyph* find(char ch) const {
            const unsigned char c = (unsigned char)ch;
            return (c < glyphs.size() && glyphs[c].present) ? &glyphs[c] : nullptr;
        }
    };

    // Rasterize the printable ASCII of the font file (a path the assets
    // folder resolves, as bytes_from_file does) at em_texels to the em
    // with the GUI font's analytic rasterizer (font.mm), packed in rows
    // into a power-of-two texture.  Defined in font.mm.
    DecalFont build_decal_font(const char* path, float em_texels);

    enum class DecalAlign { left, centre };

    // One glyph of a laid-out run: its padded image's centre and size in
    // the run's units (those of cap_height; x along the baseline from the
    // anchor, y up from the baseline) and where it is in the texture
    struct DecalGlyphPlacement {
        simd_float2 centre;
        simd_float2 size;
        simd_float4 uv_rect;
    };

    struct DecalTextRun {
        std::vector<DecalGlyphPlacement> glyphs;
        float width = 0.0f;   // the advances' sum, after the shrink
        float scale = 1.0f;   // the shrink applied to fit max_width (1: none)
    };

    // The advances' sum of `text` at one em (no kerning: otf::Handle
    // reads no kern / GPOS)
    float decal_text_advance(const DecalFont&, std::string_view text);

    // Set `text` on a baseline with capitals cap_height tall, shrunk
    // uniformly (about the anchor) if its advances are wider than
    // max_width (<= 0: never), its left end or its centre at the anchor.
    // Characters the font lacks take no room.
    DecalTextRun layout_decal_text(const DecalFont&, std::string_view text,
                                   float cap_height, float max_width,
                                   DecalAlign align);

    // An opcode tile's caption on its south edge, in the glyph frame's
    // units (24 to the frame, y down from the frame's top; the frame is
    // one world unit): one line at cap 2.3 on the baseline 21.4, shrunk to
    // 19 wide, or, if that would shrink it below 3/4 and the name has a
    // space, two lines broken at the space nearest the middle at cap 1.7
    // on the baselines 19.4 and 21.5.  `name` is the display name
    // (underscores already spaces).
    struct TileCaption {
        std::vector<std::string> lines;
        std::vector<float> baselines;
        float cap = 0.0f;   // before any shrink to the width (layout_decal_text does that)
        static constexpr float kWidth = 19.0f;
        static constexpr float kFrame = 24.0f;
    };
    TileCaption tile_caption_layout(const DecalFont&, std::string_view name);

} // namespace wry

#endif /* decal_text_hpp */
