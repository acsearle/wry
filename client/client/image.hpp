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
#include "debug.hpp"

namespace wry {
    
    // TODO: image concept
    //
    // We attempt to support matrix and matrix_view of various pixel formats

    // Duck typed interface?
    
    size_type image_width(const auto& v) {
        return v.minor();
    }

    size_type image_height(const auto& v) {
        return v.major();
    }
    
    size_type image_row_bytes(const auto& v) {
        return v.stride_bytes();
    }
    
    size_type image_rows(const auto& v) {
        return v.major();
    }
    
    size_type image_columns(const auto& v) {
        return v.minor();
    }
    
    auto* image_lookup(const auto& v, float2 xy) {
        difference_type2 ji = convert<difference_type>(floor(xy));
        if ((ji.x < 0) || (v.minor() <= ji.x) || (ji.y < 0) || (v.major() < ji.y))
            return static_cast<decltype(v.to(ji.x, ji.y))>(nullptr);
        return v.to(ji.x, ji.y);
    }
    
    auto image_interpolate(const auto& v, float2 xy) {
        xy = simd::clamp(xy - 0.5f,
                         make<float2>(0, 0),
                         make<float2>(image_width(v), image_height(v))-2) + 0.5f;
        float2 d = fract(xy);
        difference_type2 ji00 = convert<difference_type>(floor(xy));
        float x00 = v[ji00.x  , ji00.y  ];
        float x01 = v[ji00.x  , ji00.y+1];
        float x10 = v[ji00.x+1, ji00.y  ];
        float x11 = v[ji00.x+1, ji00.y+1];
        auto x0 = lerp(x00, x01, d.y);
        auto x1 = lerp(x10, x11, d.y);
        return lerp(x0, x1, d.x);
    }



    matrix<RGBA8Unorm_sRGB> from_png(StringView);
    void to_png(matrix<RGBA8Unorm_sRGB> const&, StringView);
    
    matrix<float4> multiply_alpha(const matrix<RGBA8Unorm_sRGB>& source);
    
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

    void halve(matrix<float4>&);
    void inflate(matrix<uchar4>&);
    
    matrix<float4> to_RGB8Unorm_sRGB(const matrix<simd_float4>&);
    
    inline void compose(matrix_view<RGBA8Unorm_sRGB> target, matrix_view<const R8Unorm> source, float2 offset) {
        for (difference_type y = 0; y != image_height(target)-1; ++y) {
            difference_type y2 = y - offset.y;
            if (y2 < 0)
                continue;
            if (y2 >= image_height(source))
                break;
            for (difference_type x = 0; x != image_width(target)-1; ++x) {
                // float alpha = image_interpolate(source, make<float2>(x, y) - offset);
                //auto* p = image_lookup(source, make<float2>(x, y));
                //if (!p)
                //    continue;
                //float alpha = *p;

                difference_type x2 = x - offset.x;
                if (x2 < 0)
                    continue;
                if (x2 >= image_width(source))
                    break;

                float alpha = source[x2, y2];
                
                //if (alpha != 0)
                //    DUMP(alpha);
                
                auto& a = target[x+1, y+1];
                a.r = a.r * (1.0f - alpha);
                a.g = a.g * (1.0f - alpha);
                a.b = a.b * (1.0f - alpha);
                a.a = a.a * (1.0f - alpha) + alpha;

                // Place above
                auto& b = target[x, y];
                b.r = b.r * (1.0f - alpha) + alpha;
                b.g = b.g * (1.0f - alpha) + alpha;
                b.b = b.b * (1.0f - alpha) + alpha;
                b.a = b.a * (1.0f - alpha) + alpha;
                
                /*
                // Place below
                a.r = alpha * (1.0f - a.a) + a.r;
                a.g = alpha * (1.0f - a.a) + a.g;
                a.b = alpha * (1.0f - a.a) + a.b;
                a.a = alpha * (1.0f - a.a) + a.a;
                 */

                
            }
        }
    }
    
    
} // namespace manic

#endif /* image_hpp */
