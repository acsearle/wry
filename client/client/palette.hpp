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
    
    template<typename T>
    struct Palette {
        
        simd_float4x4 _transform;
        simd_float4x4 _inverse_transform;
        matrix<T> _payload;
        
        // intersect mouse ray with Palette
        // mouse ray is (u, v, ?, 1)
        std::optional<simd_float2> intersect(simd_float4 screen_ray) {
            
            // the Palette is drawn with the transform
            
            // xy / w = MVP . uv01;
            // z and w unknown
            
            simd_float4 a = simd_mul(_inverse_transform, screen_ray);
            
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
