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

#include <iostream>

#include "assert.hpp"
#include "debug.hpp"
#include "font.hpp"
#include "image.hpp"
#include "utility.hpp"
#include "string.hpp"
#include "platform.hpp"
#include "filesystem.hpp"
#include "otf.hpp"
#include "ShaderTypes.h"


namespace wry {
    
    matrix<RGBA8Unorm_sRGB> rgba_from_a(matrix_view<R8Unorm> x) {

        matrix<RGBA8Unorm_sRGB> c(x.minor(), x.major());
        for (size_t i = 0; i != c.minor(); ++i) {
            for (size_t j = 0; j != c.major(); ++j) {
                c[i, j].a = x[i, j];
            }
        }
        
        // Color is alpha to linear color to sRGB
        for (size_t i = 0; i != c.minor(); ++i) {
            for (size_t j = 0; j != c.major(); ++j) {
                uchar d = _multiply_alpha_table[x[i, j]._][255];
                //printf("%d\n", (int) d);
                auto& p = c[i, j];
                p.r._ = d;
                p.g._ = d;
                p.b._ = d;
            }
        }
        return c;
    }

    // CPU port of Shaders.metal::otf::bezierFragmentFunction, simplified for
    // the rasterizer-into-atlas use case: no perspective (w=1 implicitly), no
    // page-to-screen transform.  Returns the signed analytic coverage of a
    // closed cubic Bezier path under a 1-pixel-sigma 2D Gaussian prefilter,
    // evaluated at one pixel.  Positive for CCW-outer contours (the
    // convention process_points emits after orientation normalization).
    static float coverage_at_pixel(std::vector<bezier4> const& curves,
                                   simd_float2 pixel_center,
                                   float pixel_size_du)
    {
        // TODO: This function is structured to avoid recursion and diverging
        // control flow on the GPU.  It could be written more isiomatically
        // on the CPU with recursion.

        // Typical glyphs have a few dozen curves and subdivide ~6 levels in
        // the worst case, so 128 is comfortable.  If we overflow we drop the
        // tail rather than crash; the coverage error is bounded by the
        // error-threshold on the dropped curves.
        constexpr int STACK_SIZE = 128;
        simd_float2 stack[STACK_SIZE][4];
        int sp = 0;

        // Convert to "erf coordinates": 1 unit == sqrt(2) pixels == one sigma
        // of the unit-pixel Gaussian.  Same scale convention as the shader's
        // `pixel_size * M_SQRT2_F` divisor.
        // NOTE: Extra factor of two from somewhere is needed
        float scale = 1.0f / (pixel_size_du * (float)M_SQRT2) * 2.0;

        for (auto const& curve : curves) {
            if (sp >= STACK_SIZE) break;
            stack[sp][0] = (curve.columns[0] - pixel_center) * scale;
            stack[sp][1] = (curve.columns[1] - pixel_center) * scale;
            stack[sp][2] = (curve.columns[2] - pixel_center) * scale;
            stack[sp][3] = (curve.columns[3] - pixel_center) * scale;
            ++sp;
        }

        float cumulant = 0.0f;
        while (sp) {
            --sp;
            simd_float2 a = stack[sp][0];
            simd_float2 b = stack[sp][1];
            simd_float2 c = stack[sp][2];
            simd_float2 d = stack[sp][3];

            // Control-point bbox: the curve stays inside this convex hull.
            simd_float2 lo = simd_min(simd_min(a, b), simd_min(c, d));
            simd_float2 hi = simd_max(simd_max(a, b), simd_max(c, d));
            // Map bbox corners to erf coordinates so the error estimate is in
            // the integrator's natural units.
            simd_float2 plo = erf(lo);
            simd_float2 phi = erf(hi);
            float error = (phi.x - plo.x) * (phi.y - plo.y);

            if (error <= 1.0f / 64.0f) {
                // Trapezoid rule between the two endpoints in erf coords.
                simd_float2 pa = erf(a);
                simd_float2 pd = erf(d);
                cumulant += (pd.y - pa.y) * (pa.x + pd.x);
            } else if (sp + 2 <= STACK_SIZE) {
                // de Casteljau subdivision at t=0.5: left half, right half.
                simd_float2 ab = simd_mix(a, b, 0.5f);
                simd_float2 bc = simd_mix(b, c, 0.5f);
                simd_float2 cd = simd_mix(c, d, 0.5f);
                simd_float2 abbc = simd_mix(ab, bc, 0.5f);
                simd_float2 bccd = simd_mix(bc, cd, 0.5f);
                simd_float2 abbbcccd = simd_mix(abbc, bccd, 0.5f);
                stack[sp][0] = a;
                stack[sp][1] = ab;
                stack[sp][2] = abbc;
                stack[sp][3] = abbbcccd;
                ++sp;
                stack[sp][0] = abbbcccd;
                stack[sp][1] = bccd;
                stack[sp][2] = cd;
                stack[sp][3] = d;
                ++sp;
            }
            // else stack full: drop this curve's contribution.
        }

        // Factor of 0.125 matches the shader: erf range is [-1, 1] (width 2),
        // and pa.x + pd.x is twice the trapezoid mean.
        return cumulant * 0.125f;
    }

