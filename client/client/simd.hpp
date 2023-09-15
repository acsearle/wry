//
//  simd.hpp
//  client
//
//  Created by Antony Searle on 30/8/2023.
//

#ifndef simd_hpp
#define simd_hpp

#include <simd/simd.h>
#include <cassert>

#include "common.hpp"

#include "half.h"

#define M_PI_H ((half) M_PI)
#define M_PI_F ((float) M_PI)
#define M_PI_D ((double) M_PI)

typedef half simd_half1;
typedef __attribute__((__ext_vector_type__(2))) half simd_half2;
typedef __attribute__((__ext_vector_type__(3))) half simd_half3;
typedef __attribute__((__ext_vector_type__(4))) half simd_half4;
typedef __attribute__((__ext_vector_type__(8))) half simd_half8;
typedef __attribute__((__ext_vector_type__(16),__aligned__(16))) half simd_half16;
typedef __attribute__((__ext_vector_type__(32),__aligned__(16))) half simd_half32;

typedef __attribute__((__ext_vector_type__(2),__aligned__(2))) half simd_packed_half2;
typedef __attribute__((__ext_vector_type__(4),__aligned__(2))) half simd_packed_half4;
typedef __attribute__((__ext_vector_type__(8),__aligned__(2))) half simd_packed_half8;
typedef __attribute__((__ext_vector_type__(16),__aligned__(2))) half simd_packed_half16;
typedef __attribute__((__ext_vector_type__(32),__aligned__(2))) half simd_packed_half32;


typedef struct { simd_half2 columns[2]; } simd_half2x2;
typedef struct { simd_half2 columns[3]; } simd_half3x2;
typedef struct { simd_half2 columns[4]; } simd_half4x2;
typedef struct { simd_half3 columns[2]; } simd_half2x3;
typedef struct { simd_half3 columns[3]; } simd_half3x3;
typedef struct { simd_half3 columns[4]; } simd_half4x3;
typedef struct { simd_half4 columns[2]; } simd_half2x4;
typedef struct { simd_half4 columns[3]; } simd_half3x4;
typedef struct { simd_half4 columns[4]; } simd_half4x4;

inline simd_half3 simd_make_half3(half x, half y, half z) {
    simd_half3 result;
    result.x = x;
    result.y = y;
    result.z = z;
    return result;
}

inline simd_half4 simd_make_half4(half x, half y, half z, half w) {
    simd_half4 result;
    result.x = x;
    result.y = y;
    result.z = z;
    result.w = w;
    return result;
}

inline simd_half3x3 simd_matrix(simd_half3 col0, simd_half3 col1, simd_half3 col2) {
    simd_half3x3 result;
    result.columns[0] = col0;
    result.columns[1] = col1;
    result.columns[2] = col2;
    return result;
}

inline simd_half4x3 simd_matrix(simd_half3 col0, simd_half3 col1, simd_half3 col2, simd_half3 col3) {
    simd_half4x3 result;
    result.columns[0] = col0;
    result.columns[1] = col1;
    result.columns[2] = col2;
    result.columns[3] = col3;
    return result;
}

inline simd_half4x4 simd_matrix(simd_half4 col0, simd_half4 col1, simd_half4 col2, simd_half4 col3) {
    simd_half4x4 result;
    result.columns[0] = col0;
    result.columns[1] = col1;
    result.columns[2] = col2;
    result.columns[3] = col3;
    return result;
}


inline constexpr ulong simd_bitselect(ulong a, ulong b, ulong c) {
    return (a & ~c) | (b & c);
}

template<typename T> T simd_saturate(T x) {
    return simd_clamp(x, T(0), T(1));
}

inline simd_float4x4 simd_matrix4x4(simd_float3x3 a) {
    return simd_matrix(simd_make_float4(a.columns[0], 0.0f),
                       simd_make_float4(a.columns[1], 0.0f),
                       simd_make_float4(a.columns[2], 0.0f),
                       simd_make_float4(0.0f, 0.0f, 0.0f, 1.0f));
}

inline simd_float3x3 simd_matrix3x3(simd_float4x4 a) {
    return simd_matrix(a.columns[0].xyz,
                       a.columns[1].xyz,
                       a.columns[2].xyz);
}

