//
//  font.mm
//  client
//
//  Created by Antony Searle on 25/6/2023.
//

// .mm - Infected by SpriteAtlas?

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <algorithm>
#include <iostream>

#include "assert.hpp"
#include "debug.hpp"
#include "decal_text.hpp"
#include "font.hpp"
#include "image.hpp"
#include "utility.hpp"
#include "string.hpp"
#include "platform.hpp"
#include "filesystem.hpp"
#include "bezier.hpp"
#include "otf.hpp"
#include "ShaderTypes.h"


namespace wry {
    
    // Glyphs are stored as premultiplied white coverage, padded with
    // transparent texels so quadFragmentShader can draw a dark outline
    // around them (WryQuadFlagOutlined): it dilates the coverage by taking
    // the max over a disc of taps of radius WRY_GLYPH_OUTLINE_RADIUS
    // texels.  The image is padded by two bands of kGlyphPad texels:
    //
    // - the inner band lies inside the sprite's quad, room for the
    //   outline to spill past the coverage bitmap (whose own 3 px Gaussian
    //   margin is measured from the control-point bbox, not the ink);
    // - the outer band lies outside the quad, so the taps of a fragment at
    //   the quad's edge read this glyph's own transparent texels rather
    //   than a neighbor's in the atlas.
    constexpr int kGlyphPad = 3;  // texels, >= WRY_GLYPH_OUTLINE_RADIUS
    static_assert(kGlyphPad >= WRY_GLYPH_OUTLINE_RADIUS + 0.5f);

    // The coverage bitmap as premultiplied white, with 2 * kGlyphPad
    // transparent texels on every side.  Tinting by a vertex color colors
    // the glyph.
    matrix<RGBA8Unorm_sRGB> rgba_padded(matrix_view<R8Unorm> x) {
        const size_t h = x.minor();
        const size_t w = x.major();
        const size_t P = 2 * kGlyphPad;
        const size_t H = h + 2 * P;
        const size_t W = w + 2 * P;
        matrix<RGBA8Unorm_sRGB> c(H, W);
        for (size_t i = 0; i != H; ++i)
            for (size_t j = 0; j != W; ++j) {
                R8Unorm g(0.0f);
                if (P <= i && i < P + h && P <= j && j < P + w)
                    g = x[i - P, j - P];
                auto& p = c[i, j];
                p.a = (float)g;
                // Color is premultiplied white, g, converted from linear
                // to sRGB by the table.
                const uchar d = _multiply_alpha_table[g._][255];
                p.r._ = d;
                p.g._ = d;
                p.b._ = d;
            }
        return c;
    }

    // Glyph outlines rasterize through the shared analytic integrator
    // (bezier.hpp: rasterize_coverage, the CPU twin of
    // Shaders.metal::otf::bezierFragmentFunction), which the SVG loader
    // also uses.  Its bitmap offsets match FreeType's bitmap_left /
    // bitmap_top conventions, so rgba_padded and atlas placement are as
    // they were.

