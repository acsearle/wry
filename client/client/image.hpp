//
//  image.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef image_hpp
#define image_hpp

#include <cstdlib>
#include <utility>

#include "matrix.hpp"
#include "string.hpp"

namespace wry {
    
    // Images are in sRGB colorspace with premultiplied alpha
    //
    // libpng loads sRGB with non-premultiplied alpha.
    //
    // Linear RGB with premultipled alpha is required for correct filtering.
    //
    // We need another image format, linear RGB represented as f32, for filtering
    // and compositing?
    //
    // Emmissives are their own texture layer so the fact they can't be represented
    // in non-premultiplied-alpha pngs is irrelevant.
    
    
    /* Metal Spec 3.1 conversions
     
     sRGB conversions:
     
     if (c <= 0.04045)
     result = c / 12.92;
     else
     result = powr((c + 0.055) / 1.055, 2.4);
     
     if (isnan(c))
        c = 0.0;
     if (c > 1.0)
        c = 1.0;
     else if (c < 0.0)
        c = 0.0;
     else if (c < 0.0031308)
        c = 12.92 * c;
     else
        c = 1.055 * powr(c, 1.0/2.4) - 0.055;
     // Convert to integer scale: c = c * 255.0
     // Convert to integer: c = c + 0.5
     // Drop the decimal fraction. The remaining floating-point(integral) value
     // is converted directly to an integer.
     
     
     U/Snorm conversions:
     
     float(c) / 255.0
     
     max(-1.0,
     float(c)/127.0)
     
     x = min(max(f * 255.0, 0.0), 255.0)
     i7:0 = intRTNE(x)
     
     result = min(max(f * 127.0, -127.0), 127.0)
     i7:0 = intRTNE(x)
     
     
     
     */
    
    using pixel = simd_uchar4;
    using image = matrix<pixel>;
    
    using imagef = matrix<simd_float4>;

    image from_png(string_view);
    image from_png_and_multiply_alpha(string_view);
    imagef from_png_and_multiply_alpha_f(string_view);
    void to_png(image const&, char const*);
    
    void multiply_alpha(image& a);
    
    inline f32 from_sRGB(f32 u) {
        return ((u <= 0.04045f)
                ? (u / 12.92f)
                : (std::powf((u + 0.055f) / 1.055f, 2.4f)));
    }
    
    extern f32* _from_sRGB_table;
    
    inline f32 from_sRBG(u8 u) {
        return _from_sRGB_table[u];
    }
    
    inline simd_float4 from_sRGB_(pixel p) {
        return simd_make_float4(_from_sRGB_table[p.r],
                                _from_sRGB_table[p.g],
                                _from_sRGB_table[p.b],
                                p.a / 255.0f);
    }
    
    inline f32 to_sRGB(f32 u) {
        return ((u <= 0.0031308f)
                ? (u * 12.92f)
                : (1.055f * powf(u, 1.0f / 2.4f) - 0.055f));
    }
    
    inline simd_float4 to_sRGB(simd_float4 v) {
        return simd_float4{
            to_sRGB(v.r)  * 255.0f,
            to_sRGB(v.g)  * 255.0f,
            to_sRGB(v.b)  * 255.0f,
            v.a * 255.0f,
        };
    }
    
    extern u8 (*_multiply_alpha_table)[256];
    
    inline pixel multiply_alpha(pixel x) {
        return pixel{
            _multiply_alpha_table[x.a][x.r],
            _multiply_alpha_table[x.a][x.g],
            _multiply_alpha_table[x.a][x.g],
            x.a
        };
    }
    
    extern u8 (*_divide_alpha_table)[256];
    
    inline pixel divide_alpha(pixel x) {
        return pixel{
            _multiply_alpha_table[x.a][x.r],
            _multiply_alpha_table[x.a][x.g],
            _multiply_alpha_table[x.a][x.g],
            x.a
        };
    }
    
    inline void draw_bounding_box(matrix_view<pixel>& x) {
        // draw a dark box outline _under_ the premultiplied alpha image
        auto foo = [&](auto i, auto j) {
            auto b = x(i, j).a;
            b = b * 3 / 4 + 64;
            x(i, j).a = b;
        };
        for (i64 j = 0; j != x.columns(); ++j) {
            foo(0, j);
            foo(x.rows() - 1, j);
        }
        for (i64 i = 1; i != x.rows() - 1; ++i) {
            foo(i, 0);
            foo(i, x.columns() - 1);
        }
    }

    void halve(imagef&);
    void inflate(image&);
    
    image to_RGB8Unorm_sRGB(const imagef&);
    
    
} // namespace manic

#endif /* image_hpp */
