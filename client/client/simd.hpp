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

#include "stdfloat.hpp"
#include "stdint.hpp"

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

// map from normalized device coordinates
//     [-1, 1] x [1, -1] x [0, 1]
// to texture coordinates
//     [ 0, 1] x [0,  1] x [0, 1]

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


// fixme
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


inline simd_float2 simd_floor(simd_float2 a) {
    return simd_make_float2(floor(a.x), floor(a.y));
}

inline simd_float2 simd_ceil(simd_float2 a) {
    return simd_make_float2(ceil(a.x), ceil(a.y));
}

inline simd_float2 project_screen_ray(const simd_float4x4& A, simd_float4& b) {
    
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
                                  simd_make_float4(0.0f, 0.0f, -1.0f, 0.0f),
                                  A.columns[3]);
    
    // Multiplication inverse is generally a poor way to solve linear
    // equations; is this small, modest accuracy case OK?
    
    simd_float4x4 C = simd_inverse(B);
    simd_float4 d = simd_mul(C, simd_make_float4(b.xy, 0.0f, b.w));
    b.z = d.z;
    
    return d.xy / d.w;
}

inline simd_float4x2 project_screen_frustum(const simd_float4x4& A) {
    
    simd_float4x4 B = simd_matrix(A.columns[0],
                                  A.columns[1],
                                  simd_make_float4(0.0f, 0.0f, -1.0f, 0.0f),
                                  A.columns[3]);
    simd_float4x4 C = simd_inverse(B);
    B = simd_matrix(simd_make_float4(-1, -1, 0, 1),
                    simd_make_float4(-1, +1, 0, 1),
                    simd_make_float4(+1, +1, 0, 1),
                    simd_make_float4(+1, -1, 0, 1));
    simd_float4x4 D = simd_mul(C, B);
    return simd_matrix(D.columns[0].xy / D.columns[0].w,
                       D.columns[1].xy / D.columns[1].w,
                       D.columns[2].xy / D.columns[2].w,
                       D.columns[3].xy / D.columns[3].w);
}


inline simd_float2 project_screen_ray(const simd_float4x4& A, const simd_float4& b) {
    simd_float4 c = b;
    return project_screen_ray(A, c);
}

typedef struct {
    
    simd_double4x4 slices[4];
    
} simd_double4x4x4;




#endif /* simd_hpp */