inline constexpr simd_float4x4 matrix_ndc_to_tc_float4x4 = {{
    {  0.5f,  0.0f,  0.0f,  0.0f },
    {  0.0f, -0.5f,  0.0f,  0.0f },
    {  0.0f,  0.0f,  1.0f,  0.0f },
    {  0.5f,  0.5f,  0.0f,  1.0f },
}};

inline constexpr simd_float4x4 matrix_tc_to_ndc_float4x4 = {{
    {  2.0f,  0.0f,  0.0f,  0.0f },
    {  0.0f, -2.0f,  0.0f,  0.0f },
    {  0.0f,  0.0f,  1.0f,  0.0f },
    { -1.0f,  1.0f,  0.0f,  1.0f },
}};

inline constexpr simd_float4x4 matrix_perspective_float4x4 = {{
    {  1.0f,  0.0f,  0.0f,  0.0f,  },
    {  0.0f,  1.0f,  0.0f,  0.0f,  },
    {  0.0f,  0.0f,  1.0f,  1.0f,  },
    {  0.0f,  0.0f, -1.0f,  0.0f,  },
}};


inline simd_float4x4 simd_matrix_rotate(float theta, simd_float3 u) {
    assert(simd_all(u == simd_normalize(u)));
    return simd_matrix4x4(simd_quaternion(theta, u));
}

inline simd_float4x4 simd_matrix_translate(simd_float3 u) {
    return simd_matrix(simd_make_float4(1.0f, 0.0f, 0.0f, 0.0f),
                       simd_make_float4(0.0f, 1.0f, 0.0f, 0.0f),
                       simd_make_float4(0.0f, 0.0f, 1.0f, 0.0f),
                       simd_make_float4(u, 1.0f));
}

inline simd_float4x4 simd_matrix_translate(simd_float4 u) {
    return simd_matrix(simd_make_float4(u.w, 0.0f, 0.0f, 0.0f),
                       simd_make_float4(0.0f, u.w, 0.0f, 0.0f),
                       simd_make_float4(0.0f, 0.0f, u.w, 0.0f),
                       u);
}

inline simd_float4x4 simd_matrix_scale(float u) {
    return simd_matrix(simd_make_float4(  u, 0.0f, 0.0f, 0.0f),
                       simd_make_float4(0.0f,   u, 0.0f, 0.0f),
                       simd_make_float4(0.0f, 0.0f,   u, 0.0f),
                       simd_make_float4(0.0f, 0.0f, 0.0f, 1.0f));
}

inline simd_float4x4 simd_matrix_scale(simd_float3 u) {
    return simd_matrix(simd_make_float4(u.x, 0.0f, 0.0f, 0.0f),
                       simd_make_float4(0.0f, u.y, 0.0f, 0.0f),
                       simd_make_float4(0.0f, 0.0f, u.z, 0.0f),
                       simd_make_float4(0.0f, 0.0f, 0.0f, 1.0f));
}

inline simd_float4x4 simd_matrix_scale(simd_float4 u) {
    return simd_matrix(simd_make_float4(u.x, 0.0f, 0.0f, 0.0f),
                       simd_make_float4(0.0f, u.y, 0.0f, 0.0f),
                       simd_make_float4(0.0f, 0.0f, u.z, 0.0f),
                       simd_make_float4(0.0f, 0.0f, 0.0f, u.w));
}



typedef struct {
    
    simd_double4x4 slices[4];
    
} simd_double4x4x4;


namespace wry {
    
} // namespace wry


namespace wry {
    
    extern float* _from_sRGB_table;
    extern uchar (*_multiply_alpha_table)[256];
    
    inline constexpr int intRTNE(float x) {
        return (int) nearbyint(x);
    }
    
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
        
        constexpr explicit R8Unorm(float f) {
            write(f);
        }
        
        operator float() const {
            return read();
        }
        
        R8Unorm& operator=(float f) {
            write(f);
            return *this;
        }
        
    };
    
    struct R8Unorm_sRGB {
        
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
            assert(c > -1.0f);
            assert(c < 256.0f);
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
    
    struct alignas(4) RGBA8Unorm_sRGB {
        R8Unorm_sRGB r, g, b;
        R8Unorm a;
        
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
    };
    
} // namespace wry

#endif /* simd_hpp */