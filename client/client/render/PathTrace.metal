//
//  PathTrace.metal
//  client
//
//  The path-traced reference renderer (docs/path_tracing.md).  One thread
//  per pixel of a band of rows adds `samples` paths to the pixel's running
//  sums; the resolve pass then writes the mean into the frame's colour.
//  The scene is the raster passes' own instance lists (WryPathTracer.mm
//  builds the grid over them), and the surfaces shade through
//  ShaderSurfaces.h, the code the raster passes shade with.
//
//  Fast math (-fmetal-math-mode=fast) is on for the whole library, so
//  nothing here relies on infinities or NaN compares: "no hit" distances
//  are the finite kFar, reciprocals of direction components are clamped,
//  and a non-finite sample is found by its exponent bits and dropped.
//

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

#include "ShaderTypes.h"
#include "PathTraceTypes.h"
#include "ShaderSurfaces.h"

namespace path_trace {

    using namespace deferred;

    constant float kFar = 1.0e30f;
    // Secondary rays start this far off the surface along its geometric
    // normal (world units: 0.13 slab texels, well above the march's 0.02
    // texel hit threshold, so a ray leaving a slab does not find it again
    // where it starts; far below anything the eye resolves)
    constant float kRayOffset = 1.0e-3f;
    // The slab march's step budget for light and shadow rays (the
    // primary rays keep the raster's 160, so silhouettes agree)
    constant int kSecondaryMarchSteps = 512;

    typedef array<texture2d<half>, WRY_PT_MAX_MESHES> MeshMaps;

    // ---- Random numbers: pcg4d (Jarzynski and Olano 2020) of (pixel,
    // sample, dimension), so an image is a pure function of the scene and
    // each pixel's sample count

    static uint4 pcg4d(uint4 v) {
        v = v * 1664525u + 1013904223u;
        v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
        v ^= v >> 16u;
        v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
        return v;
    }

    struct Random {
        uint2 pixel;
        uint sample;
        uint dimension;
        // Four uniforms in [0, 1) (24 bits each: never rounds to 1)
        float4 next() {
            uint4 h = pcg4d(uint4(pixel, sample, dimension++));
            return float4(h >> 8u) * (1.0f / 16777216.0f);
        }
    };

    // ---- The scene's buffers

    struct Scene {
        constant WryPathTraceUniforms* u;
        constant WryTerrainUniforms* board;
        const device uchar* kinds;
        const device WrySymbolInstance* symbols;
        const device WryDecalInstance* decals;
        const device WryPathTraceMeshInstance* instances;
        const device WryPathTraceMesh* meshes;
        const device WryPathTraceNode* nodes;
        const device WryPathTraceTriangle* triangles;
        const device MeshVertex* vertices;
        const device uint* cell_start;
        const device uint* cell_items;
        const device float2* cell_z;
        const device uint* decal_start;
        const device uint* decal_items;
    };

    enum HitKind { HIT_NONE, HIT_BOARD, HIT_SLAB, HIT_MESH };

    struct Hit {
        float t;            // world distance along the unit direction
        int kind;           // HitKind
        uint index;         // the slab or the mesh instance
        // the board: its cell and kind
        int2 cell;
        int terrain;
        // a slab: the march's answer, in slab space, and where the ray
        // entered the slab's cuboid (the raster's rasterized point)
        SymbolHit slab;
        float3 slab_entry;
        // a mesh: the triangle (its index among the mesh's input
        // triangles) and the barycentrics of its corners 1 and 2
        uint triangle;
        float2 barycentric;
    };

    // How a ray is traced
    struct Query {
        bool any;           // a shadow ray: the first hit found will do
        bool primary;       // a camera ray: pass what the raster would not draw
    };

    // ---- Intersection

    // The ray's distances inside the box [lo, hi], clipped to [t0, t1];
    // false if none.  inv_d is safe_reciprocal of the direction
    static bool box_clip(float3 lo, float3 hi, float3 o, float3 inv_d, thread float& t0, thread float& t1) {
        float3 ta = (lo - o) * inv_d;
        float3 tb = (hi - o) * inv_d;
        float3 tn = min(ta, tb), tf = max(ta, tb);
        t0 = max(t0, max(max(tn.x, tn.y), tn.z));
        t1 = min(t1, min(min(tf.x, tf.y), tf.z));
        return t0 <= t1;
    }

    // 1 / d with tiny components pushed to +-1e-20 (a finite 1e20)
    static float3 safe_reciprocal(float3 d) {
        float3 s = select(float3(-1.0f), float3(1.0f), d >= 0.0f);
        return 1.0f / select(d, s * 1e-20f, abs(d) < 1e-20f);
    }

    // A slab: the raster pass's march, in slab space (source texels; the
    // direction renormalized there, the distance converted back)
    static bool slab_intersect(Scene s, texture2d<float> field, uint index,
                               float3 o, float3 d, Query q, thread Hit& hit)
    {
        WrySymbolInstance instance = s.symbols[index];
        float3 os = (instance.inverse_model_transform * float4(o, 1.0f)).xyz;
        float3 ds = (instance.inverse_model_transform * float4(d, 0.0f)).xyz;
        float scale = length(ds);           // slab texels per world unit
        ds /= scale;
        SymbolHit h;
        if (!symbol_march(field, instance, os, ds, h, q.primary ? 160 : kSecondaryMarchSteps))
            return false;
        float t = dot(h.position - os, ds) / scale;
        if (!(t > 0.0f) || t >= hit.t)
            return false;
        hit.t = t;
        hit.kind = HIT_SLAB;
        hit.index = index;
        hit.slab = h;
        if (q.primary) {
            // where symbol_march's clip entered the cuboid
            float3 he = instance.half_extent.xyz;
            float3 inv = safe_reciprocal(ds);
            float t0 = 0.0f, t1 = kFar;
            box_clip(-he, he, os, inv, t0, t1);
            hit.slab_entry = os + ds * t0;
        }
        return true;
    }