    Font build_font(SpriteAtlas& atl, float pixels_per_point) {

        pixels_per_point = std::clamp(pixels_per_point, 0.25f,
                                      Font::max_pixels_per_point);
        atl.reset_font_region();

        Font result;
        result.pixels_per_point = pixels_per_point;
        result.stem_width = 1.0f; // fallback if the font lacks 'I'
        result.cap_height = result.ascender; // fallback if it lacks 'H'

        // Load the font file and parse with our bespoke OTF/CFF stack.
        // OTF is a binary container, so go through bytes_from_file rather
        // than the UTF-8-validating string_from_file.
        ContiguousDeque<byte> bytes = bytes_from_file("Noto_Sans/static/NotoSans-Medium.ttf");
        auto handle = otf::Handle::parse({
            bytes.data(),
            bytes.size()
        });

        int upem = handle.units_per_em();
        // Sized to keep the mean advance over printable ASCII of the font
        // it replaced, Futura Medium Condensed at a 40 px em: 0.3736 em vs
        // Noto Sans Light's 0.5171 em, so 40 * 0.3736 / 0.5171 = 28.9 px,
        // at the 2 px/pt it was tuned at: Font::em_points.  Rasterized at
        // the UI's scale rather than scaled as a bitmap.
        const float target_em_pixels = Font::em_points * pixels_per_point;
        float pixel_size_du = (float)upem / target_em_pixels;

        auto m = handle.metrics_for_face();
        result.ascender  = (float)m.ascender  / pixel_size_du;
        result.descender = (float)m.descender / pixel_size_du;
        result.height    = (float)(m.ascender - m.descender + m.line_gap)
                           / pixel_size_du;

        // Printable ASCII; same scope as build_font2.  Anything else would
        // require cmap enumeration on otf::Handle.
        for (int charcode = 32; charcode != 127; ++charcode) {
            int glyph_index = handle.glyph_index_for_character(charcode);
            if (glyph_index == 0)
                continue; // character not in font

            auto curves = handle.outline_for_glyph_index(glyph_index);
            auto gb = rasterize_coverage(curves, pixel_size_du);
            matrix<RGBA8Unorm_sRGB> u = rgba_padded(gb.coverage);

            // Stem width: integrate coverage along the middle row of 'I'.
            // The Gaussian prefilter preserves the integral, so this is
            // the stem's width in pixels regardless of subpixel position.
            // Cap height: the top of 'H' is flat, so the highest control
            // point is on the outline.
            if (charcode == 'H') {
                float top = -INFINITY;
                for (auto const& curve : curves)
                    for (int i = 0; i != 4; ++i)
                        top = std::max(top, curve.columns[i].y);
                result.cap_height = top / pixel_size_du;
            }

            if (charcode == 'I') {
                const auto& c = gb.coverage;
                const size_t row = c.minor() / 2;
                float w = 0.0f;
                for (size_t col = 0; col != c.major(); ++col)
                    w += c[row, col];
                result.stem_width = w;
            }

            // The pen origin within u: rgba_padded put the coverage bitmap
            // at (2 * kGlyphPad, 2 * kGlyphPad).  The sprite is the image
            // inset by the outer band (see kGlyphPad).
            Sprite s = atl.place_font(u, make<float2>(2 * kGlyphPad - gb.left,
                                                       2 * kGlyphPad + gb.top));
            {
                const float2 inset = make<float2>(kGlyphPad, kGlyphPad);
                const float2 duv = (s.b.texCoord - s.a.texCoord)
                    / make<float2>(u.major(), u.minor());
                s.a.position.xy += inset;
                s.b.position.xy -= inset;
                s.a.texCoord += inset * duv;
                s.b.texCoord -= inset * duv;
            }

            float advance = handle.advance_for_glyph_index(glyph_index)
                            / pixel_size_du;
            result.charmap.insert(std::make_pair((uint)charcode,
                                                 Font::Glyph{s, advance}));
        }

        return result;
    }

