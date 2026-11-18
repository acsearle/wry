//
//  WryPathTracer.mm
//  client
//
//  docs/path_tracing.md.  Main thread only, like the rest of the scene,
//  except the command buffers' completion handlers, which only store the
//  GPU time into an atomic.
//

#import "WryPathTracer.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>

#import "WryMesh.h"
#import "WryRenderContext.h"
#include "path_trace_bvh.hpp"
#include "path_trace_grid.hpp"

namespace {

    // A frame's tracing budget, GPU seconds: the samples and rows of a
    // dispatch are chosen to fit it (a command buffer that runs for
    // seconds is killed, and the process with it: wry::abort_on_command_
    // buffer_error).  Offscreen the frames are on demand, so more fits
    constexpr double kWindowedBudget = 0.040;
    constexpr double kOffscreenBudget = 0.250;
    constexpr int kMaxSamplesPerFrame = 64;
    constexpr int kMinRowsPerFrame = 8;
    constexpr int kMaxRowStride = 256;

    // What a frame's dispatch was, and (from its completion handler) how
    // long it took
    // Published by the completion handlers, read by the next frames: one
    // location each (relaxed: no other data rides on them).  A restart
    // bumps the epoch and forgets the cost, so a frame of the old image
    // (a cheap view, say) cannot size the new one's dispatches
    struct Timing {
        std::atomic<double> per{0.0};         // GPU seconds per row-sample; 0: not measured
        std::atomic<double> seconds{0.0};     // the last measured frame's
        std::atomic<std::uint64_t> epoch{0};
    };

    template<typename T>
    std::uint64_t hash_value(std::uint64_t h, const T& value) {
        return wry::path_trace_hash(h, &value, sizeof(T));
    }

    std::uint64_t hash_pointer(std::uint64_t h, id object) {
        const void* p = (__bridge const void*)object;
        return wry::path_trace_hash(h, &p, sizeof(p));
    }

} // namespace

@implementation WryPathTracer {
    WryRenderContext* _ctx;
    id<MTLComputePipelineState> _kernel;
    id<MTLRenderPipelineState> _resolve;

    // Every mesh's BVH, triangles and vertices, concatenated; built once
    // for the meshes' vertex buffers (they never change)
    id<MTLBuffer> _meshVertexSources[WRY_PT_MAX_MESHES];
    WryPathTraceMesh _meshTable[WRY_PT_MAX_MESHES];
    simd_float3 _meshLo[WRY_PT_MAX_MESHES];
    simd_float3 _meshHi[WRY_PT_MAX_MESHES];
    id<MTLBuffer> _nodes;
    id<MTLBuffer> _triangles;
    id<MTLBuffer> _vertices;

    // Defaults for maps a mesh lacks
    id<MTLTexture> _white;
    id<MTLTexture> _black;
    id<MTLTexture> _flatNormal;

    id<MTLTexture> _environment;
    std::string _environmentName;

    // The accumulator: two float4 per pixel (PathTraceTypes.h)
    id<MTLBuffer> _accumulator;
    simd_int2 _accumulatorSize;
    bool _haveKey;
    std::uint64_t _key;
    bool _clear;                    // zero the accumulator before the next dispatch
    std::uint64_t _passes;          // samples every pixel has
    int _phase;                     // the pass's next row phase (0: a new pass)
    int _passStride;                // the pass's row stride (and phases)
    int _streak;                    // consecutive frames that restarted
    int _passSamples;               // the pass's samples per pixel
    std::shared_ptr<Timing> _timing;
    int _resolveView;
    simd_int2 _resolveViewport;
}

