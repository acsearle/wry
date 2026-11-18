//
//  WryPathTracer.h
//  client
//
//  The path-traced reference renderer's host side (docs/path_tracing.md):
//  takes the instance lists WryWorldScene gathered for the frame, builds
//  the acceleration grid and the meshes' BVHs, decides whether the
//  accumulation restarts, dispatches a band of path samples
//  (PathTrace.metal path_trace_kernel), and resolves the mean into the
//  frame's colour inside the composite pass.
//

#ifndef WryPathTracer_h
#define WryPathTracer_h

#import <Metal/Metal.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ShaderTypes.h"
#include "PathTraceTypes.h"
#include "world_state.hpp"

@class WryRenderContext;
@class WryMesh;

namespace wry {

    // One frame's scene and settings, as the raster passes would draw it
    // (the pointers live for the call)
    struct PathTraceFrame {
        // The camera: MeshUniforms' ray basis, the drawable, and the
        // raster's clip planes as view depth
        simd_float4 camera_position_world;
        simd_float4 camera_ray_centre;
        simd_float4 camera_ray_dx;
        simd_float4 camera_ray_dy;
        simd_int2 viewport;
        float near_depth;
        float far_depth;
        // The light
        simd_float3 light_direction;    // unit, toward the sun
        simd_float3 sun_irradiance;     // times exposure
        float exposure;                 // the sky's scale
        std::string environment;        // its PNG's resource name
        // The settings (WorldState::_path_trace_*)
        int view;
        bool antialias;
        int bounces;
        int64_t target;
        uint64_t generation;
        std::shared_ptr<WorldState::PathTraceProgress> progress;
        // The scene; the onscreen flags mark what the raster would have
        // drawn (the rest is the margin's: lights and shadows only)
        const std::vector<WrySymbolInstance>* symbols;
        const std::vector<bool>* symbol_onscreen;
        const std::vector<WryDecalInstance>* prints;
        const std::vector<WryDecalInstance>* strings;
        WryMesh* meshes[WRY_PT_MAX_MESHES];
        const std::vector<bool>* mesh_onscreen[WRY_PT_MAX_MESHES];
        WryTerrainUniforms board;
        const std::vector<uint8_t>* kinds;
        // The images the surfaces read
        id<MTLTexture> field;
        id<MTLTexture> symbol_atlas;
        id<MTLTexture> glyphs;
        id<MTLTexture> decal_font;      // may be nil (no string decals then)
    };

} // namespace wry

@interface WryPathTracer : NSObject

- (nonnull instancetype)initWithContext:(nonnull WryRenderContext*)context;

// Trace this frame's band of samples (or nothing, once at the target) into
// the accumulator; before the composite pass
- (void)encodeFrame:(const wry::PathTraceFrame&)frame
      commandBuffer:(nonnull id<MTLCommandBuffer>)commandBuffer;

// Inside the composite pass (colour, ambient and depth attachments as the
// composite pipeline declares them): the accumulator's mean, or the
// view's, into the colour attachment
- (void)resolveWithEncoder:(nonnull id<MTLRenderCommandEncoder>)encoder;

// Forget the accumulation (the next frame restarts it)
- (void)reset;

@end

#endif /* WryPathTracer_h */
