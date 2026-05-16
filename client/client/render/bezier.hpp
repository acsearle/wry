//
//  bezier.hpp
//  client
//
//  Created by Antony Searle on 19/3/2026.
//

#ifndef bezier_hpp
#define bezier_hpp

#include "simd.hpp"
#include "utility.hpp"

namespace wry {

    using bezier4 = simd_float4x2;

    // TODO: We can alternatively express these operations as matrix multiplies


    inline bezier4 make_bezier4(simd_float2x2 ab) {
        return bezier4{
            ab.columns[0],
            simd_mix(ab.columns[0], ab.columns[1], 1.0f / 3.0f),
            simd_mix(ab.columns[0], ab.columns[1], 2.0f / 3.0f),
            ab.columns[1]
        };
    }

    inline simd_float2 bezier4_evaluate(bezier4 abcd, float t) {

        simd_float2 a = abcd.columns[0];
        simd_float2 b = abcd.columns[1];
        simd_float2 c = abcd.columns[2];
        simd_float2 d = abcd.columns[3];

        simd_float2 ab = simd_mix(a, b, t);
        simd_float2 bc = simd_mix(b, c, t);
        simd_float2 cd = simd_mix(c, d, t);

        simd_float2 abbc = simd_mix(ab, bc, t);
        simd_float2 bccd = simd_mix(bc, cd, t);

        simd_float2 abbcbccd = simd_mix(abbc, bccd, t);

        return abbcbccd;
    }

    inline std::pair<bezier4, bezier4> bezier4_split(bezier4 abcd, float t) {

        simd_float2 a = abcd.columns[0];
        simd_float2 b = abcd.columns[1];
        simd_float2 c = abcd.columns[2];
        simd_float2 d = abcd.columns[3];

        simd_float2 ab = simd_mix(a, b, t);
        simd_float2 bc = simd_mix(b, c, t);
        simd_float2 cd = simd_mix(c, d, t);

        simd_float2 abbc = simd_mix(ab, bc, t);
        simd_float2 bccd = simd_mix(bc, cd, t);

        simd_float2 abbcbccd = simd_mix(abbc, bccd, t);

        return {{
            a,
            ab,
            abbc,
            abbcbccd
        },{
            abbcbccd,
            bccd,
            cd,
            d
        }};
    }
    
} // namespace wry

#endif /* bezier_hpp */
