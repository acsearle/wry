//
//  sRGB.cpp
//  client
//
//  Created by Antony Searle on 17/9/2023.
//

#include <algorithm>
#include <cstring>

#include "sRGB.hpp"

namespace wry {
    
    const float* R8Unorm_sRGB::_from_sRGB_table = []() {
        const int count = 256;
        float* p = (float*) malloc(count * sizeof(float));
        for (int index = 0; index != count; ++index) {
            p[index] = from_sRGB(index / 255.0f);
        }
        return p;
    }();
    
    
    // Use a table to perform premultiplication in sRGB color space.  Lots faster
    // in debug mode, at least
    uchar (*_multiply_alpha_table)[256] = []() {
        uchar (*p)[256] = (uchar (*)[256]) malloc(256 * 256);
        // We expect to have long blocks of alpha = 0 or alpha = 255, so to be
        // cache friendly, we make color be the minor index
        
        // 64k table same size as modern L1 cache?
        uchar alpha = 0;
        do {
            uchar color = 0;
            do {
                // perf: we can compute from_sRGB(uchar) via a table
                // perf: we can compute from_sRGB(uchar) without the / 255.0 by
                //       adjusting the other constants
                // perf: we can transpose the loop and compute from_sRGB(color / 255.0) / 255.0 once for all alpha
                p[alpha][color] = round(to_sRGB(from_sRGB(color / 255.0f) * alpha / 255.0f) * 255.0f);
            } while (++color);
        } while (++alpha);
        
        return p;
    }();
        
    uchar (*_divide_alpha_table)[256] = []() {
        uchar (*p)[256] = (uchar (*)[256]) malloc(256 * 256);
        // We expect to have long blocks of alpha = 0 or alpha = 255, so to be
        // cache friendly, we make color be the minor index
        
        std::memset(p, 0, 256);
        uchar alpha = 1;
        do {
            uchar color = 0;
            do {
                // perf: we can compute from_sRGB(uchar) via a table
                // perf: we can compute from_sRGB(uchar) without the / 255.0 by
                //       adjusting the other constants
                // perf: we can transpose the loop and compute from_sRGB(color / 255.0) / 255.0 once for all alpha
                p[alpha][color] = std::clamp<float>(round(to_sRGB(from_sRGB(color / 255.0f) / (alpha / 255.0f)) * 255.0f), 0, 255);
            } while ((uchar) ++color);
        } while ((uchar) ++alpha);
        
        return p;
    }();
    
} // namespace wry
