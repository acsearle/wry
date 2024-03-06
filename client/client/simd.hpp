//
//  simd.hpp
//  client
//
//  Created by Antony Searle on 30/8/2023.
//

#ifndef simd_hpp
#define simd_hpp

#include <simd/simd.h>
#include <cstdio>

#include "assert.hpp"
#include "stddef.hpp"
#include "stdfloat.hpp"
#include "stdint.hpp"

constexpr simd_float4x4 matrix_ndc_to_tc_float4x4 = {{
    {  0.5f,  0.0f,  0.0f,  0.0f },
    {  0.0f, -0.5f,  0.0f,  0.0f },
    {  0.0f,  0.0f,  1.0f,  0.0f },
    {  0.5f,  0.5f,  0.0f,  1.0f },
}};

constexpr simd_float4x4 matrix_tc_to_ndc_float4x4 = {{
    {  2.0f,  0.0f,  0.0f,  0.0f },
    {  0.0f, -2.0f,  0.0f,  0.0f },
    {  0.0f,  0.0f,  1.0f,  0.0f },
    { -1.0f,  1.0f,  0.0f,  1.0f },
}};

constexpr simd_float4x4 matrix_perspective_float4x4 = {{
    {  1.0f,  0.0f,  0.0f,  0.0f,  },
    {  0.0f,  1.0f,  0.0f,  0.0f,  },
    {  0.0f,  0.0f,  1.0f,  1.0f,  },
    {  0.0f,  0.0f, -1.0f,  0.0f,  },
}};

inline simd_float4x4 matrix_perspective_right_hand(float fovyRadians, float aspect, float nearZ, float farZ) {
    float ys = 1 / tanf(fovyRadians * 0.5);
    float xs = ys / aspect;
    float zs = farZ / (farZ - nearZ);
    return simd_matrix_from_rows(simd_make_float4(xs,  0,  0,           0),
                                 simd_make_float4(0, ys,  0,           0),
                                 simd_make_float4(0,  0, zs, -nearZ * zs),
                                 simd_make_float4(0,  0,  1,           0 ));
}

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

inline simd_float4x4 simd_matrix_translate(float x, float y, float z = 0.0f, float w = 1.0f) {
    return simd_matrix(simd_make_float4(w, 0.0f, 0.0f, 0.0f),
                       simd_make_float4(0.0f, w, 0.0f, 0.0f),
                       simd_make_float4(0.0f, 0.0f, w, 0.0f),
                       simd_make_float4(x, y, z, w));
}


inline simd_float4x4 simd_matrix_scale(float x) {
    return simd_matrix(simd_make_float4(  x, 0.0f, 0.0f, 0.0f),
                       simd_make_float4(0.0f,   x, 0.0f, 0.0f),
                       simd_make_float4(0.0f, 0.0f,   x, 0.0f),
                       simd_make_float4(0.0f, 0.0f, 0.0f, 1.0f));
}

inline simd_float4x4 simd_matrix_scale(simd_float3 u, float w = 1.0f) {
    return simd_matrix(simd_make_float4(u.x, 0.0f, 0.0f, 0.0f),
                       simd_make_float4(0.0f, u.y, 0.0f, 0.0f),
                       simd_make_float4(0.0f, 0.0f, u.z, 0.0f),
                       simd_make_float4(0.0f, 0.0f, 0.0f, w));
}

inline simd_float4x4 simd_matrix_scale(simd_float4 u) {
    return simd_matrix(simd_make_float4(u.x, 0.0f, 0.0f, 0.0f),
                       simd_make_float4(0.0f, u.y, 0.0f, 0.0f),
                       simd_make_float4(0.0f, 0.0f, u.z, 0.0f),
                       simd_make_float4(0.0f, 0.0f, 0.0f, u.w));
}

inline simd_float4x4 simd_matrix_scale(float x, float y, float z = 1, float w = 1.0f) {
    return simd_matrix(simd_make_float4(x, 0, 0, 0),
                       simd_make_float4(0, y, 0, 0),
                       simd_make_float4(0, 0, z, 0),
                       simd_make_float4(0, 0, 0, w));
}

struct simd_float4x4x4 {
    
    simd_float4x4 slices[4];
    
};

struct simd_double4x4x4 {
    
    simd_double4x4 slices[4];
    
};

namespace wry {
    
    namespace simd {
        
        using namespace ::simd;
        
        using half1 = half;
        
        using half2 = __attribute__((__ext_vector_type__(2))) half;
        using half3 = __attribute__((__ext_vector_type__(3))) half;
        using half4 = __attribute__((__ext_vector_type__(4))) half;
        using half8 = __attribute__((__ext_vector_type__(8))) half;
        using half16 = __attribute__((__ext_vector_type__(16),__aligned__(16))) half;
        using half32 = __attribute__((__ext_vector_type__(32),__aligned__(16))) half;
        
