//
//  bezier.hpp
//  client
//
//  Created by Antony Searle on 19/3/2026.
//

#ifndef bezier_hpp
#define bezier_hpp

#include <span>

#include "matrix.hpp"
#include "simd.hpp"
#include "sRGB.hpp"
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
    
    // ---- Analytic coverage of a closed compound cubic Bezier path ----------
    //
    // The CPU twin of Shaders.metal::otf::bezierFragmentFunction, shared by
    // the font engine (font.mm) and the SVG loader (io/svg.cpp): anything
    // that can be expressed as a list of cubic Beziers whose concatenation
    // is a set of closed contours rasterizes through here.
    //
    // The result is the signed winding number of the path, convolved with
    // a Gaussian prefilter of one pixel scale, at one pixel center.  The
    // sign convention is the positive integral of x dy: a contour that is
    // counterclockwise in a y-up frame contributes +1 inside.  Callers
    // clamp to [0, 1], so overlapping same-signed contours union (nonzero
    // fill clamped to coverage) and opposite-signed contours cut holes.
    //
    // Curves are processed one at a time, so the curve count is unbounded;
    // the subdivision stack is per curve.
    float bezier4_coverage(std::span<bezier4 const> curves,
                           simd_float2 pixel_center,
                           float pixel_size);

    struct CoverageBitmap {
        matrix<R8Unorm> coverage; // 0 = no fill, 255 = full fill
        int left;                 // pixel column of coverage column 0
        int top;                  // pixel row (counting up) of the top edge
    };

    // Rasterize the curves, in a y-up frame where one pixel is pixel_size
    // units, to a coverage bitmap covering their control-point bounding box
    // plus a 3 pixel prefilter margin.  Row 0 is the top.  Pixel (col, row)
    // of the bitmap has its center at ((left + col + 0.5) * pixel_size,
    // (top - row - 0.5) * pixel_size).  Empty input gives an empty bitmap.
    CoverageBitmap rasterize_coverage(std::span<bezier4 const> curves,
                                      float pixel_size);

    // Signed area (integral of x dy) of the curves, exact for cubics; for a
    // set of closed contours this is the area weighted by winding number.
    double bezier4_signed_area(std::span<bezier4 const> curves);

} // namespace wry

#endif /* bezier_hpp */
