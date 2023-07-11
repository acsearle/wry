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
    
    image apply_shadow(const_matrix_view<std::uint8_t> x) {

        // We get an alpha map.
        //
        // We want a drop-shadow, Gaussian, offset below

        double k[5] = { 0.0625, 0.25, 0.375, 0.25, 0.0625 };
        // double k[5] = { 1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0 };
        // double k[5] = { 1.0 / 6.0, 4.0 / 6.0, 6.0 / 6.0, 4.0 / 6.0, 1.0 / 6.0 };
        // double k[5] = { 0, 0, 1, 0, 0 };
                
        matrix<double> a(x.rows() + 4, x.columns() + 4);
        matrix<std::uint8_t> b(x.rows() + 4 + 4, x.columns() + 4 + 4);
        b.sub(4, 4, x.rows(), x.columns()) = x;
        
        // Compute offset filter
        for (i64 i = 0; i != a.rows(); ++i) {
            for (i64 j = 0; j != a.columns(); ++j) {
                for (i64 u = 0; u != 5; ++u) {
                    for (i64 v = 0; v != 5; ++v) {
                        a(i, j) += k[u] * k[v] * b(i + u, j + v);
                    }
                }
            }
        }
        
        // Blend with offset glyph alpha
        for (i64 i = 0; i != x.rows(); ++i) {
            for (i64 j = 0; j != x.columns(); ++j) {
                (a(i + 1, j + 2) *= (1.0 - (x(i, j) / 255.0))) += x(i, j);
            }
        }
                
        
        // Copy alpha into final result
        image c(a.rows(), a.columns());
        for (i64 i = 0; i != c.rows(); ++i) {
            for (i64 j = 0; j != c.columns(); ++j) {
                c(i, j).a = round(a(i, j));
                //std::cout << i << ", " << j << ", " << (int) c(i, j).a << std::endl;
                //u8 d = _multiply_alpha_table[c(i, j).a][255];
                //c(i, j).rgb = {d, d, d};
            }
        }
        
        // Color is alpha to linear color to sRGB
        for (i64 i = 0; i != x.rows(); ++i) {
            for (i64 j = 0; j != x.columns(); ++j) {
                // u8 d = std::round(to_sRGB(x(i, j) / 255.0) * 255.0); // <-- replace with table
                u8 d = _multiply_alpha_table[x(i, j)][255];
                c(i + 1, j + 2).rgb = {d, d, d};
            }
        }
        
        
        return c;
        
    }
    
    // tight-bound
    gl::ivec2 prune(image& x, auto predicate = [](pixel v) {
        return v == pixel{0,0,0,0}; } )
    {
        gl::ivec2 offset = {};
        
        auto f = [&](auto v) {
            return std::all_of(std::begin(v), std::end(v), predicate);
        };
        
        while (x.rows() && f(x.front())) {
            ++offset.y;
            --x._rows;
            x._begin += x._stride;
        }

        while (x.rows() && f(x.back())) {
            --x._rows;
        }
        
        while (x.columns() && f(x.column(0))) {
            ++offset.x;
            --x._columns;
            ++x._begin;
        }

        while (x.columns() && f(x.column(x.columns() - 1))) {
            --x._columns;
        }
        
        return offset;
        
    }
        
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
        
        image u;
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
            auto v = const_matrix_view<std::uint8_t>(face->glyph->bitmap.buffer,
                                                     face->glyph->bitmap.width,
                                                     face->glyph->bitmap.pitch,
                                                     face->glyph->bitmap.rows);
            image u = apply_shadow(v);
            draw_bounding_box(u);
            
            sprite s = atl.place(u,
                                 vec2(-face->glyph->bitmap_left + 2,
                                      +face->glyph->bitmap_top + 1));
            
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