        namespace packed {
            
            using namespace ::simd::packed;
            
            using half2 = __attribute__((__ext_vector_type__(2),__aligned__(2))) half;
            using half4 = __attribute__((__ext_vector_type__(4),__aligned__(2))) half;
            using half8 = __attribute__((__ext_vector_type__(8),__aligned__(2))) half;
            using half16 = __attribute__((__ext_vector_type__(16),__aligned__(2))) half;
            using half32 = __attribute__((__ext_vector_type__(32),__aligned__(2))) half;
            
        };
        
        struct half2x2 { half2 columns[2]; };
        struct half3x2 { half2 columns[3]; };
        struct half4x2 { half2 columns[4]; };
        struct half2x3 { half3 columns[2]; };
        struct half3x3 { half3 columns[3]; };
        struct half4x3 { half3 columns[4]; };
        struct half2x4 { half4 columns[2]; };
        struct half3x4 { half4 columns[3]; };
        struct half4x4 { half4 columns[4]; };
        
        inline constexpr half M_PI_H = M_PI;
        inline constexpr float M_PI_F = M_PI;
        inline constexpr double M_PI_D = M_PI;
        
        using difference_type2 = Vector_t<difference_type, 2>;
        using size_type2 = Vector_t<size_type, 2>;
        
        using ::simd::bitselect;
        template<typename T>
        inline constexpr Vector_t<T, 1> bitselect(Vector_t<T, 1>  a, Vector_t<T, 1>  b, Vector_t<T, 1>  c) {
            return (a & ~c) | (b & c);
        }
        
    };
    
    using namespace wry::simd;
    
    inline float2 project_screen_ray(const simd_float4x4& A, simd_float4& b) {
        
        // Project the mouse ray with z the unknown ray parameter onto the XY
        // plane
        //
        //     A [s t 0 u]^T = [x y z 1]^T
        //
        // We want the plane parametric coordinates (s/u, t/u)
        
        // For unknowns s,t, u and z, rearrange the equation:
        //
        // [ a00 a01  0 a03 ] [ s ]   [ x ]
        // [ a10 a11  0 a13 ] [ t ] = [ y ]
        // [ a20 a21 -1 a23 ] [ z ]   [ 0 ]
        // [ a30 a31  0 a33 ] [ u ]   [ 1 ]
        
        simd_float4x4 B = simd_matrix(A.columns[0],
                                      A.columns[1],
                                      make<float4>(0.0f, 0.0f, -1.0f, 0.0f),
                                      A.columns[3]);
        
        // Multiplication inverse is generally a poor way to solve linear
        // equations; is this small, modest accuracy case OK?
        
        simd_float4x4 C = simd_inverse(B);
        simd_float4 d = simd_mul(C, make<float4>(b.xy, 0.0f, b.w));
        b.z = d.z;
        
        return d.xy / d.w;
    }
    
    inline simd_float4x2 project_screen_frustum(const simd_float4x4& A) {
        
        simd_float4x4 B = simd_matrix(A.columns[0],
                                      A.columns[1],
                                      make<float4>(0.0f, 0.0f, -1.0f, 0.0f),
                                      A.columns[3]);
        simd_float4x4 C = simd_inverse(B);
        B = simd_matrix(make<float4>(-1, -1, 0, 1),
                        make<float4>(-1, +1, 0, 1),
                        make<float4>(+1, +1, 0, 1),
                        make<float4>(+1, -1, 0, 1));
        simd_float4x4 D = simd_mul(C, B);
        return simd_matrix(D.columns[0].xy / D.columns[0].w,
                           D.columns[1].xy / D.columns[1].w,
                           D.columns[2].xy / D.columns[2].w,
                           D.columns[3].xy / D.columns[3].w);
    }
    
    
    inline float2 project_screen_ray(const simd_float4x4& A, const simd_float4& b) {
        simd_float4 c = b;
        return project_screen_ray(A, c);
    }
    
    inline float saturate(float x) {
        if (x < 0.0f)
            return 0.0f;
        if (x > 1.0f)
            return 1.0f;
        return x;
    }
    
    inline float smoothstep5(float x) {
        x = saturate(x);
        return ((6.0f * x - 15.0f) * x + 10.0f) * x * x * x;
    }
    
    inline float dsmoothstep5(float x) {
        x = saturate(x);
        return ((30.0f * x - 60.0f) * x + 30.0f) * x * x;
    }
    
    simd_float4 interpolate_wheeled_vehicle(simd_float2 x0, simd_float2 y0,
                                            simd_float2 x1, simd_float2 y1,
                                            float t);
    
} // namespace wry

#endif /* simd_hpp */
