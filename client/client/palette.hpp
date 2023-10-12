//
//  palette.hpp
//  client
//
//  Created by Antony Searle on 12/10/2023.
//

#ifndef palette_hpp
#define palette_hpp

#include <optional>
#include "matrix.hpp"
#include "sim.hpp"
#include "simd.hpp"

namespace wry {
    
    template<typename MatrixView>
    typename MatrixView::value_type* matrix_lookup(MatrixView& v, simd_float2 xy) {
        difference_type i = floor(xy.x);
        difference_type j = floor(xy.y);
        if ((i < 0) || (v.minor() <= i) || (j < 0) || (v.major() < j))
            return nullptr;
        return v.to(i, j);
    }
    
    inline simd_float2 project_mouse_ray(simd_float4x4 A, simd_float2 b) {
        
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
        simd_float4x4 C = simd_inverse(B);
        simd_float4 d = simd_mul(C, simd_make_float4(b, 0.0f, 1.0f));
        // printf("d.z = %g\n", d.z);
        return d.xy / d.w;
    }
    
    template<typename T>
    struct Palette {
        
        simd_float4x4 _transform;
        simd_float4x4 _inverse_transform;
        matrix<T> _payload;
        
        // intersect mouse ray with Palette
        // mouse ray is (u, v, ?, 1)
        std::optional<simd_float2> intersect(simd_float4 ray) {
            
            // the Palette is drawn with the transform
            
            // xy / w = MVP . uv01;
            // z and w unknown
            
            simd_float4 a = simd_mul(_inverse_transform, ray);
            
            return a.xy / a.w;
            
        }
        
        std::optional<simd_int2> bucket(simd_float2 xy_viewport) {
            std::optional<simd_float2> a = intersect(xy_viewport);
        }
        
        T* operator[](simd_float2 xy_viewport) {
            auto a = intersect(xy_viewport);
            if (!a)
                return nullptr;
            return matrix_lookup(*a);
        }
        
    };
    
} // namespace wry


#endif /* palette_hpp */