    // Two-sided Moller-Trumbore, as path_trace_bvh.cpp's; `front` is
    // whether the ray meets the counter-clockwise (OBJ front) side
    static bool triangle_intersect(WryPathTraceTriangle tri, float3 o, float3 d, float t_max,
                                   thread float& t, thread float2& uv, thread bool& front)
    {
        float3 p0 = float3(tri.p0[0], tri.p0[1], tri.p0[2]);
        float3 e1 = float3(tri.p1[0], tri.p1[1], tri.p1[2]) - p0;
        float3 e2 = float3(tri.p2[0], tri.p2[1], tri.p2[2]) - p0;
        float3 pv = cross(d, e2);
        float det = dot(e1, pv);
        if (abs(det) < 1e-12f)
            return false;
        float inv = 1.0f / det;
        float3 tv = o - p0;
        float u = dot(tv, pv) * inv;
        if (u < 0.0f || u > 1.0f)
            return false;
        float3 qv = cross(tv, e1);
        float v = dot(d, qv) * inv;
        if (v < 0.0f || u + v > 1.0f)
            return false;
        float tt = dot(e2, qv) * inv;
        if (!(tt > 1e-6f) || tt >= t_max)
            return false;
        t = tt;
        uv = float2(u, v);
        // det = -d . (e1 x e2): positive where the ray meets the front
        front = det > 0.0f;
        return true;
    }

    // A mesh instance: its BVH, in object space.  The object-space
    // direction is not renormalized, so its distances are the world's.
    // Primary rays pass back faces, as the raster culls them; light and
    // shadow rays see both sides
    static bool mesh_intersect(Scene s, uint index, float3 o, float3 d, Query q, thread Hit& hit)
    {
        WryPathTraceMeshInstance instance = s.instances[index];
        WryPathTraceMesh mesh = s.meshes[instance.mesh];
        if (mesh.node_count == 0)
            return false;
        float3 om = (instance.inverse_model_transform * float4(o, 1.0f)).xyz;
        float3 dm = (instance.inverse_model_transform * float4(d, 0.0f)).xyz;
        float3 inv_d = safe_reciprocal(dm);
        const device WryPathTraceNode* nodes = s.nodes + mesh.node_offset;
        const device WryPathTraceTriangle* triangles = s.triangles + mesh.triangle_offset;
        uint stack[32];
        int top = 0;
        stack[top++] = 0;
        bool found = false;
        float best = hit.t;
        while (top > 0) {
            WryPathTraceNode n = nodes[stack[--top]];
            float t0 = 0.0f, t1 = best;
            if (!box_clip(float3(n.lo[0], n.lo[1], n.lo[2]), float3(n.hi[0], n.hi[1], n.hi[2]),
                          om, inv_d, t0, t1))
                continue;
            if (n.count) {
                for (uint i = n.first; i != n.first + n.count; ++i) {
                    WryPathTraceTriangle tri = triangles[i];
                    float t;
                    float2 uv;
                    bool front;
                    if (triangle_intersect(tri, om, dm, best, t, uv, front) && (front || !q.primary)) {
                        best = t;
                        found = true;
                        hit.triangle = tri.index;
                        hit.barycentric = uv;
                        if (q.any)
                            break;
                    }
                }
                if (q.any && found)
                    break;
            } else if (top + 2 <= 32) {
                stack[top++] = n.first;
                stack[top++] = n.first + 1;
            }
        }
        if (!found)
            return false;
        hit.t = best;
        hit.kind = HIT_MESH;
        hit.index = index;
        return true;
    }