- (instancetype)initWithContext:(WryRenderContext*)context {
    if ((self = [super init])) {
        _ctx = context;
        _timing = std::make_shared<Timing>();
        auto solid = [&](uint8_t r, uint8_t g, uint8_t b, MTLPixelFormat format) {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                                         width:1
                                                                                        height:1
                                                                                     mipmapped:NO];
            id<MTLTexture> t = [context.device newTextureWithDescriptor:d];
            const uint8_t texel[4] = {r, g, b, 255};
            [t replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:texel bytesPerRow:4];
            return t;
        };
        _white = solid(255, 255, 255, MTLPixelFormatRGBA8Unorm);
        _black = solid(0, 0, 0, MTLPixelFormatRGBA8Unorm);
        _flatNormal = solid(128, 128, 255, MTLPixelFormatRGBA8Unorm);
    }
    return self;
}

- (void)reset {
    _haveKey = false;
}

// Pipelines on first use: the kernel is large, and most sessions never
// trace
- (void)makePipelines {
    if (_kernel)
        return;
    NSError* error = nil;
    _kernel = [_ctx.device newComputePipelineStateWithFunction:[_ctx newFunctionWithName:@"path_trace::path_trace_kernel"]
                                                         error:&error];
    if (!_kernel) {
        NSLog(@"path trace kernel: %@", error);
        abort();
    }
    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.colorAttachments[AAPLColorIndexColor].pixelFormat = _ctx.drawablePixelFormat;
    descriptor.colorAttachments[AAPLColorIndexColor].blendingEnabled = NO;
    descriptor.colorAttachments[AAPLColorIndexAmbient].pixelFormat = MTLPixelFormatRGBA16Float;
    descriptor.colorAttachments[AAPLColorIndexAmbient].writeMask = MTLColorWriteMaskNone;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    descriptor.vertexFunction = [_ctx newFunctionWithName:@"path_trace::resolve_vertex"];
    descriptor.fragmentFunction = [_ctx newFunctionWithName:@"path_trace::resolve_fragment"];
    descriptor.label = @"Path trace resolve";
    _resolve = [_ctx newRenderPipelineStateWithDescriptor:descriptor];
}

// The meshes' BVHs, once per set of vertex buffers
- (void)makeMeshes:(const wry::PathTraceFrame&)frame {
    bool same = _nodes != nil;
    for (int k = 0; k != WRY_PT_MAX_MESHES; ++k)
        same = same && _meshVertexSources[k] == frame.meshes[k].vertexBuffer;
    if (same)
        return;
    std::vector<WryPathTraceNode> nodes;
    std::vector<WryPathTraceTriangle> triangles;
    std::vector<MeshVertex> vertices;
    for (int k = 0; k != WRY_PT_MAX_MESHES; ++k) {
        id<MTLBuffer> source = frame.meshes[k].vertexBuffer;
        _meshVertexSources[k] = source;
        WryPathTraceMesh& m = _meshTable[k];
        m = WryPathTraceMesh{};
        _meshLo[k] = simd_make_float3(0.0f, 0.0f, 0.0f);
        _meshHi[k] = _meshLo[k];
        if (!source)
            continue;
        // Unshared vertices, three per triangle (io/Wavefront.cpp): the
        // strip index buffer only joins them, so it is not needed
        const MeshVertex* v = (const MeshVertex*)source.contents;
        const std::size_t count = source.length / sizeof(MeshVertex);
        const std::size_t triangle_count = count / 3;
        std::vector<simd_float4> positions(3 * triangle_count);
        for (std::size_t i = 0; i != positions.size(); ++i)
            positions[i] = v[i].position;
        wry::PathTraceBVH bvh = wry::build_path_trace_bvh(positions.data(), triangle_count);
        // The kernel's traversal stack holds 32 (it needs the depth + 1)
        if (wry::path_trace_bvh_depth(bvh) > 31)
            NSLog(@"path trace: mesh %d's BVH is %d deep; PathTrace.metal mesh_intersect's stack of 32 drops subtrees",
                  k, wry::path_trace_bvh_depth(bvh));
        m.node_offset = (unsigned)nodes.size();
        m.triangle_offset = (unsigned)triangles.size();
        m.vertex_offset = (unsigned)vertices.size();
        m.node_count = (unsigned)bvh.nodes.size();
        if (!bvh.nodes.empty()) {
            const WryPathTraceNode& root = bvh.nodes[0];
            _meshLo[k] = simd_make_float3(root.lo[0], root.lo[1], root.lo[2]);
            _meshHi[k] = simd_make_float3(root.hi[0], root.hi[1], root.hi[2]);
        }
        nodes.insert(nodes.end(), bvh.nodes.begin(), bvh.nodes.end());
        triangles.insert(triangles.end(), bvh.triangles.begin(), bvh.triangles.end());
        vertices.insert(vertices.end(), v, v + 3 * triangle_count);
    }
    auto buffer = [&](const void* bytes, std::size_t length) {
        // Metal refuses empty buffers
        static const std::uint64_t zero[4] = {};
        if (!length)
            return [_ctx.device newBufferWithBytes:zero length:sizeof(zero) options:MTLResourceStorageModeShared];
        return [_ctx.device newBufferWithBytes:bytes length:length options:MTLResourceStorageModeShared];
    };
    _nodes = buffer(nodes.data(), nodes.size() * sizeof(WryPathTraceNode));
    _triangles = buffer(triangles.data(), triangles.size() * sizeof(WryPathTraceTriangle));
    _vertices = buffer(vertices.data(), vertices.size() * sizeof(MeshVertex));
    _nodes.label = @"Path trace BVH nodes";
    _triangles.label = @"Path trace triangles";
    _vertices.label = @"Path trace vertices";
}

