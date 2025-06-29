//
//  morton.hpp
//  client
//
//  Created by Antony Searle on 24/11/2024.
//

#ifndef morton_hpp
#define morton_hpp

#include <cstdint>

namespace wry {
    
    using std::uint32_t;
    using std::uint64_t;
    
    // https://en.wikipedia.org/wiki/Z-order_curve
    constexpr bool _less_msb(uint32_t a, uint32_t b) {
        return (a < b) && (a < (a ^ b));
    }
    
    constexpr bool _less_msb_by_clz(uint32_t a, uint32_t b) {
        return __builtin_clz(b) < __builtin_clz(a);
    }
    
    // suppose we encode rects by xmin,ymin,xmax,ymax
    // we then have a 4d space
    // invariants xmin < xmax, ymin < ymax are two diagonal cuts through
    // the space
    // to find areas that overlap a query area we want
    // qxmin < xmax and qxmax > xmin and qymin < ymax and qymax > ymax
    // this suggests that we should reverse the ordering of either min or max
    // then everything will run the same way in the ordering (does this actually
    // matter?)
    // the area query places more constraining hyperplanes; they enclose an
    // unbounded volume but the min < max invariants close it(?).  But there can
    // be an infinite space of overlapping areas
    
    // in 64 bit this requires 16 bit coordinates, which is barely enough?
    // very wasteful since the dimensions will rarely be large and the upper bits
    // of xmin, xmax will be redundant
    // we can reduce the maximum allowed width of areas and reclaim those upper bits
    // 24+8, 24+8 ?
    
    
    
    
    
    
    
}

#endif /* morton_hpp */