    // The nearest hit of o + t d (d unit), t in (0, t_max).  The board is
    // the plane z = 0 where it has a kind (its holes, like the raster
    // pass's discards, let the ray through); everything else is in the
    // grid, walked cell by cell (Amanatides and Woo).  An item listed in
    // several cells may be found from a cell before its hit's own, so the
    // walk goes on until the best hit lies within the cell just walked
    static bool trace(Scene s, texture2d<float> field, float3 o, float3 d, float t_max,
                      Query q, thread Hit& hit)
    {
        constant WryPathTraceUniforms& u = *s.u;
        hit.kind = HIT_NONE;
        hit.t = t_max;

        if (d.z < 0.0f && o.z > 0.0f) {
            float t = -o.z / d.z;
            if (t < hit.t) {
                float2 p = o.xy + t * d.xy;
                int2 cell = int2(round(p));
                int k = terrain_kind(*s.board, s.kinds, cell);
                if (k != WRY_TERRAIN_KIND_NONE) {
                    if (q.any)
                        return true;
                    hit.t = t;
                    hit.kind = HIT_BOARD;
                    hit.cell = cell;
                    hit.terrain = clamp(k, 0, WRY_TERRAIN_KINDS - 1);
                }
            }
        }

        // The grid's box
        float3 inv_d = safe_reciprocal(d);
        float3 lo = float3(float2(u.grid_origin) - 0.5f, u.grid_z_lo);
        float3 hi = float3(float2(u.grid_origin + u.grid_size) - 0.5f, u.grid_z_hi);
        float t0 = 0.0f, t1 = hit.t;
        if (u.grid_z_lo > u.grid_z_hi || !box_clip(lo, hi, o, inv_d, t0, t1))
            return hit.kind != HIT_NONE;

        float2 entry = o.xy + t0 * d.xy;
        int2 cell = clamp(int2(floor(entry + 0.5f)) - u.grid_origin, int2(0), u.grid_size - 1);
        int2 step = int2(d.x >= 0.0f ? 1 : -1, d.y >= 0.0f ? 1 : -1);
        // the distances to the cell's next boundary on each axis, and
        // between boundaries
        float2 boundary = float2(cell + u.grid_origin) + 0.5f * float2(step);
        bool2 still = abs(d.xy) < 1e-12f;
        float2 t_next = select((boundary - o.xy) * inv_d.xy, float2(kFar), still);
        float2 t_delta = select(abs(inv_d.xy), float2(kFar), still);
        float t_cell = t0;
        const uint skip = q.primary ? WRY_PT_ITEM_OFFSCREEN : 0u;
        for (int guard = 0; guard != 4096; ++guard) {
            float t_exit = min(min(t_next.x, t_next.y), t1);
            uint c = (uint)(cell.y * u.grid_size.x + cell.x);
            uint begin = s.cell_start[c], end = s.cell_start[c + 1];
            if (begin != end) {
                // The ray's heights across the cell, against its items'
                float2 cz = s.cell_z[c];
                float za = o.z + t_cell * d.z, zb = o.z + t_exit * d.z;
                if (max(za, zb) >= cz.x && min(za, zb) <= cz.y) {
                    for (uint i = begin; i != end; ++i) {
                        uint item = s.cell_items[i];
                        if (item & skip)
                            continue;
                        uint index = item & WRY_PT_ITEM_INDEX;
                        bool h = (item & WRY_PT_ITEM_MESH)
                            ? mesh_intersect(s, index, o, d, q, hit)
                            : slab_intersect(s, field, index, o, d, q, hit);
                        if (h && q.any)
                            return true;
                    }
                }
            }
            if (hit.t <= t_exit || t_exit >= t1)
                break;
            t_cell = t_exit;
            if (t_next.x < t_next.y) {
                cell.x += step.x;
                t_next.x += t_delta.x;
                if (cell.x < 0 || cell.x >= u.grid_size.x)
                    break;
            } else {
                cell.y += step.y;
                t_next.y += t_delta.y;
                if (cell.y < 0 || cell.y >= u.grid_size.y)
                    break;
            }
        }
        return hit.kind != HIT_NONE;
    }

    // ---- Surfaces

    // The surface's BRDF inputs, as the G-buffer would hold them
    struct Surface {
        float3 albedo;
        float3 normal;      // shading, unit, on the incoming ray's side
        float3 geometric;   // unit, on the incoming ray's side
        float roughness;
        float metallic;
        float clear_coat;
        float occlusion;    // a mesh's baked occlusion (detail its geometry lacks)
        float3 emission;
    };

    // The primary ray's footprint on a plane through the hit: the surface
    // point's steps per pixel across and down the screen (ray
    // differentials); zero for an unfiltered point sample.  `ray` is the
    // unnormalized ray (unit along the view axis) and t_ray the hit's
    // parameter along it; dDdx, dDdy its steps per pixel
    struct Footprint {
        float3 dx;
        float3 dy;
    };

    static Footprint footprint_on_plane(float3 ray, float t_ray, float3 dDdx, float3 dDdy, float3 n) {
        float dn = dot(ray, n);
        Footprint f = { float3(0.0f), float3(0.0f) };
        if (abs(dn) < 1e-6f)
            return f;
        f.dx = t_ray * (dDdx - ray * (dot(dDdx, n) / dn));
        f.dy = t_ray * (dDdy - ray * (dot(dDdy, n) / dn));
        return f;
    }

    // The G-buffer's Material attachment is 8-bit unorm
    static float quantize8(float x) {
        return round(saturate(x) * 255.0f) / 255.0f;
    }

    static float3 sky(constant WryPathTraceUniforms& u, texture2d<half> environment, float3 d) {
        constexpr sampler bilinear(mag_filter::linear,
                                   min_filter::linear,
                                   s_address::repeat,
                                   t_address::clamp_to_edge);
        // The prefilter's convention (CubeFilterAccumulate3): u around +z
        // from +x, v down from the zenith
        float2 uv = float2(atan2(d.y, d.x) * (0.5f * M_1_PI_F),
                           acos(clamp(d.z, -1.0f, 1.0f)) * M_1_PI_F);
        return float3(environment.sample(bilinear, uv, level(0.0f)).rgb) * u.environment_scale.rgb;
    }