- (id<MTLTexture>)environmentNamed:(const std::string&)name {
    if (!_environment || name != _environmentName) {
        _environment = [_ctx sharedTextureFromResource:[NSString stringWithUTF8String:name.c_str()]
                                                ofType:@"png"
                                       withPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB];
        _environmentName = name;
    }
    return _environment;
}

// The beauty's noise for `path-trace-status`: each pixel's standard error
// relative to its mean (luminance), over a sample of the pixels.  Reads
// the shared accumulator as it stands (a frame still in flight may tear a
// few pixels; this is an estimate)
- (void)reportNoise:(wry::WorldState::PathTraceProgress&)progress {
    if (!_accumulator)
        return;
    const simd_float4* a = (const simd_float4*)_accumulator.contents;
    const std::size_t pixels = (std::size_t)_accumulatorSize.x * (std::size_t)_accumulatorSize.y;
    std::vector<double> relative;
    relative.reserve(pixels / 7 + 1);
    double dropped = 0.0;
    for (std::size_t i = 0; i < pixels; ++i)
        dropped += a[2 * i + 1].w;
    for (std::size_t i = 0; i < pixels; i += 7) {
        simd_float4 sum = a[2 * i], squares = a[2 * i + 1];
        double n = sum.w;
        if (n < 2.0)
            continue;
        auto lum = [](simd_float3 c) { return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z; };
        double mean = lum(sum.xyz) / n;
        // The luminance's spread from the channels' (the second moments
        // are per channel): sigma(lum) <= sum w_c sigma_c, tight when the
        // channels move together, as a path's brightness makes them
        simd_float3 m = sum.xyz / (float)n;
        simd_float3 var = simd_max(squares.xyz / (float)n - m * m, 0.0f) * (float)(n / (n - 1.0));
        double sigma = 0.2126 * std::sqrt(var.x) + 0.7152 * std::sqrt(var.y) + 0.0722 * std::sqrt(var.z);
        double variance = sigma * sigma;
        relative.push_back(std::sqrt(variance / n) / std::max(mean, 1e-3));
    }
    progress.noise_median = progress.noise_p90 = -1.0;   // not measurable
    if (!relative.empty()) {
        std::sort(relative.begin(), relative.end());
        progress.noise_median = relative[relative.size() / 2];
        progress.noise_p90 = relative[std::min(relative.size() - 1, relative.size() * 9 / 10)];
    }
    progress.dropped = (std::uint64_t)dropped;
}

