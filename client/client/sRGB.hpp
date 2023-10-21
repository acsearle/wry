//
//  sRGB.hpp
//  client
//
//  Created by Antony Searle on 17/9/2023.
//

#ifndef sRGB_hpp
#define sRGB_hpp

#include "simd.hpp"

namespace wry {
    
    inline float from_sRGB(float x) {
        return ((x <= 0.04045f)
                ? (x / 12.92f)
                : (std::pow((x + 0.055f) / 1.055f, 2.4f)));
    }
    
    inline float to_sRGB(float x) {
        return ((x <= 0.0031308f)
                ? (x * 12.92f)
                : (1.055f * std::powf(x, 1.0f / 2.4f) - 0.055f));
    }
    
    inline simd_float3 from_sRGB(simd_float3 rgb) {
        return simd_make_float3(from_sRGB(rgb.r),
                                from_sRGB(rgb.g),
                                from_sRGB(rgb.b));
    }
            
    inline simd_float3 to_sRGB(simd_float3 rgb) {
        return simd_make_float3(to_sRGB(rgb.r),
                                to_sRGB(rgb.g),
                                to_sRGB(rgb.b));
    }
    
    inline simd_float4 from_sRGB(simd_float4 rgba) {
        return simd_make_float4(from_sRGB(rgba.r),
                                from_sRGB(rgba.g),
                                from_sRGB(rgba.b),
                                rgba.a);
    }

    inline simd_float4 to_sRGB(simd_float4 rgba) {
        return simd_make_float4(to_sRGB(rgba.r),
                                to_sRGB(rgba.g),
                                to_sRGB(rgba.b),
                                rgba.a);
    }


    // round ties to nearest even
    
    inline constexpr int intRTNE(float x) {
        return static_cast<int>(nearbyint(x));
    }
    
    // 8 bit usigned normalized integer
    
    struct R8Unorm {
        
        uchar _;
        
        float read() const {
            return float(_) / 255.0f;
        }
        
        constexpr void write(float f) {
            float x = f * 255.0f;
            if (!(x > 0.0f)) {
                _ = 0;
                return;
            }
            if (x > 255.0f) {
                _ = 255.0f;
                return;
            }
            _ = intRTNE(x);
        }
        
        R8Unorm() = default;
        
        R8Unorm(const R8Unorm&) = default;
        R8Unorm(R8Unorm&&) = default;

        constexpr explicit R8Unorm(float f) {
            write(f);
        }
        
        operator float() const {
            return read();
        }

        R8Unorm& operator=(const R8Unorm&) = default;
        R8Unorm& operator=(R8Unorm&&) = default;

        R8Unorm& operator=(float f) {
            write(f);
            return *this;
        }
        
    };
    
    struct R8Unorm_sRGB {
        
        static const float* _from_sRGB_table;

        uchar _;
        
        float read() const {
            return _from_sRGB_table[_];
        }
        
        constexpr void write(float c) {
            if (!(c > 0.0f)) { // nonpositive, including nan
                _ = 0;
                return;
            }
            if (c >= 1.0f) { // saturate
                _ = 255;
                return;
            }
            if (c < 0.0031308f)
                c = fma(c, 3294.6f, 0.5f);
            else
                c = fma(pow(c, 1.0f / 2.4f), 269.025f, -13.525);
            assert((0.0f <= c) && (c < 256.0f));
            _ = c;
        }
        
        R8Unorm_sRGB() = default;
        
        constexpr explicit R8Unorm_sRGB(float c) {
            write(c);
        }
        
        operator float() const {
            return read();
        }
        
    };
    
    extern uchar (*_multiply_alpha_table)[256];
        
    struct alignas(4) RGBA8Unorm_sRGB {
        R8Unorm_sRGB r, g, b;
        R8Unorm a;
        
        constexpr RGBA8Unorm_sRGB() = default;
        
        constexpr RGBA8Unorm_sRGB(float red, float green, float blue, float alpha)
        : r(red)
        , g(green)
        , b(blue)
        , a(alpha) {
        }
        
    };
    
    struct alignas(4) BGRA8Unorm_sRGB {
        
        R8Unorm_sRGB b, g, r;
        R8Unorm a;
        
        constexpr BGRA8Unorm_sRGB() = default;
        
        constexpr BGRA8Unorm_sRGB(float blue, float green, float red, float alpha)
        : b(blue)
        , g(green)
        , r(red)
        , a(alpha) {
        }
    };
    
} // namespace wry
#endif /* sRGB_hpp */