    // The surface at a hit of o + t d (d unit), its decals applied.
    // `filtered`: the primary hit, given its unnormalized ray and the
    // ray's steps per pixel, filters as the raster pass would
    static Surface shade(Scene s, Hit hit, float3 o, float3 d,
                         bool filtered, float3 ray, float3 dDdx, float3 dDdy,
                         texture2d<float> field,
                         texture2d<half> symbol_atlas,
                         texture2d<half> glyphs,
                         texture2d<half> decal_font,
                         MeshMaps mesh_albedo, MeshMaps mesh_normal, MeshMaps mesh_roughness,
                         MeshMaps mesh_metallic, MeshMaps mesh_emissive, MeshMaps mesh_occlusion)
    {
        constant WryPathTraceUniforms& u = *s.u;
        float3 p = o + hit.t * d;
        // the hit's parameter along the unnormalized ray from the eye
        // (filtered only: the primary ray starts on the near plane, and
        // the neighbouring pixels' rays all start at the eye)
        float t_ray = filtered ? u.near_depth + hit.t / length(ray) : 0.0f;
        Surface surface;
        surface.emission = float3(0.0f);
        surface.clear_coat = 0.0f;
        surface.metallic = 0.0f;
        surface.occlusion = 1.0f;
        Footprint fp = { float3(0.0f), float3(0.0f) };

        if (hit.kind == HIT_BOARD) {
            surface.normal = float3(0.0f, 0.0f, 1.0f);
            surface.geometric = surface.normal;
            if (filtered)
                fp = footprint_on_plane(ray, t_ray, dDdx, dDdy, surface.normal);
            // the raster's fwidth(p) (|dfdx| + |dfdy|), floored as it floors it
            float2 wpp = max(abs(fp.dx.xy) + abs(fp.dy.xy), 1e-5f);
            surface.albedo = terrain_albedo(*s.board, s.kinds, field, p.xy, hit.cell, hit.terrain, wpp);
            surface.roughness = quantize8(s.board->roughness);
        } else if (hit.kind == HIT_SLAB) {
            constexpr sampler trilinear(mag_filter::linear,
                                        min_filter::linear,
                                        mip_filter::linear,
                                        s_address::clamp_to_edge,
                                        t_address::clamp_to_edge);
            WrySymbolInstance instance = s.symbols[hit.index];
            surface.normal = normalize((instance.model_transform * float4(hit.slab.normal, 0.0f)).xyz);
            surface.geometric = surface.normal;
            float lod = 0.0f;
            if (filtered) {
                // The raster's lod is fwidth of the rasterized cuboid point,
                // the ray's entry into the cuboid: its steps on the entry
                // face's plane, in slab space
                float3 rs = (instance.inverse_model_transform * float4(ray, 0.0f)).xyz;
                float3 xs = (instance.inverse_model_transform * float4(dDdx, 0.0f)).xyz;
                float3 ys = (instance.inverse_model_transform * float4(dDdy, 0.0f)).xyz;
                float3 os = (instance.inverse_model_transform * u.camera_position_world).xyz;
                float3 e = hit.slab_entry;
                float3 he = instance.half_extent.xyz;
                float3 off = abs(abs(e) - he);
                // the face it entered: the axis it sits on
                float3 n = (off.x <= off.y && off.x <= off.z) ? float3(sign(e.x), 0.0f, 0.0f)
                         : (off.y <= off.z) ? float3(0.0f, sign(e.y), 0.0f)
                         : float3(0.0f, 0.0f, sign(e.z));
                float dn = dot(rs, n);
                if (hit.slab.print && abs(dn) > 1e-8f) {
                    float te = dot(e - os, n) / dn;
                    float3 fx = te * (xs - rs * (dot(xs, n) / dn));
                    float3 fy = te * (ys - rs * (dot(ys, n) / dn));
                    float2 w = abs(fx.xy) + abs(fy.xy);
                    lod = max(log2(max(max(w.x, w.y), 1e-8f)), 0.0f);
                }
                fp = footprint_on_plane(ray, t_ray, dDdx, dDdy, surface.normal);
            }
            surface.albedo = float3(symbol_atlas.sample(trilinear, hit.slab.uv, level(lod)).rgb);
            surface.roughness = quantize8(0.5f);
            surface.clear_coat = quantize8(instance.clear_coat);
        } else {
            constexpr sampler trilinear(mag_filter::linear,
                                        min_filter::linear,
                                        mip_filter::linear,
                                        s_address::repeat,
                                        t_address::repeat);
            WryPathTraceMeshInstance instance = s.instances[hit.index];
            WryPathTraceMesh mesh = s.meshes[instance.mesh];
            uint base = mesh.vertex_offset + 3u * hit.triangle;
            MeshVertex v0 = s.vertices[base], v1 = s.vertices[base + 1], v2 = s.vertices[base + 2];
            float b1 = hit.barycentric.x, b2 = hit.barycentric.y, b0 = 1.0f - b1 - b2;
            float2 uv = b0 * v0.coordinate.xy + b1 * v1.coordinate.xy + b2 * v2.coordinate.xy;
            float4x4 it = instance.inverse_transpose_model_transform;
            // As the raster interpolates them: transformed, not normalized
            float3 T = (it * (b0 * v0.tangent + b1 * v1.tangent + b2 * v2.tangent)).xyz;
            float3 B = (it * (b0 * v0.bitangent + b1 * v1.bitangent + b2 * v2.bitangent)).xyz;
            float3 N = (it * (b0 * v0.normal + b1 * v1.normal + b2 * v2.normal)).xyz;
            // The face (OBJ counter-clockwise is the front), in the world
            float3 e1 = v1.position.xyz - v0.position.xyz;
            float3 e2 = v2.position.xyz - v0.position.xyz;
            float3 face = cross(e1, e2);
            float3 ng = normalize((it * float4(face, 0.0f)).xyz);
            uint slot = min(instance.mesh, (uint)WRY_PT_MAX_MESHES - 1);
            float2 duvdx = float2(0.0f), duvdy = float2(0.0f);
            if (filtered) {
                fp = footprint_on_plane(ray, t_ray, dDdx, dDdy, ng);
                // the barycentric steps of the footprint, in object space
                float3 ox = (instance.inverse_model_transform * float4(fp.dx, 0.0f)).xyz;
                float3 oy = (instance.inverse_model_transform * float4(fp.dy, 0.0f)).xyz;
                float nn = max(dot(face, face), 1e-20f);
                float2 bx = float2(dot(cross(ox, e2), face), dot(cross(e1, ox), face)) / nn;
                float2 by = float2(dot(cross(oy, e2), face), dot(cross(e1, oy), face)) / nn;
                float2 du1 = v1.coordinate.xy - v0.coordinate.xy, du2 = v2.coordinate.xy - v0.coordinate.xy;
                duvdx = bx.x * du1 + bx.y * du2;
                duvdy = by.x * du1 + by.y * du2;
            }
            gradient2d g(duvdx, duvdy);
            float3 albedo = float3(mesh_albedo[slot].sample(trilinear, uv, g).rgb);
            float3 nt = float3(mesh_normal[slot].sample(trilinear, uv, g).xyz) * 2.0f - 1.0f;
            surface.normal = normalize(float3x3(T, B, N) * nt);
            surface.albedo = albedo * instance.tint.rgb;
            surface.roughness = quantize8(float(mesh_roughness[slot].sample(trilinear, uv, g).g));
            surface.metallic = quantize8(float(mesh_metallic[slot].sample(trilinear, uv, g).b));
            surface.emission = float3(mesh_emissive[slot].sample(trilinear, uv, g).rgb);
            surface.occlusion = float(mesh_occlusion[slot].sample(trilinear, uv, g).r);
            surface.geometric = ng;
            // A back face (light and shadow rays only): a thin sheet, lit
            // on the side the ray meets
            if (dot(ng, d) > 0.0f) {
                surface.geometric = -ng;
                surface.normal = -surface.normal;
            }
        }

        // The decals over this point, in the raster pass's order (the
        // prints, then the strings), each seeing the surface as the ones
        // before left it, at the G-buffer's half precision
        int2 gc = int2(floor(p.xy + 0.5f)) - u.grid_origin;
        if (all(gc >= 0) && all(gc < u.grid_size)) {
            uint c = (uint)(gc.y * u.grid_size.x + gc.x);
            uint begin = s.decal_start[c], end = s.decal_start[c + 1];
            if (begin != end) {
                DecalTarget target = { half4(half3(surface.emission), 0.0h),
                                       half4(half3(surface.albedo), 1.0h),
                                       half4(half3(surface.normal), 0.0h),
                                       half4(1.0h, half(surface.roughness), half(surface.metallic), half(surface.clear_coat)) };
                bool painted = false;
                for (uint i = begin; i != end; ++i) {
                    uint k = s.decal_items[i];
                    WryDecalInstance decal = s.decals[k];
                    float3 q = (decal.inverse_model_transform * float4(p, 1.0f)).xyz;
                    if (any(abs(q) > 0.5f))
                        continue;
                    float3 axis = normalize(decal.model_transform.columns[2].xyz);
                    if (dot(float3(target.normal.xyz), axis) < decal.normal_cutoff)
                        continue;
                    float2 uv = decal_uv(decal, q);
                    // the raster's dfdx / dfdy of uv: the footprint in the
                    // decal's frame (decal_paint adds the lod bias)
                    float3 qx = (decal.inverse_model_transform * float4(fp.dx, 0.0f)).xyz;
                    float3 qy = (decal.inverse_model_transform * float4(fp.dy, 0.0f)).xyz;
                    float2 duvdx = float2(qx.x, -qx.y) * decal.uv_rect.zw;
                    float2 duvdy = float2(qy.x, -qy.y) * decal.uv_rect.zw;
                    if (k < u.print_count)
                        decal_paint(decal, glyphs, uv, duvdx, duvdy, target);
                    else
                        decal_paint(decal, decal_font, uv, duvdx, duvdy, target);
                    painted = true;
                }
                if (painted) {
                    surface.emission = float3(target.light.rgb);
                    surface.albedo = float3(target.albedo.rgb);
                    surface.normal = normalize(float3(target.normal.xyz));
                    surface.roughness = quantize8(float(target.material.g));
                    surface.metallic = quantize8(float(target.material.b));
                    surface.occlusion *= float(target.material.r);
                }
            }
        }
        return surface;
    }