    struct GlyphBitmap {
        matrix<R8Unorm> coverage; // 0 = no fill, 255 = full fill
        int bitmap_left;          // pixel offset from pen origin to left edge
        int bitmap_top;           // pixel offset from baseline upward to top edge
    };

    // Rasterize a glyph outline (in font design units) to an 8-bit coverage
    // bitmap at the given design-units-per-pixel scale, using the analytic
    // Gaussian-prefilter integrator above.  The returned offsets match
    // FreeType's bitmap_left/bitmap_top conventions so the rest of the
    // pipeline (apply_shadow, atlas placement) needs no changes.
    static GlyphBitmap rasterize_glyph(std::vector<bezier4> const& curves,
                                       float pixel_size_du)
    {
        if (curves.empty()) {
            // Empty glyph (e.g. space).  Hand back a zero-sized bitmap; the
            // downstream apply_shadow will produce a 4x4 fully-transparent
            // result, the atlas will place it, and only the advance matters.
            return GlyphBitmap{matrix<R8Unorm>{}, 0, 0};
        }

        // Control-point bbox in design units.
        simd_float2 lo = simd_float2{ INFINITY,  INFINITY};
        simd_float2 hi = simd_float2{-INFINITY, -INFINITY};
        for (auto const& curve : curves) {
            for (int i = 0; i != 4; ++i) {
                lo = simd_min(lo, curve.columns[i]);
                hi = simd_max(hi, curve.columns[i]);
            }
        }

        // Pixel-space bbox with Gaussian-spillover padding.  3 sigma captures
        // >99.7% of the Gaussian mass; beyond that the falloff is invisible
        // at 8-bit precision.
        constexpr int padding = 3;
        int x_min_px = (int)std::floor(lo.x / pixel_size_du) - padding;
        int x_max_px = (int)std::ceil (hi.x / pixel_size_du) + padding;
        int y_min_px = (int)std::floor(lo.y / pixel_size_du) - padding;
        int y_max_px = (int)std::ceil (hi.y / pixel_size_du) + padding;

        int width  = x_max_px - x_min_px;
        int height = y_max_px - y_min_px;

        matrix<R8Unorm> bitmap((size_t)height, (size_t)width);

        // TrueType design y is up; bitmap rows go top-to-bottom (row 0 = top).
        for (int row = 0; row != height; ++row) {
            float y_du = (y_max_px - row - 0.5f) * pixel_size_du;
            for (int col = 0; col != width; ++col) {
                float x_du = (x_min_px + col + 0.5f) * pixel_size_du;
                float cov = coverage_at_pixel(curves,
                                              simd_float2{x_du, y_du},
                                              pixel_size_du);
                // R8Unorm::operator=(float) clamps [0,1] internally.
                bitmap[row, col] = cov;
            }
        }

        return GlyphBitmap{
            std::move(bitmap),
            x_min_px,
            y_max_px,
        };
    }

    Font build_font(SpriteAtlas& atl) {

        Font result;

        // Load the font file and parse with our bespoke OTF/CFF stack.
        String bytes = string_from_file("Futura Medium Condensed.otf");
        auto handle = otf::Handle::parse({
            (byte const*)bytes.chars.data(),
            bytes.chars.size()
        });

        int upem = handle.units_per_em();
        constexpr int target_em_pixels = 40; // matches the old FT_Set_Pixel_Sizes
        float pixel_size_du = (float)upem / (float)target_em_pixels;

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
            auto gb = rasterize_glyph(curves, pixel_size_du);
            matrix<RGBA8Unorm_sRGB> u = rgba_from_a(gb.coverage);

            Sprite s = atl.place(u, make<float2>(-gb.bitmap_left,
                                                  gb.bitmap_top));

            float advance = handle.advance_for_glyph_index(glyph_index)
                            / pixel_size_du;
            result.charmap.insert(std::make_pair((uint)charcode,
                                                 Font::Glyph{s, advance}));
        }

        return result;
    }


    
    Font2 build_font2() {

        Font2 result;

        // Load the font file and parse with our bespoke OTF/CFF stack.
        // String bytes = string_from_file("Futura Medium Condensed.otf");
        String bytes = string_from_file("OpenSans_Condensed-Light.ttf");
        // String bytes = string_from_file("OpenSans-VariableFont_wdth,wght.ttf");
        auto handle = otf::Handle::parse({
            (byte const*)bytes.chars.data(),
            bytes.chars.size()
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


