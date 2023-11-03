//
//  font.mm
//  client
//
//  Created by Antony Searle on 25/6/2023.
//

// include FreeType2, suppressing warnings
//#pragma clang diagnostic push
//#pragma clang diagnostic ignored "-Wdocumentation"
#include <ft2build.h>
#include FT_FREETYPE_H
//#pragma clang diagnostic pop

#include <iostream>
#include <string>

#include "assert.hpp"
#include "debug.hpp"
#include "font.hpp"
#include "image.hpp"
#include "utility.hpp"
#include "platform.hpp"

namespace wry {
    
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
                
        matrix<float> a(x.minor() + 4, x.major() + 4);
        matrix<R8Unorm> b(x.minor() + 4 + 4, x.major() + 4 + 4);
        
        b = 0.0f;
        b.sub(4, 4, x.minor(), x.major()) = x;
        
        // Compute offset filter
        for (size_t i = 0; i != a.minor(); ++i) {
            for (size_t j = 0; j != a.major(); ++j) {
                a[i, j] = 0.0f;
                for (size_t u = 0; u != 5; ++u) {
                    for (size_t v = 0; v != 5; ++v) {
                        //printf("%g\n", (float) b[i + u, j + v]);
                        a[i, j] += k[u] * k[v] * b[i + u, j + v];
                    }
                }
            }
        }
        
        // Blend with offset glyph alpha
        for (size_t i = 0; i != x.minor(); ++i) {
            for (size_t j = 0; j != x.major(); ++j) {
                float alpha = x[i, j];
                (a[i + 0, j + 2] *= (1.0 - alpha)) += alpha;
            }
        }
        
        // Copy alpha into final result
        matrix<RGBA8Unorm_sRGB> c(a.minor(), a.major());
        for (size_t i = 0; i != c.minor(); ++i) {
            for (size_t j = 0; j != c.major(); ++j) {
                c[i, j].a = a[i, j];
            }
        }
        
        // Color is alpha to linear color to sRGB
        for (size_t i = 0; i != x.minor(); ++i) {
            for (size_t j = 0; j != x.major(); ++j) {
                uchar d = _multiply_alpha_table[x[i, j]._][255];
                //printf("%d\n", (int) d);
                auto& p = c[i + 0, j + 2];
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
                        path_for_resource(u8"Futura Medium Condensed", u8"otf").c_str(),
                        // path_for_resource("Hack-Regular", "ttf").c_str(),
                        //"/Users/antony/Desktop/assets/OpenSans-VariableFont_wdth,wght.ttf",
                        0,
                        &face);
        assert(!e);
        
        FT_Set_Pixel_Sizes(face, 0, 40);
        
        FT_UInt gindex = 0;
        FT_ULong charcode = FT_Get_First_Char(face, &gindex);
        
        matrix<uchar4> u;
        constexpr float k = 1.0f / 64.0f; // metrics are in 26.6 fixed point
        
        // render all glyphs
        while (gindex) {
            
            // DUMP(charcode);
            
            // Render fractional pixel coverage
            // FT_Load_Glyph(face, gindex, FT_LOAD_RENDER); // load and render for gray level
            
            // Render signed distance field from Bezier curves
            // FT_Load_Glyph(face, gindex, FT_LOAD_DEFAULT); // load but do not render
            // FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF);

            // Render signed distance field from bitmask
            FT_Load_Glyph(face, gindex, FT_LOAD_RENDER); // load and render for gray level
            FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF); // re-render for SDF

            // apply subtle dropshadow
            auto v = matrix_view<R8Unorm>(stride_iterator<R8Unorm>(reinterpret_cast<R8Unorm*>(face->glyph->bitmap.buffer),
                                                                   face->glyph->bitmap.pitch),
                                          face->glyph->bitmap.rows,
                                          face->glyph->bitmap.width);
            matrix<RGBA8Unorm_sRGB> u = apply_shadow(v);
            // draw_bounding_box(u);
            
            sprite s = atl.place(u,
                                 make<float2>(-face->glyph->bitmap_left + 2,
                                                  +face->glyph->bitmap_top + 1
                                                  ));
            
            float advance = face->glyph->advance.x * k;
            result.charmap.insert(std::make_pair((uint) charcode, font::glyph{s, advance}));
            
            charcode = FT_Get_Next_Char(face, charcode, &gindex);
        }
        
        result.height = face->size->metrics.height * k;
        result.ascender = face->size->metrics.ascender * k;
        result.descender = face->size->metrics.descender * k;
        
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        
        return result;
    }
    
} // namespace wry



namespace wry {
    
    // source of glyph coverage maps or signed distance fields
    
    struct Font2 {
        
        FT_Library library;
        FT_Face face;
                
        Font2() {
            
            FT_Error e;
            
            {
                FT_Init_FreeType(&library);
                e = FT_Init_FreeType(&library);
                assert(!e);
            }
            
            {
                const char* filepathname
                = "/Users/antony/Desktop/assets/Futura Medium Condensed.otf";
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
        
        ~Font2() {
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
        static auto* f = new Font2();
        
        return (*f)[charcode];
        
    }

    
    
    
} // namespace wry