    // ---- Sampling brdf_cosine: a mixture of the cosine lobe and GGX
    // visible normals for the base and for the coat, each picked with a
    // probability that depends on V and the material only; the mixture's
    // pdf, every lobe evaluated at the chosen direction, is the weight's
    // denominator

    // An orthonormal basis with z = n (Duff et al. 2017)
    static float3x3 basis(float3 n) {
        float sgn = n.z >= 0.0f ? 1.0f : -1.0f;
        float a = -1.0f / (sgn + n.z);
        float b = n.x * n.y * a;
        float3 t = float3(1.0f + sgn * n.x * n.x * a, sgn * b, -sgn * n.x);
        float3 bt = float3(b, sgn + n.y * n.y * a, -n.y);
        return float3x3(t, bt, n);
    }

    // A GGX visible normal (Heitz 2018) in the local frame z = n; alpha =
    // roughness^2, as distribution_Trowbridge_Reitz takes alpha^2
    static float3 sample_vndf(float3 v, float alpha, float2 xi) {
        float3 vh = normalize(float3(alpha * v.x, alpha * v.y, v.z));
        float lensq = vh.x * vh.x + vh.y * vh.y;
        float3 t1 = lensq > 1e-12f ? float3(-vh.y, vh.x, 0.0f) * rsqrt(lensq) : float3(1.0f, 0.0f, 0.0f);
        float3 t2 = cross(vh, t1);
        float r = sqrt(xi.x);
        float phi = 2.0f * M_PI_F * xi.y;
        float a = r * cos(phi);
        float b = r * sin(phi);
        float sh = 0.5f * (1.0f + vh.z);
        b = (1.0f - sh) * sqrt(max(1.0f - a * a, 0.0f)) + sh * b;
        float3 nh = a * t1 + b * t2 + sqrt(max(1.0f - a * a - b * b, 0.0f)) * vh;
        return normalize(float3(alpha * nh.x, alpha * nh.y, max(nh.z, 1e-6f)));
    }

