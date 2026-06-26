//
//  scene_image.hpp
//  client
//
//  Created by Antony Searle on 2026-06-26.
//
//  Texture-coordinate window helpers shared by the backdrop scenes (splash,
//  main menu).  A "window" is (u0, v0, u1, v1) in [0,1] texCoords; pairing a
//  full-viewport quad with such a window is how WryRenderContext -drawImage:
//  crops / pans / zooms an image to the screen.
//

#ifndef scene_image_hpp
#define scene_image_hpp

#include <simd/simd.h>

namespace wry {

    // Largest centered sub-rect of the image with the viewport's aspect ratio
    // -- "cover": fills the screen, cropping the overflow axis.  No distortion.
    inline simd_float4 image_cover_window(float imgW, float imgH,
                                          float vpW, float vpH) {
        if (imgW <= 0.0f || imgH <= 0.0f || vpW <= 0.0f || vpH <= 0.0f)
            return simd_make_float4(0.0f, 0.0f, 1.0f, 1.0f);
        const float ai = imgW / imgH;   // image aspect
        const float av = vpW / vpH;     // viewport aspect
        if (ai >= av) {
            const float ww = av / ai;   // crop the (wider) horizontal axis
            const float u0 = (1.0f - ww) * 0.5f;
            return simd_make_float4(u0, 0.0f, u0 + ww, 1.0f);
        } else {
            const float wh = ai / av;   // crop the (taller) vertical axis
            const float v0 = (1.0f - wh) * 0.5f;
            return simd_make_float4(0.0f, v0, 1.0f, v0 + wh);
        }
    }

    // A viewport-aspect window smaller than the cover window by `zoom` (< 1, so
    // there is room to move), positioned by (sx, sy) in [0,1] across the
    // pannable range.  (sx,sy)=(0,0) is the top-left extreme, (1,1) the
    // bottom-right.  The window always stays within [0,1], so a pan never
    // reaches the image border.
    inline simd_float4 image_pan_window(float imgW, float imgH,
                                        float vpW, float vpH,
                                        float zoom, float sx, float sy) {
        const simd_float4 cov = image_cover_window(imgW, imgH, vpW, vpH);
        const float coverW = cov.z - cov.x;
        const float coverH = cov.w - cov.y;
        const float ww = coverW * zoom;
        const float wh = coverH * zoom;
        const float u0 = sx * (1.0f - ww);
        const float v0 = sy * (1.0f - wh);
        return simd_make_float4(u0, v0, u0 + ww, v0 + wh);
    }

} // namespace wry

#endif /* scene_image_hpp */
