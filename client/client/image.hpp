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
#include "simd.hpp"
#include "string.hpp"

namespace wry {

    matrix<RGBA8Unorm_sRGB> from_png(string_view);
    void to_png(matrix<RGBA8Unorm_sRGB> const&, string_view);
    
    matrix<simd_float4> multiply_alpha(const matrix<RGBA8Unorm_sRGB>& source);
    
    void multiply_alpha_inplace(matrix<RGBA8Unorm_sRGB>& target);

    inline f32 from_sRGB(f32 u) {
        return ((u <= 0.04045f)
                ? (u / 12.92f)
                : (std::powf((u + 0.055f) / 1.055f, 2.4f)));
    }
    
    inline f32 from_sRBG(u8 u) {
        return _from_sRGB_table[u];
    }
    
    /*
    inline simd_float4 from_sRGB_(pixel p) {
        return simd_make_float4(_from_sRGB_table[p.r],
                                _from_sRGB_table[p.g],
                                _from_sRGB_table[p.b],
                                p.a / 255.0f);
    }
     */
    
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
        
    /*
    inline pixel multiply_alpha(pixel x) {
        return pixel{
            _multiply_alpha_table[x.a][x.r],
            _multiply_alpha_table[x.a][x.g],
            _multiply_alpha_table[x.a][x.g],
            x.a
        };
    }
    */
    /*
    extern u8 (*_divide_alpha_table)[256];
    
    inline pixel divide_alpha(pixel x) {
        return pixel{
            _multiply_alpha_table[x.a][x.r],
            _multiply_alpha_table[x.a][x.g],
            _multiply_alpha_table[x.a][x.g],
            x.a
        };
    }
     */
    
    template<typename ImageView>
    inline void draw_bounding_box(ImageView& x) {
        // draw a dark box outline _under_ the premultiplied alpha image
        auto foo = [&](auto i, auto j) {
            auto b = x(i, j).a;
            b = b * 0.5f + 0.5f;
            x(i, j).a = b;
        };
        for (i64 j = 0; j != x.get_major(); ++j) {
            foo(0, j);
            foo(x.get_minor() - 1, j);
        }
        for (i64 i = 1; i != x.get_minor() - 1; ++i) {
            foo(i, 0);
            foo(i, x.get_major() - 1);
        }
    }

    void halve(matrix<simd_float4>&);
    void inflate(matrix<simd_uchar4>&);
    
    matrix<simd_float4> to_RGB8Unorm_sRGB(const matrix<simd_float4>&);
    
    
} // namespace manic

#endif /* image_hpp */