    // Smith's G1 for GGX (the visible normals' own; the BRDF's G is
    // Schlick-Smith, which only the weight sees)
    static float smith_g1(float NdotV, float alpha) {
        float a2 = alpha * alpha;
        return 2.0f * NdotV / (NdotV + sqrt(a2 + (1.0f - a2) * NdotV * NdotV));
    }

    // The solid-angle pdf of reflecting v to l through a visible normal
    static float vndf_pdf(float NdotV, float NdotH, float alpha) {
        float D = distribution_Trowbridge_Reitz(NdotH, alpha * alpha);
        return D * smith_g1(NdotV, alpha) / (4.0f * NdotV);
    }

    static float luminance(float3 c) {
        return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
    }

    struct Lobes {
        float diffuse, base, coat;   // probabilities, summing to 1
        float alpha, coat_alpha;
    };

    // The lobes' shares at a vertex: by estimated reflectance; with V
    // below the shading normal only the cosine lobe (brdf_cosine's
    // specular is then zero, G1(0) = 0, and what is left lives on
    // NdotL > 0, which the cosine lobe covers)
    static Lobes lobes(Surface s, float NdotV) {
        Lobes w;
        w.alpha = max(s.roughness * s.roughness, 1e-3f);
        w.coat_alpha = kClearCoatRoughness * kClearCoatRoughness;
        if (NdotV <= 1e-4f) {
            w.diffuse = 1.0f;
            w.base = w.coat = 0.0f;
            return w;
        }
        float3 F0 = mix(float3(0.04f), s.albedo, s.metallic);
        float3 F = Fresnel_Schlick(NdotV, F0);
        w.diffuse = (1.0f - s.metallic) * luminance(s.albedo * (1.0f - F));
        w.base = luminance(F);
        w.coat = s.clear_coat * Fresnel_Schlick(NdotV, float3(kClearCoatF0)).x;
        // keep some of the cosine lobe (the coat and the board's shallow
        // lobes reflect off-specular too)
        w.diffuse = max(w.diffuse, 0.1f * (w.base + w.coat));
        float sum = w.diffuse + w.base + w.coat;
        w.diffuse /= sum;
        w.base /= sum;
        w.coat /= sum;
        return w;
    }

    static float mixture_pdf(Lobes w, float3 n, float3 v, float3 l) {
        float NdotL = dot(n, l);
        if (NdotL <= 0.0f)
            return 0.0f;
        float p = w.diffuse * NdotL * M_1_PI_F;
        float NdotV = dot(n, v);
        if (NdotV > 0.0f && (w.base > 0.0f || w.coat > 0.0f)) {
            float NdotH = saturate(dot(n, normalize(v + l)));
            if (w.base > 0.0f)
                p += w.base * vndf_pdf(NdotV, NdotH, w.alpha);
            if (w.coat > 0.0f)
                p += w.coat * vndf_pdf(NdotV, NdotH, w.coat_alpha);
        }
        return p;
    }

    // Whether a float3 holds an infinity or a NaN, by its bits (fast math
    // may fold isfinite)
    static bool non_finite(float3 v) {
        uint3 b = as_type<uint3>(v) & 0x7f800000u;
        return any(b == 0x7f800000u);
    }

    // ---- The kernel

