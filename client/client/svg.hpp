//
//  svg.hpp
//  client
//
//  Created by Antony Searle on 8/3/2026.
//

#ifndef svg_hpp
#define svg_hpp

#include <vector>

#include "simd.hpp"
#include "string_view.hpp"

namespace wry::svg {
    
    // Rudimentary SVG loading
    
    // TODO:
    // - Transform to px
    // - Clip to viewBox
    // - Non-paths?
    // - Mixed primitives (vs convert all to CubicBezier)
    // - Arcs in paths
    
    struct CubicBezier {        
        float4x4 control_points;
    };
    
    struct PiecewiseCurve {
        uchar4 color;
        std::vector<CubicBezier> cubic_beziers;
    };
    
    std::vector<PiecewiseCurve> parse(StringView& v);
    
} // namespace wry::svg

#endif /* svg_hpp */
