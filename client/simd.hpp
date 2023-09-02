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

// extend some missing simd-like functions

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

inline constexpr simd_float4x4 simd_matrix_ndc_to_tc = {{
    {  0.5f,  0.0f,  0.0f,  0.0f },
    {  0.0f, -0.5f,  0.0f,  0.0f },
    {  0.0f,  0.0f,  1.0f,  0.0f },
    {  0.5f,  0.5f,  0.0f,  1.0f },
}};

inline constexpr simd_float4x4 simd_matrix_tc_to_ndc = {{
    {  2.0f,  0.0f,  0.0f,  0.0f },
    {  0.0f, -2.0f,  0.0f,  0.0f },
    {  0.0f,  0.0f,  1.0f,  0.0f },
    { -1.0f,  1.0f,  0.0f,  1.0f },
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

union r8Unorm_sRGB {
    unsigned char r;
    unsigned char x;
};

#define M_PI_F ((float) M_PI)

#endif /* simd_hpp */