    [[kernel]] void
    path_trace_kernel(uint2 gid [[thread_position_in_grid]],
                      constant WryPathTraceUniforms& u [[buffer(WryPathTraceBufferUniforms)]],
                      constant WryTerrainUniforms& board [[buffer(WryPathTraceBufferBoard)]],
                      const device uchar* kinds [[buffer(WryPathTraceBufferKinds)]],
                      const device WrySymbolInstance* symbols [[buffer(WryPathTraceBufferSymbols)]],
                      const device WryDecalInstance* decals [[buffer(WryPathTraceBufferDecals)]],
                      const device WryPathTraceMeshInstance* instances [[buffer(WryPathTraceBufferMeshInstances)]],
                      const device WryPathTraceMesh* meshes [[buffer(WryPathTraceBufferMeshes)]],
                      const device WryPathTraceNode* nodes [[buffer(WryPathTraceBufferNodes)]],
                      const device WryPathTraceTriangle* triangles [[buffer(WryPathTraceBufferTriangles)]],
                      const device MeshVertex* vertices [[buffer(WryPathTraceBufferVertices)]],
                      const device uint* cell_start [[buffer(WryPathTraceBufferCellStart)]],
                      const device uint* cell_items [[buffer(WryPathTraceBufferCellItems)]],
                      const device float2* cell_z [[buffer(WryPathTraceBufferCellZ)]],
                      const device uint* decal_start [[buffer(WryPathTraceBufferDecalStart)]],
                      const device uint* decal_items [[buffer(WryPathTraceBufferDecalItems)]],
                      device float4* accumulator [[buffer(WryPathTraceBufferAccumulator)]],
                      texture2d<float> field [[texture(WryPathTraceTextureField)]],
                      texture2d<half> symbol_atlas [[texture(WryPathTraceTextureSymbols)]],
                      texture2d<half> glyphs [[texture(WryPathTraceTextureGlyphs)]],
                      texture2d<half> decal_font [[texture(WryPathTraceTextureDecalFont)]],
                      texture2d<half> environment [[texture(WryPathTraceTextureEnvironment)]],
                      MeshMaps mesh_albedo [[texture(WryPathTraceTextureMeshAlbedo)]],
                      MeshMaps mesh_normal [[texture(WryPathTraceTextureMeshNormal)]],
                      MeshMaps mesh_roughness [[texture(WryPathTraceTextureMeshRoughness)]],
                      MeshMaps mesh_metallic [[texture(WryPathTraceTextureMeshMetallic)]],
                      MeshMaps mesh_emissive [[texture(WryPathTraceTextureMeshEmissive)]],
                      MeshMaps mesh_occlusion [[texture(WryPathTraceTextureMeshOcclusion)]])
    {
        const uint2 pixel = uint2(gid.x, u.row_begin + gid.y * u.row_stride);
        if (pixel.x >= (uint)u.viewport.x || pixel.y >= (uint)u.viewport.y)
            return;

        Scene s = { &u, &board, kinds, symbols, decals, instances, meshes, nodes, triangles, vertices,
                    cell_start, cell_items, cell_z, decal_start, decal_items };

        const uint slot = 2u * (pixel.y * (uint)u.viewport.x + pixel.x);
        float4 sum = accumulator[slot];
        float4 squares = accumulator[slot + 1];
        const float2 viewport = float2(u.viewport);
        const float3 sun = u.light_direction.xyz;
        // The ray's steps per pixel across and down (pixel rows run down
        // the screen, NDC y up)
        const float3 dDdx = u.camera_ray_dx.xyz * (2.0f / viewport.x);
        const float3 dDdy = -u.camera_ray_dy.xyz * (2.0f / viewport.y);

        // this pixel's next sample number: how many it has had (kept and
        // dropped), read before the loop below adds to them
        const uint base = (uint)(sum.w + squares.w);
        for (uint n = 0; n != u.samples; ++n) {
            Random rng = { pixel, base + n, 0u };
            float4 xi0 = rng.next();
            float2 jitter = u.antialias ? xi0.xy : float2(0.5f);
            float2 ndc = float2(2.0f * (float(pixel.x) + jitter.x) / viewport.x - 1.0f,
                                1.0f - 2.0f * (float(pixel.y) + jitter.y) / viewport.y);
            float3 ray = u.camera_ray_centre.xyz + ndc.x * u.camera_ray_dx.xyz + ndc.y * u.camera_ray_dy.xyz;

            // The primary ray runs between the raster's clip planes: ray
            // has unit component along the view axis, so depth is its
            // parameter
            float3 o = u.camera_position_world.xyz + ray * u.near_depth;
            float3 d = normalize(ray);
            float t_max = (u.far_depth - u.near_depth) * length(ray);
            float3 throughput = float3(1.0f);
            float3 radiance = float3(0.0f);
            float3 direct = float3(0.0f);       // emission and the sun at the first hit
            float3 sky_seen = float3(0.0f);     // the sky the first bounce reaches
            float3 sky_open = float3(0.0f);     // the same, nothing in the way

            for (uint bounce = 0; bounce <= u.bounces; ++bounce) {
                const bool primary = bounce == 0;
                Hit hit;
                Query q = { false, primary };
                if (!trace(s, field, o, d, primary ? t_max : kFar, q, hit)) {
                    float3 L = throughput * sky(u, environment, d);
                    radiance += L;
                    if (bounce == 1)
                        sky_seen = L;
                    break;
                }
                Surface surface = shade(s, hit, o, d, primary && !u.antialias, ray, dDdx, dDdy,
                                        field, symbol_atlas, glyphs, decal_font,
                                        mesh_albedo, mesh_normal, mesh_roughness, mesh_metallic,
                                        mesh_emissive, mesh_occlusion);
                if (primary && u.view == WRY_PT_VIEW_ALBEDO) {
                    radiance = surface.albedo;
                    break;
                }
                if (primary && u.view == WRY_PT_VIEW_NORMAL) {
                    radiance = surface.normal * 0.5f + 0.5f;
                    break;
                }
                float3 p = o + hit.t * d;
                float3 V = -d;
                float3 Ng = surface.geometric;
                float3 N = surface.normal;
                float3 start = p + Ng * kRayOffset;

                if (primary && u.view == WRY_PT_VIEW_OCCLUSION) {
                    // A cosine direction about the geometric normal: does
                    // it reach the sky?
                    float4 xi = rng.next();
                    float r = sqrt(xi.x), phi = 2.0f * M_PI_F * xi.y;
                    float3 l = basis(Ng) * float3(r * cos(phi), r * sin(phi), sqrt(max(1.0f - xi.x, 0.0f)));
                    Hit blocker;
                    Query sq = { true, false };
                    radiance = float3(trace(s, field, start, l, kFar, sq, blocker) ? 0.0f : 1.0f);
                    break;
                }

                // Emission, as the raster pass writes it (not exposed)
                float3 emitted = throughput * surface.emission;
                // The sun, a delta light: its shadow ray alone.  Only where
                // it is above both normals (a shading normal tilted past
                // the face would light the inside)
                float3 lit = float3(0.0f);
                if (dot(Ng, sun) > 0.0f && dot(N, sun) > 0.0f) {
                    Hit blocker;
                    Query sq = { true, false };
                    if (!trace(s, field, start, sun, kFar, sq, blocker))
                        lit = throughput * brdf_cosine(surface.albedo, N, V, sun, surface.roughness,
                                                       surface.metallic, surface.clear_coat)
                                         * u.sun_irradiance.rgb;
                }
                radiance += emitted + lit;
                if (primary) {
                    direct = emitted + lit;
                    if (u.view == WRY_PT_VIEW_DIRECT)
                        break;
                }
                if (bounce == u.bounces)
                    break;

                // The next direction, from the mixture
                float NdotV = dot(N, V);
                Lobes w = lobes(surface, NdotV);
                float4 xi = rng.next();
                float3x3 frame = basis(N);
                float3 l;
                if (xi.z < w.diffuse) {
                    // cosine (Malley)
                    float r = sqrt(xi.x), phi = 2.0f * M_PI_F * xi.y;
                    l = frame * float3(r * cos(phi), r * sin(phi), sqrt(max(1.0f - xi.x, 0.0f)));
                } else {
                    float alpha = xi.z < w.diffuse + w.base ? w.alpha : w.coat_alpha;
                    float3 h = frame * sample_vndf(transpose(frame) * V, alpha, xi.xy);
                    l = reflect(-V, h);
                }
                float pdf = mixture_pdf(w, N, V, l);
                // Into the surface (below the face, or a shading normal
                // tilted past it), or a direction the mixture cannot make:
                // the path ends
                if (dot(l, Ng) <= 0.0f || !(pdf > 0.0f))
                    break;
                // Baked occlusion stands for detail the geometry lacks: it
                // dims the light arriving by bounces, never the sun
                throughput *= brdf_cosine(surface.albedo, N, V, l, surface.roughness,
                                          surface.metallic, surface.clear_coat)
                            * (surface.occlusion / pdf);
                if (primary) {
                    sky_open = throughput * sky(u, environment, l);
                    if (u.view == WRY_PT_VIEW_SKY_OPEN)
                        break;
                }
                // Russian roulette from the fourth vertex
                if (bounce >= 3) {
                    float survive = min(max(max(throughput.r, throughput.g), throughput.b), 0.95f);
                    if (xi.w >= survive)
                        break;
                    throughput /= survive;
                }
                o = start;
                d = l;
                if (primary && u.view == WRY_PT_VIEW_SKY) {
                    // the first bounce only: its sky, if it gets there
                    Hit blocker;
                    Query sq = { true, false };
                    if (!trace(s, field, o, d, kFar, sq, blocker))
                        sky_seen = sky_open;
                    break;
                }
            }
            switch (u.view) {
                case WRY_PT_VIEW_DIRECT:   radiance = direct; break;
                case WRY_PT_VIEW_AMBIENT:  radiance -= direct; break;
                case WRY_PT_VIEW_SKY:      radiance = sky_seen; break;
                case WRY_PT_VIEW_SKY_OPEN: radiance = sky_open; break;
                case WRY_PT_VIEW_INDIRECT: radiance -= direct + sky_seen; break;
                default: break;
            }
            if (non_finite(radiance)) {
                squares.w += 1.0f;      // dropped, but counted toward the sample number
                continue;
            }
            sum += float4(radiance, 1.0f);
            squares.xyz += radiance * radiance;
        }

        accumulator[slot] = sum;
        accumulator[slot + 1] = squares;
    }