- (void)encodeFrame:(const wry::PathTraceFrame&)frame
      commandBuffer:(id<MTLCommandBuffer>)commandBuffer
{
    using namespace simd;
    [self makePipelines];
    [self makeMeshes:frame];
    id<MTLTexture> environment = [self environmentNamed:frame.environment];
    wry::WorldState::PathTraceProgress& progress = *frame.progress;
    progress.active = true;
    progress.traced_generation = frame.generation;
    _resolveView = frame.view;
    _resolveViewport = frame.viewport;

    // ---- The scene

    const std::vector<WrySymbolInstance>& symbols = *frame.symbols;
    std::vector<WryDecalInstance> decals;
    decals.reserve(frame.prints->size() + frame.strings->size());
    decals.insert(decals.end(), frame.prints->begin(), frame.prints->end());
    decals.insert(decals.end(), frame.strings->begin(), frame.strings->end());
    if (!frame.decal_font)
        decals.resize(frame.prints->size());

    struct Item {
        std::uint32_t code;
        simd_float3 lo, hi;
    };
    std::vector<Item> items;
    items.reserve(symbols.size() + 64);
    for (std::size_t i = 0; i != symbols.size(); ++i) {
        const WrySymbolInstance& s = symbols[i];
        Item it;
        wry::transform_box(s.model_transform, -s.half_extent.xyz, s.half_extent.xyz, it.lo, it.hi);
        it.code = (std::uint32_t)i | ((*frame.symbol_onscreen)[i] ? 0u : WRY_PT_ITEM_OFFSCREEN);
        items.push_back(it);
    }
    std::vector<WryPathTraceMeshInstance> instances;
    for (int k = 0; k != WRY_PT_MAX_MESHES; ++k) {
        WryMesh* mesh = frame.meshes[k];
        if (!mesh || !_meshTable[k].node_count)
            continue;
        const MeshInstanced* in = [mesh instances];
        for (unsigned j = 0; j != mesh.instanceCount; ++j) {
            WryPathTraceMeshInstance m = {};
            m.model_transform = in[j].model_transform;
            m.inverse_model_transform = simd_inverse(in[j].model_transform);
            m.inverse_transpose_model_transform = in[j].inverse_transpose_model_transform;
            m.tint = in[j].albedo;
            m.mesh = (unsigned)k;
            Item it;
            wry::transform_box(m.model_transform, _meshLo[k], _meshHi[k], it.lo, it.hi);
            const std::vector<bool>& onscreen = *frame.mesh_onscreen[k];
            bool on = j < onscreen.size() ? (bool)onscreen[j] : true;
            it.code = (std::uint32_t)instances.size() | WRY_PT_ITEM_MESH | (on ? 0u : WRY_PT_ITEM_OFFSCREEN);
            items.push_back(it);
            instances.push_back(m);
        }
    }
    // The grid covers the items (a ship's modules may lie well beyond the
    // view), capped against a runaway
    simd_int2 lo = simd_make_int2(0, 0), hi = simd_make_int2(0, 0);
    bool any = false;
    std::vector<std::pair<simd_float3, simd_float3>> decal_boxes(decals.size());
    for (std::size_t i = 0; i != decals.size(); ++i)
        wry::transform_box(decals[i].model_transform, simd_make_float3(-0.5f, -0.5f, -0.5f),
                           simd_make_float3(0.5f, 0.5f, 0.5f), decal_boxes[i].first, decal_boxes[i].second);
    auto grow = [&](simd_float3 a, simd_float3 b) {
        simd_int2 ca = simd_make_int2((int)std::floor(a.x + 0.5f), (int)std::floor(a.y + 0.5f));
        simd_int2 cb = simd_make_int2((int)std::floor(b.x + 0.5f), (int)std::floor(b.y + 0.5f));
        lo = any ? simd_min(lo, ca) : ca;
        hi = any ? simd_max(hi, cb) : cb;
        any = true;
    };
    for (const Item& it : items)
        grow(it.lo, it.hi);
    for (auto const& [a, b] : decal_boxes)
        grow(a, b);
    {
        constexpr int kMaxGrid = 512;
        for (int axis = 0; axis != 2; ++axis) {
            if (hi[axis] - lo[axis] + 1 > kMaxGrid) {
                int c = (lo[axis] + hi[axis]) / 2;
                lo[axis] = c - kMaxGrid / 2;
                hi[axis] = lo[axis] + kMaxGrid - 1;
            }
        }
    }
    const simd_int2 grid_size = any ? hi - lo + 1 : simd_make_int2(1, 1);
    wry::PathTraceGridBuilder geometry(lo, grid_size);
    for (const Item& it : items)
        geometry.add(it.code, it.lo, it.hi);
    wry::PathTraceGrid grid = geometry.build();
    wry::PathTraceGridBuilder decal_builder(lo, grid_size);
    for (std::size_t i = 0; i != decals.size(); ++i)
        decal_builder.add((std::uint32_t)i, decal_boxes[i].first, decal_boxes[i].second);
    wry::PathTraceGrid decal_grid = decal_builder.build();

    // ---- The uniforms

    WryPathTraceUniforms u = {};
    u.camera_position_world = frame.camera_position_world;
    u.camera_ray_centre = frame.camera_ray_centre;
    u.camera_ray_dx = frame.camera_ray_dx;
    u.camera_ray_dy = frame.camera_ray_dy;
    u.light_direction = simd_make_float4(frame.light_direction, 0.0f);
    u.sun_irradiance = simd_make_float4(frame.sun_irradiance, 0.0f);
    u.environment_scale = simd_make_float4(frame.exposure, frame.exposure, frame.exposure, 0.0f);
    u.viewport = frame.viewport;
    u.grid_origin = grid.origin;
    u.grid_size = grid.size;
    u.grid_z_lo = grid.z_lo;
    u.grid_z_hi = grid.z_hi;
    if (grid.items.empty()) {
        u.grid_z_lo = 1.0f;     // empty: the kernel skips the walk
        u.grid_z_hi = 0.0f;
    }
    u.near_depth = frame.near_depth;
    u.far_depth = frame.far_depth;
    u.bounces = (unsigned)std::max(frame.bounces, 0);
    u.view = (unsigned)frame.view;
    u.antialias = frame.antialias ? 1u : 0u;
    u.print_count = (unsigned)frame.prints->size();

    // ---- Restart?  Every input of the image, field by field (the
    // instances' padding and the unread MeshInstanced fields are not
    // initialized), and the images' identities (a texture swapped in
    // later under the same name)

    std::uint64_t key = 0xcbf29ce484222325ull;
    {
        WryPathTraceUniforms k = u;
        k.row_begin = 0;
        k.samples = 0;
        // the view the resolve shows the beauty by traces the beauty
        if (k.view == WRY_PT_VIEW_ERROR)
            k.view = WRY_PT_VIEW_BEAUTY;
        key = hash_value(key, k);
        key = hash_value(key, frame.generation);
        const WryTerrainUniforms& b = frame.board;
        key = hash_value(key, b.origin);
        key = hash_value(key, b.size);
        key = hash_value(key, b.focus);
        key = hash_value(key, b.tint);
        key = hash_value(key, b.lattice);
        key = hash_value(key, b.line_color);
        key = hash_value(key, b.misregister);
        for (float f : {b.tint_blur, b.line_width, b.line_min_pixels, b.mark_bias, b.roughness})
            key = hash_value(key, f);
        key = wry::path_trace_hash(key, frame.kinds->data(), frame.kinds->size());
        key = wry::path_trace_hash(key, symbols.data(), symbols.size() * sizeof(WrySymbolInstance));
        constexpr std::size_t kDecalBytes = offsetof(WryDecalInstance, lod_bias) + sizeof(float);
        for (const WryDecalInstance& d : decals)
            key = wry::path_trace_hash(key, &d, kDecalBytes);
        for (const WryPathTraceMeshInstance& m : instances) {
            key = hash_value(key, m.model_transform);
            key = hash_value(key, m.inverse_transpose_model_transform);
            key = hash_value(key, m.tint);
            key = hash_value(key, m.mesh);
        }
        key = wry::path_trace_hash(key, grid.items.data(), grid.items.size() * sizeof(std::uint32_t));
        for (id t : {(id)frame.field, (id)frame.symbol_atlas, (id)frame.glyphs, (id)frame.decal_font, (id)environment})
            key = hash_pointer(key, t);
        for (int k = 0; k != WRY_PT_MAX_MESHES; ++k) {
            WryMesh* mesh = frame.meshes[k];
            for (id t : {(id)mesh.albedoTexture, (id)mesh.normalTexture, (id)mesh.roughnessTexture,
                         (id)mesh.metallicTexture, (id)mesh.emissiveTexture, (id)mesh.occlusionTexture})
                key = hash_pointer(key, t);
        }
    }
    const std::size_t pixels = (std::size_t)frame.viewport.x * (std::size_t)frame.viewport.y;
    if (!_accumulator || !simd_equal(_accumulatorSize, frame.viewport)) {
        _accumulator = [_ctx.device newBufferWithLength:std::max<std::size_t>(pixels, 1) * 2 * sizeof(simd_float4)
                                                options:MTLResourceStorageModeShared];
        _accumulator.label = @"Path trace accumulator";
        _accumulatorSize = frame.viewport;
        _haveKey = false;
    }
    if (!_haveKey || key != _key) {
        _haveKey = true;
        _key = key;
        _clear = true;
        _passes = 0;
        _phase = 0;
        ++progress.restarts;
        ++_streak;
        // measurements still in flight are of the old image
        _timing->epoch.fetch_add(1, std::memory_order_relaxed);
    } else {
        _streak = 0;
    }

    // ---- The dispatch: every row_stride-th row from the pass's phase,
    // each pixel `samples` more, sized to the budget from the measured
    // cost.  A pass's samples and stride are fixed until its phases have
    // covered every row, so every pixel ends it with the same count; the
    // rows interleave so that each frame's cost is a fair share and the
    // first frame after a restart already covers the screen (the resolve
    // fills the rows not traced yet from the traced row above)

    const int height = frame.viewport.y;
    if (_phase == 0 && _passes < (std::uint64_t)frame.target && height > 0) {
        // the offscreen host has no window
        const double budget = (NSApp && NSApp.windows.count) ? kWindowedBudget : kOffscreenBudget;
        // GPU seconds per row-sample, as last measured (none yet: an
        // eighth of the rows)
        const double per = _timing->per.load(std::memory_order_relaxed);
        const double fits = per > 0.0 ? budget / per : height / 8.0;
        int samples = 1;
        // the first pass after a restart takes one sample: the cost was
        // measured on another image, perhaps a much cheaper view
        if (_passes > 0 && fits >= height)
            samples = (int)std::clamp(fits / height, 1.0, (double)kMaxSamplesPerFrame);
        samples = (int)std::min<std::uint64_t>((std::uint64_t)samples, (std::uint64_t)frame.target - _passes);
        _passSamples = std::max(samples, 1);
        const double rows = std::max(fits / _passSamples, (double)kMinRowsPerFrame);
        _passStride = (int)std::clamp(std::ceil(height / rows), 1.0, (double)kMaxRowStride);
    }
    const bool trace = _passes < (std::uint64_t)frame.target && height > 0;

    if (_clear) {
        id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
        blit.label = @"Path trace restart";
        [blit fillBuffer:_accumulator range:NSMakeRange(0, _accumulator.length) value:0];
        [blit endEncoding];
        _clear = false;
    }

    progress.restart_streak = (std::uint64_t)_streak;
    // `path-trace-status` asked: answer from the sums as they stand (a
    // frame in flight may tear a few pixels, a restart's clear may not
    // have run: an estimate; pixels with under two samples do not count)
    if (progress.noise_answered < progress.noise_asked) {
        [self reportNoise:progress];
        progress.noise_answered = progress.noise_asked;
    }

    if (!trace) {
        progress.samples = _passes;
        return;
    }

    u.row_begin = (unsigned)_phase;
    u.row_stride = (unsigned)_passStride;
    u.samples = (unsigned)_passSamples;
    const int rows = (height - _phase + _passStride - 1) / _passStride;

    WryBufferArena* arena = _ctx.arena;
    auto slice = [&](const void* bytes, std::size_t length) {
        static const std::uint64_t zero[4] = {};
        return length ? [arena copyBytes:bytes length:length] : [arena copyBytes:zero length:sizeof(zero)];
    };
    WryBufferSlice kinds = slice(frame.kinds->data(), frame.kinds->size());
    WryBufferSlice symbol_slice = slice(symbols.data(), symbols.size() * sizeof(WrySymbolInstance));
    WryBufferSlice decal_slice = slice(decals.data(), decals.size() * sizeof(WryDecalInstance));
    WryBufferSlice instance_slice = slice(instances.data(), instances.size() * sizeof(WryPathTraceMeshInstance));
    WryBufferSlice cell_start = slice(grid.start.data(), grid.start.size() * sizeof(std::uint32_t));
    WryBufferSlice cell_items = slice(grid.items.data(), grid.items.size() * sizeof(std::uint32_t));
    WryBufferSlice cell_z = slice(grid.z.data(), grid.z.size() * sizeof(simd_float2));
    WryBufferSlice decal_start = slice(decal_grid.start.data(), decal_grid.start.size() * sizeof(std::uint32_t));
    WryBufferSlice decal_items = slice(decal_grid.items.data(), decal_grid.items.size() * sizeof(std::uint32_t));

    id<MTLComputeCommandEncoder> compute = [commandBuffer computeCommandEncoder];
    compute.label = @"Path trace";
    [compute setComputePipelineState:_kernel];
    [compute setBytes:&u length:sizeof(u) atIndex:WryPathTraceBufferUniforms];
    [compute setBytes:&frame.board length:sizeof(frame.board) atIndex:WryPathTraceBufferBoard];
    auto bind = [&](const WryBufferSlice& s, NSUInteger index) {
        [compute setBuffer:s.buffer offset:s.offset atIndex:index];
    };
    bind(kinds, WryPathTraceBufferKinds);
    bind(symbol_slice, WryPathTraceBufferSymbols);
    bind(decal_slice, WryPathTraceBufferDecals);
    bind(instance_slice, WryPathTraceBufferMeshInstances);
    [compute setBytes:_meshTable length:sizeof(_meshTable) atIndex:WryPathTraceBufferMeshes];
    [compute setBuffer:_nodes offset:0 atIndex:WryPathTraceBufferNodes];
    [compute setBuffer:_triangles offset:0 atIndex:WryPathTraceBufferTriangles];
    [compute setBuffer:_vertices offset:0 atIndex:WryPathTraceBufferVertices];
    bind(cell_start, WryPathTraceBufferCellStart);
    bind(cell_items, WryPathTraceBufferCellItems);
    bind(cell_z, WryPathTraceBufferCellZ);
    bind(decal_start, WryPathTraceBufferDecalStart);
    bind(decal_items, WryPathTraceBufferDecalItems);
    [compute setBuffer:_accumulator offset:0 atIndex:WryPathTraceBufferAccumulator];
    [compute setTexture:frame.field atIndex:WryPathTraceTextureField];
    [compute setTexture:frame.symbol_atlas atIndex:WryPathTraceTextureSymbols];
    [compute setTexture:frame.glyphs atIndex:WryPathTraceTextureGlyphs];
    [compute setTexture:(frame.decal_font ? frame.decal_font : _white) atIndex:WryPathTraceTextureDecalFont];
    [compute setTexture:environment atIndex:WryPathTraceTextureEnvironment];
    for (int k = 0; k != WRY_PT_MAX_MESHES; ++k) {
        WryMesh* mesh = frame.meshes[k];
        auto or_else = [](id<MTLTexture> t, id<MTLTexture> fallback) { return t ? t : fallback; };
        [compute setTexture:or_else(mesh.albedoTexture, _white) atIndex:WryPathTraceTextureMeshAlbedo + k];
        [compute setTexture:or_else(mesh.normalTexture, _flatNormal) atIndex:WryPathTraceTextureMeshNormal + k];
        [compute setTexture:or_else(mesh.roughnessTexture, _white) atIndex:WryPathTraceTextureMeshRoughness + k];
        [compute setTexture:or_else(mesh.metallicTexture, _black) atIndex:WryPathTraceTextureMeshMetallic + k];
        [compute setTexture:or_else(mesh.emissiveTexture, _black) atIndex:WryPathTraceTextureMeshEmissive + k];
        [compute setTexture:or_else(mesh.occlusionTexture, _white) atIndex:WryPathTraceTextureMeshOcclusion + k];
    }
    // Groups as wide as a SIMD group and as tall as the kernel allows
    const NSUInteger w = _kernel.threadExecutionWidth;
    const NSUInteger h = std::max<NSUInteger>(_kernel.maxTotalThreadsPerThreadgroup / w, 1);
    [compute dispatchThreads:MTLSizeMake((NSUInteger)frame.viewport.x, (NSUInteger)rows, 1)
       threadsPerThreadgroup:MTLSizeMake(w, std::min<NSUInteger>(h, 4), 1)];
    [compute endEncoding];

    // The time it takes, for the next frames' shapes (if the image is
    // still the same one by then)
    std::shared_ptr<Timing> timing = _timing;
    const std::uint64_t work = (std::uint64_t)rows * (std::uint64_t)_passSamples;
    const std::uint64_t epoch = _timing->epoch.load(std::memory_order_relaxed);
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
        double seconds = cb.GPUEndTime - cb.GPUStartTime;
        if (seconds > 0.0 && timing->epoch.load(std::memory_order_relaxed) == epoch) {
            timing->per.store(seconds / (double)work, std::memory_order_relaxed);
            timing->seconds.store(seconds, std::memory_order_relaxed);
        }
    }];

    progress.frame_ms = _timing->seconds.load(std::memory_order_relaxed) * 1000.0;
    progress.samples_per_frame = _passSamples;
    progress.rows_per_frame = rows;

    if (++_phase >= _passStride) {
        _phase = 0;
        _passes += (std::uint64_t)_passSamples;
    }
    progress.samples = _passes;
}

- (void)resolveWithEncoder:(id<MTLRenderCommandEncoder>)encoder {
    if (!_resolve || !_accumulator)
        return;
    WryPathTraceResolveUniforms r = {};
    r.viewport = _resolveViewport;
    r.view = (unsigned)_resolveView;
    r.row_stride = (unsigned)std::max(_passStride, 1);
    [encoder setRenderPipelineState:_resolve];
    [encoder setDepthStencilState:_ctx.disabledDepthStencilState];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setVertexBuffer:_ctx.screenTriangleStripVertexBuffer offset:0 atIndex:AAPLBufferIndexVertices];
    [encoder setFragmentBytes:&r length:sizeof(r) atIndex:AAPLBufferIndexUniforms];
    [encoder setFragmentBuffer:_accumulator offset:0 atIndex:WryPathTraceBufferAccumulator];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
}

@end
