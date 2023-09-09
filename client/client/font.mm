//
//  font.mm
//  client
//
//  Created by Antony Searle on 25/6/2023.
//

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"

#include <ft2build.h>
#include FT_FREETYPE_H

#pragma clang diagnostic pop

#include <cassert>
#include <iostream>
#include <string>

#include "debug.hpp"
#include "font.hpp"
#include "image.hpp"
#include "utility.hpp"

namespace wry {
    
    string path_for_resource(string_view name, string_view ext);

    
    auto binomial_n(auto first, auto n) {
        // [k] = (n ; k) = n! / (k! (n - k)!)
        // \Sigma_{k=0}^n (n; k) = 2^n
        decltype(n) a = 1, b = 1;
        while (n) {
            *first = a;
            a *= n;
            assert(a % b == 0);
            a /= b;
            ++first;
            --n;
            ++b;
        }
        return first;
    }
    
    // Apply a simple pixel-scale drop-shadow to existing artwork
    
    matrix<RGBA8Unorm_sRGB> apply_shadow(matrix_view<R8Unorm> x) {

        // We get an alpha map.
        //
        // We want a drop-shadow, Gaussian, offset below

        float k[5] = { 0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f };
        // double k[5] = { 1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0 };
        // double k[5] = { 1.0 / 6.0, 4.0 / 6.0, 6.0 / 6.0, 4.0 / 6.0, 1.0 / 6.0 };
        // double k[5] = { 0, 0, 1, 0, 0 };
                
        matrix<float> a(x.get_minor() + 4, x.get_major() + 4);
        matrix<R8Unorm> b(x.get_minor() + 4 + 4, x.get_major() + 4 + 4);
        b.sub(4, 4, x.get_minor(), x.get_major()) = x;
        
        // Compute offset filter
        for (i64 i = 0; i != a.get_minor(); ++i) {
            for (i64 j = 0; j != a.get_major(); ++j) {
                for (i64 u = 0; u != 5; ++u) {
                    for (i64 v = 0; v != 5; ++v) {
                        a(i, j) += k[u] * k[v] * b(i + u, j + v);
                    }
                }
            }
        }
        
        // Blend with offset glyph alpha
        for (i64 i = 0; i != x.get_minor(); ++i) {
            for (i64 j = 0; j != x.get_major(); ++j) {
                float alpha = x(i, j);
                (a(i + 1, j + 2) *= (1.0 - alpha)) += alpha;
            }
        }
                
        
        // Copy alpha into final result
        matrix<RGBA8Unorm_sRGB> c(a.get_minor(), a.get_major());
        for (i64 i = 0; i != c.get_minor(); ++i) {
            for (i64 j = 0; j != c.get_major(); ++j) {
                c(i, j).a = a(i, j);
            }
        }
        
        // Color is alpha to linear color to sRGB
        for (i64 i = 0; i != x.get_minor(); ++i) {
            for (i64 j = 0; j != x.get_major(); ++j) {
                u8 d = _multiply_alpha_table[x(i, j)._][255];
                auto& p = c(i + 2, j + 1);
                p.r._ = d;
                p.g._ = d;
                p.b._ = d;
            }
        }
        
        
        return c;
        
    }
    
    
    /*
    
    // tight-bound
    simd_int2 prune(image<simd_uchar4>& x, auto predicate = [](pixel v) {
        return v == pixel{0,0,0,0}; } )
    {
        simd_int2 offset = {};
        
        auto f = [&](auto v) {
            return std::all_of(std::begin(v), std::end(v), predicate);
        };
        
        while (x.get_height() && f(x.front())) {
            ++offset.y;
            --x._height;
            x._origin += x._stride;
        }

        while (x.get_height() && f(x.back())) {
            --x._height;
        }
        
        while (x.get_width() && f(x.column(0))) {
            ++offset.x;
            --x._width;
            ++x._origin;
        }

        while (x.get_width() && f(x.column(x.get_width() - 1))) {
            --x._width;
        }
        
        return offset;
        
    }
     
     */
     
    font build_font(atlas& atl) {
        
        font result;
        
        FT_Library ft;
        FT_Error e = FT_Init_FreeType(&ft);
        assert(!e);
        
        FT_Face face;
        e = FT_New_Face(ft,
                        path_for_resource("Futura Medium Condensed", "otf").c_str(),
                        // path_for_resource("Hack-Regular", "ttf").c_str(),
                        0,
                        &face);
        assert(!e);
        
        FT_Set_Pixel_Sizes(face, 0, 48);
        
        FT_UInt gindex = 0;
        FT_ULong charcode = FT_Get_First_Char(face, &gindex);
        
        matrix<simd_uchar4> u;
        constexpr float k = 1.0f / 64.0f; // metrics are in 26.6 fixed point
        
        // render all glyphs
        while (gindex) {
            
            // DUMP(charcode);
            
            // Render fractional pixel coverage
            FT_Load_Glyph(face, gindex, FT_LOAD_RENDER); // load and render for gray level
            
            // Render signed distance field from Bezier curves
            // FT_Load_Glyph(face, gindex, FT_LOAD_DEFAULT); // load but do not render
            // FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF);

            // Render signed distance field from bitmask
            // FT_Load_Glyph(face, gindex, FT_LOAD_RENDER); // load and render for gray level
            // FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF); // re-render for SDF

            // apply subtle dropshadow
            auto v = matrix_view<R8Unorm>(stride_iterator<R8Unorm>(reinterpret_cast<R8Unorm*>(face->glyph->bitmap.buffer),
                                                                   face->glyph->bitmap.pitch),
                                          face->glyph->bitmap.rows,
                                          face->glyph->bitmap.width);
            matrix<RGBA8Unorm_sRGB> u = apply_shadow(v);
            draw_bounding_box(u);
            
            sprite s = atl.place(u,
                                 simd_make_float2(-face->glyph->bitmap_left + 2,
                                                  +face->glyph->bitmap_top + 1
                                                  ));
            
            float advance = face->glyph->advance.x * k;
            result.charmap.insert(std::make_pair((u32) charcode, font::glyph{s, advance}));
            
            charcode = FT_Get_Next_Char(face, charcode, &gindex);
        }
        
        result.height = face->size->metrics.height * k;
        result.ascender = face->size->metrics.ascender * k;
        result.descender = face->size->metrics.descender * k;
        
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        
        return result;
    }
    
}