    // ---- The resolve: the accumulator's mean (or its standard error)
    // into the frame's colour, a full-screen strip drawn in the composite
    // pass under the GUI.  Its colour is written in the Lighting raster
    // order group, as the composite's and the GUI's are: a write outside
    // the group is not ordered against the GUI's blends, which then lose
    // whole tiles to it

    struct ResolveVertexOutput {
        float4 position [[position]];
    };

    [[vertex]] ResolveVertexOutput
    resolve_vertex(uint vertex_id [[vertex_id]],
                   const device float4* vertices [[buffer(AAPLBufferIndexVertices)]])
    {
        ResolveVertexOutput out;
        out.position = vertices[vertex_id];
        return out;
    }

    struct ResolveFragmentOutput {
        half4 color [[color(AAPLColorIndexColor), raster_order_group(AAPLRasterOrderGroupLighting)]];
    };

    [[fragment]] ResolveFragmentOutput
    resolve_fragment(ResolveVertexOutput in [[stage_in]],
                     constant WryPathTraceResolveUniforms& u [[buffer(AAPLBufferIndexUniforms)]],
                     const device float4* accumulator [[buffer(WryPathTraceBufferAccumulator)]])
    {
        uint2 p = min(uint2(in.position.xy), uint2(u.viewport) - 1);
        uint slot = 2u * (p.y * (uint)u.viewport.x + p.x);
        float4 sum = accumulator[slot];
        // Not traced yet (the first pass after a restart covers the rows
        // a stride apart, frame by frame): the nearest traced row above
        for (uint k = 1; sum.w == 0.0f && k < u.row_stride && k <= p.y; ++k) {
            slot = 2u * ((p.y - k) * (uint)u.viewport.x + p.x);
            sum = accumulator[slot];
        }
        float n = max(sum.w, 1.0f);
        float3 mean = sum.rgb / n;
        float3 value = mean;
        if (u.view == WRY_PT_VIEW_ERROR) {
            float3 variance = max(accumulator[slot + 1].rgb / n - mean * mean, 0.0f) * (n / max(n - 1.0f, 1.0f));
            value = sqrt(variance / n);
        }
        ResolveFragmentOutput out;
        out.color = half4(half3(clamp(value, 0.0f, float(HALF_MAX))), 1.0h);
        return out;
    }

} // namespace path_trace
