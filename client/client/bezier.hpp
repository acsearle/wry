//
//  bezier.hpp
//  client
//
//  Created by Antony Searle on 19/3/2026.
//

#ifndef bezier_hpp
#define bezier_hpp

#include "simd.hpp"

namespace wry {
    
    // TODO: Perhaps simply operate on simd::floatN pointers

    template<std::size_t N, typename T = simd::float2>
    struct BezierCurve {
        T x[N];
    };
    
    // TODO: Pull support functions out of the font loading
    
    void bezier4_split(simd::float2* dest, simd::float2 const* src, float t) {
        dest[0] = src[0];
        dest[7] = src[3];

        dest[1] = simd_mix(src[0], src[1], t);
        simd::float2 bc = simd_mix(src[1], src[2], t);
        dest[6] = simd_mix(src[2], src[3], t);
        
        dest[2] = simd_mix(dest[1], bc, t);
        dest[5] = simd_mix(bc, dest[7], t);
        dest[3] = dest[4] = simd_mix(dest[2], dest[5], t);
    }
    
    void bezier4_from_bezier3(simd::float2* dest, simd::float2 const* src) {
        dest[0] = src[0];
        dest[1] = simd_mix(src[0], src[1], 2.0 / 3.0);
        dest[2] = simd_mix(src[1], src[2], 1.0 / 3.0);
        dest[3] = src[2];
    }

    void bezier4_from_bezier2(simd::float2* dest, simd::float2 const* src) {
        dest[0] = src[0];
        dest[1] = simd_mix(src[0], src[1], 1.0 / 3.0);
        dest[2] = simd_mix(src[0], src[1], 2.0 / 3.0);
        dest[3] = src[1];
    }

    simd::float2 bezier4_evaluate(simd::float2 const* src, float t) {
        simd::float2 ab = simd_mix(src[0], src[1], t);
        simd::float2 bc = simd_mix(src[1], src[2], t);
        simd::float2 cd = simd_mix(src[2], src[3], t);
        simd::float2 abbc = simd_mix(ab, bc, t);
        simd::float2 bccd = simd_mix(bc, cd, t);
        return simd_mix(abbc, bccd, t);
    }
    
    
} // namespace wry

#endif /* bezier_hpp */
