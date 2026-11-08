//
//  environment_prefilter.hpp
//  client
//
//  The image-based light's specular sky: an equirectangular environment
//  prefiltered by GGX into a cube, one roughness per mip level.
//

#ifndef environment_prefilter_hpp
#define environment_prefilter_hpp

#include <Metal/Metal.h>

namespace wry {

    // The cube's shape.  Level k holds the sky prefiltered at roughness
    // 2^(k + 1 - levels), i.e. 1/16, 1/8, 1/4, 1/2, 1 for 5 levels, so the
    // lighting pass reads roughness r at lod log2(r) + 4.
    inline constexpr NSUInteger kEnvironmentPrefilterSize = 256;
    inline constexpr NSUInteger kEnvironmentPrefilterLevels = 5;

    // Encode the prefilter (CubeFilterVertex / CubeFilterAccumulate3, one
    // additive pass per level into a scratch cube, blitted into place) on
    // a new command buffer of `queue` and commit it; does not wait.  Work
    // later committed to the same queue sees the finished cube.  The
    // equirect is sampled bilinearly at its level 0 (u = atan2(y, x) / 2pi
    // wrapping, v = acos(z) / pi clamped).  Returns a private
    // RGBA32Float cube labelled `label`.
    [[nodiscard]] id<MTLTexture>
    prefilter_environment(id<MTLDevice> device,
                          id<MTLLibrary> library,
                          id<MTLCommandQueue> queue,
                          id<MTLTexture> equirect,
                          NSString* label);

} // namespace wry

#endif /* environment_prefilter_hpp */
