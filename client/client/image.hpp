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
#include "sRGB.hpp"

namespace wry {

    matrix<RGBA8Unorm_sRGB> from_png(string_view);
    void to_png(matrix<RGBA8Unorm_sRGB> const&, string_view);
    
    matrix<simd_float4> multiply_alpha(const matrix<RGBA8Unorm_sRGB>& source);
    
    void multiply_alpha_inplace(matrix<RGBA8Unorm_sRGB>& target);

    
    template<typename ImageView>
    inline void draw_bounding_box(ImageView& x) {
        // draw a dark box outline _under_ the premultiplied alpha image
        auto foo = [&](auto i, auto j) {
            auto b = x(i, j).a;
            b = b * 0.5f + 0.5f;
            x(i, j).a = b;
        };
        for (size_t j = 0; j != x.get_major(); ++j) {
            foo(0, j);
            foo(x.get_minor() - 1, j);
        }
        for (size_t i = 1; i != x.get_minor() - 1; ++i) {
            foo(i, 0);
            foo(i, x.get_major() - 1);
        }
    }

    void halve(matrix<simd_float4>&);
    void inflate(matrix<simd_uchar4>&);
    
    matrix<simd_float4> to_RGB8Unorm_sRGB(const matrix<simd_float4>&);
    
    
} // namespace manic

#endif /* image_hpp */