    // The decal face (decal_text.hpp): the same rasterizer, at a size
    // fixed for the world rather than the UI, into a texture of its own
    // as straight-alpha white (the decal shader tints the image's rgb
    // and blends by its alpha; the symbol glyphs it also prints are
    // straight too).  Glyphs are packed left to right in rows (shelves)
    // of a 1024-wide image whose height is rounded up to a power of two
    // (the mip kernel wants one), each with DecalFont::kPad transparent
    // texels on every side.
    DecalFont build_decal_font(const char* path, float em_texels) {
        DecalFont result;
        result.em_texels = em_texels;
        ContiguousDeque<byte> bytes = bytes_from_file(path);
        auto handle = otf::Handle::parse({bytes.data(), bytes.size()});
        const float upem = (float)handle.units_per_em();
        const float pixel_size_du = upem / em_texels;

        struct Placed {
            int charcode;
            CoverageBitmap bitmap;
            int x, y;   // the padded image's top-left in the texture
        };
        std::vector<Placed> placed;
        constexpr int kWidth = 1024;
        const int P = DecalFont::kPad;
        int x = 0, y = 0, shelf = 0;
        for (int charcode = 32; charcode != 127; ++charcode) {
            const int glyph_index = handle.glyph_index_for_character(charcode);
            if (glyph_index == 0)
                continue;
            auto curves = handle.outline_for_glyph_index(glyph_index);
            if (charcode == 'H') {
                float top = -INFINITY;
                for (auto const& curve : curves)
                    for (int i = 0; i != 4; ++i)
                        top = std::max(top, curve.columns[i].y);
                result.cap_height = top / upem;
            }
            DecalFont::Glyph& g = result.glyphs[(size_t)charcode];
            g.present = true;
            g.advance = handle.advance_for_glyph_index(glyph_index) / upem;
            CoverageBitmap gb = rasterize_coverage(curves, pixel_size_du);
            const int w = (int)gb.coverage.major();
            const int h = (int)gb.coverage.minor();
            if (w == 0 || h == 0)
                continue;   // the space: an advance only
            const int W = w + 2 * P, H = h + 2 * P;
            if (x + W > kWidth) {
                x = 0;
                y += shelf;
                shelf = 0;
            }
            placed.push_back(Placed{charcode, std::move(gb), x, y});
            x += W;
            shelf = std::max(shelf, H);
        }
        int height = 1;
        while (height < y + shelf)
            height *= 2;
        result.width = kWidth;
        result.height = height;
        // White everywhere, so mips and bilinear taps of the edge never
        // pull in a dark rgb; alpha zero outside the glyphs
        result.rgba.assign((size_t)kWidth * (size_t)height * 4, 255);
        for (size_t i = 3; i < result.rgba.size(); i += 4)
            result.rgba[i] = 0;
        for (const Placed& p : placed) {
            const matrix<R8Unorm>& c = p.bitmap.coverage;
            const int w = (int)c.major(), h = (int)c.minor();
            for (int row = 0; row != h; ++row)
                for (int col = 0; col != w; ++col)
                    result.rgba[((size_t)(p.y + P + row) * kWidth + (size_t)(p.x + P + col)) * 4 + 3] = c[row, col]._;
            DecalFont::Glyph& g = result.glyphs[(size_t)p.charcode];
            const int W = w + 2 * P, H = h + 2 * P;
            g.uv_rect = simd_make_float4((float)p.x / kWidth, (float)p.y / (float)height,
                                         (float)W / kWidth, (float)H / (float)height);
            // The padded image's top-left from the pen (texels, y up):
            // the bitmap's, less the padding
            g.offset = simd_make_float2((float)(p.bitmap.left - P) / em_texels,
                                        (float)(p.bitmap.top + P) / em_texels);
            g.size = simd_make_float2((float)W / em_texels, (float)H / em_texels);
        }
        return result;
    }



    Font2 build_font2() {

        Font2 result;

        // Load the font file and parse with our bespoke OTF/CFF stack.
        // TTF/OTF is binary; go through bytes_from_file.
        // ContiguousDeque<byte> bytes = bytes_from_file("Futura Medium Condensed.otf");
        ContiguousDeque<byte> bytes = bytes_from_file("OpenSans_Condensed-Light.ttf");
        // ContiguousDeque<byte> bytes = bytes_from_file("OpenSans-VariableFont_wdth,wght.ttf");
        auto handle = otf::Handle::parse({
            bytes.data(),
            bytes.size()
        });

        // All design-unit values are normalized by 1 / unitsPerEm.
        float upem_scale = 1.0f / (float)handle.units_per_em();

        auto m = handle.metrics_for_face();
        result.ascender  = (float)m.ascender  * upem_scale;
        result.descender = (float)m.descender * upem_scale;
        result.height    = (float)(m.ascender - m.descender + m.line_gap) * upem_scale;

        auto& gi = result.glyph_data;
        auto& cp = result.cubic_bezier;

        // Reserve glyph 0 (.notdef) so any later lookup by glyph_index is in-bounds.
        gi.push_back(::otf::GlyphData{{0,0}, {0,0}, 0, 0});

        // For the smoke test ("Sphinx of black quartz, ...") printable ASCII is
        // enough.  Broader coverage would require exposing cmap enumeration in
        // otf::Handle.
        for (int charcode = 32; charcode != 127; ++charcode) {
            int glyph_index = handle.glyph_index_for_character(charcode);
            if (glyph_index == 0)
                continue; // character not in font

            // Outline curves come back in font design units.  Scale to per-em
            // normalized coordinates and compute the control-point bbox.
            auto curves = handle.outline_for_glyph_index(glyph_index);

            unsigned int cp_a = (unsigned int)cp.size();

            float2 lo = float2{ INFINITY,  INFINITY};
            float2 hi = float2{-INFINITY, -INFINITY};
            for (auto const& curve : curves) {
                float2 c0 = curve.columns[0] * upem_scale;
                float2 c1 = curve.columns[1] * upem_scale;
                float2 c2 = curve.columns[2] * upem_scale;
                float2 c3 = curve.columns[3] * upem_scale;
                lo = simd_min(lo, simd_min(simd_min(c0, c1), simd_min(c2, c3)));
                hi = simd_max(hi, simd_max(simd_max(c0, c1), simd_max(c2, c3)));
                cp.push_back(::otf::CubicBezier{c0, c1, c2, c3});
            }

            unsigned int cp_b = (unsigned int)cp.size();

            if (cp_a == cp_b) {
                // Empty glyph (e.g. space): collapse bbox to origin.
                lo = float2{0, 0};
                hi = float2{0, 0};
            }

            if (gi.size() < (size_t)(glyph_index + 1))
                gi.resize((size_t)(glyph_index + 1));
            gi[glyph_index] = ::otf::GlyphData{lo, hi, cp_a, cp_b};

            float advance = handle.advance_for_glyph_index(glyph_index) * upem_scale;
            result.charmap.insert(std::make_pair(
                (uint)charcode,
                Font2::Glyph{(uint)glyph_index, advance}
            ));
        }

        return result;
    }
    
    
    
} // namespace wry



namespace wry {
    
    // source of glyph coverage maps or signed distance fields
    
    struct Font3 {
        
        FT_Library library;
        FT_Face face;
                
        Font3() {
            
            FT_Error e;
            
            {
                FT_Init_FreeType(&library);
                e = FT_Init_FreeType(&library);
                assert(!e);
            }
            
            {
                const char* filepathname
                = "Futura Medium Condensed.otf";
                FT_Long face_index = 0;
                e = FT_New_Face(library,
                                filepathname,
                                face_index,
                                &face);
                assert(!e);
            }
            
            {
                FT_UInt pixel_width = 24;
                FT_UInt pixel_height = 0;
                FT_Set_Pixel_Sizes(face, pixel_width, pixel_height);
            }
            
        }
        
        ~Font3() {
            FT_Done_FreeType(library);
        }
        
        std::tuple<float2,
        matrix_view<R8Unorm>,
        float2> operator[](char32_t charcode) const {
            
            FT_UInt glyph_index = FT_Get_Char_Index(face, charcode);
            assert(glyph_index);
            
            FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER);
            
            auto& glyph = *face->glyph;

            float2 origin = make<float2>(-glyph.bitmap_left, glyph.bitmap_top);
            
            auto& bitmap = glyph.bitmap;
            R8Unorm* pointer = reinterpret_cast<R8Unorm*>(bitmap.buffer);
            stride_iterator row_iterator(pointer, bitmap.pitch);
            matrix_view<R8Unorm> view(row_iterator, bitmap.rows, bitmap.width);

            float advance = glyph.advance.x / 64.0f;
            float height = face->height / 64.0f;

            return { origin, view, make<float2>(advance, height) };
            
        }
        
    }; // struct Font2
    
    std::tuple<float2, matrix_view<R8Unorm>, float2> get_glyph(char32_t charcode) {
        static auto* f = new Font3();
        
        return (*f)[charcode];
        
    }

} // namespace wry


