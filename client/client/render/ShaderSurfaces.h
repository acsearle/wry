//
//  ShaderSurfaces.h
//  client
//
//  Metal only: the surface code the raster passes (Shaders.metal) and the
//  path tracer (PathTrace.metal) share, so the reference renderer shades
//  exactly the scene the fast one draws -- the BRDF of the direct light,
//  the symbol slabs' field and march, the board's ink as a function of the
//  ground point, and the decals' paint as a function of a surface point.
//  Each .metal file is compiled to its own AIR and the two are linked into
//  one library, so everything here has internal linkage (static).
//  docs/path_tracing.md.
//

#ifndef ShaderSurfaces_h
#define ShaderSurfaces_h

#include <metal_stdlib>
#include "ShaderTypes.h"

using namespace metal;

// ---- The BRDF of the direct light (directional_lighting_fragment_function)

// Microfacet distribution

// Determines reflection sharpness and highlight size

static inline float distribution_Trowbridge_Reitz(float NdotH, float alpha2) {
    float nh2 = NdotH * NdotH;
    float d = (nh2 * (alpha2 - 1.0f) + 1.0f);
    float d2 = d * d;
    float d3 = M_PI_F * d2;
    return alpha2 / d3;
}

static inline float geometry_Schlick(float NdotV, float k) {
    return NdotV / (NdotV * (1.0f - k) + k);
}

static inline float geometry_Smith_k(float NdotV, float NdotL, float k) {
    float viewFactor = geometry_Schlick(NdotV, k);
    float lightFactor = geometry_Schlick(NdotL, k);
    return viewFactor * lightFactor;}

static inline float geometry_Smith_point(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0f;
    float k = r * r / 8.0f;
    return geometry_Smith_k(NdotV, NdotL, k);
}

static inline float3 Fresnel_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// Clear coat: see the note in Shaders.metal above split_sum_vertex_function

static constant float kClearCoatF0 = 0.04f;
static constant float kClearCoatRoughness = 0.125f;

// The direct light's reflectance times the cosine, f(V, L) * NdotL, for a
// surface of these G-buffer properties: the base (Lambert diffuse kD *
// albedo / pi and the GGX / Schlick-Smith / Schlick specular) and over it
// the clear coat of strength clear_coat.  The sun's light is this times its
// irradiance at normal incidence.
static inline float3 brdf_cosine(float3 albedo, float3 N, float3 V, float3 L,
                                 float roughness, float metallic, float clear_coat)
{
    float3 H = normalize(V + L);

    float3 F0 = 0.04f;
    F0 = mix(F0, albedo, metallic);

    float HdotV = saturate(dot(H, V));
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));

    // distribution factor
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float D = distribution_Trowbridge_Reitz(NdotH, alpha2);

    // geometry factor
    float G = geometry_Smith_point(NdotV, NdotL, roughness);

    // fresnel factor
    float3 F = Fresnel_Schlick(HdotV, F0);

    float3 numerator = D * G * F;
    float denominator = 4.0f * NdotV * NdotL + 0.0001f;
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0 - metallic);

    float3 Lo = (kD * albedo * M_1_PI_F + specular) * NdotL;

    // Clear coat (see kClearCoatRoughness): a second, sharper,
    // dielectric lobe on the same normal; the base sees what it
    // transmits
    if (clear_coat > 0.0f) {
        float coatAlpha = kClearCoatRoughness * kClearCoatRoughness;
        float Dc = distribution_Trowbridge_Reitz(NdotH, coatAlpha * coatAlpha);
        // Kelemen's visibility, G / (4 NdotV NdotL) ~ 1 / (4 LdotH^2),
        // as Filament's coat uses
        float Vc = 0.25f / max(HdotV * HdotV, 1e-4f);
        float Fc = clear_coat * Fresnel_Schlick(HdotV, float3(kClearCoatF0)).x;
        float coat = Dc * Vc * Fc;
        Lo = Lo * (1.0f - Fc) + coat * NdotL;
    }
    return Lo;
}

namespace deferred {

    // ---- Thickened symbols: the field and the march (the note above
    // symbol_cube_corner in Shaders.metal)

    // The atlas uv of a slab-space point (texels about the cell's centre;
    // slab y is up, image y is down)
    static float2 symbol_uv(WrySymbolInstance instance, float2 p) {
        return instance.cell.xy + 0.5f * instance.cell.z + float2(p.x, -p.y) * instance.cell.w;
    }

    // The field at a slab-space point: planar distance (texels) and the
    // outward unit gradient in slab axes
    static float3 symbol_field(texture2d<float> field, WrySymbolInstance instance, float2 p) {
        constexpr sampler bilinear(mag_filter::linear,
                                   min_filter::linear,
                                   s_address::clamp_to_edge,
                                   t_address::clamp_to_edge);
        float4 s = field.sample(bilinear, symbol_uv(instance, p));
        float d = (s.r - 0.5f) * (2.0f * WRY_SYMBOL_SDF_RANGE);
        float2 g = (s.gb - 0.5f) * 2.0f;
        return float3(d, g.x, -g.y);
    }

    // Rounded extrusion of a planar distance: w = (d2 + r, |z'| - (h - r))
    static float symbol_rounded_distance(float2 w, float r) {
        return min(max(w.x, w.y), 0.0f) + length(max(w, 0.0f)) - r;
    }

    static float symbol_slab_distance(WrySymbolInstance instance, float d2, float z) {
        float h = instance.half_extent.z;
        float rt = instance.top_radius;
        float rb = instance.bottom_radius;
        float top = symbol_rounded_distance(float2(d2 + rt, z - (h - rt)), rt);
        float bottom = symbol_rounded_distance(float2(d2 + rb, -z - (h - rb)), rb);
        return max(top, bottom);
    }

    struct SymbolHit {
        float3 position;   // slab space
        float3 normal;     // slab space, unit
        float2 uv;         // atlas: the print under a face, the margin past a wall
        bool print;        // the face's print (else the wall's margin)
    };

    // March the ray origin + t * direction (direction unit, origin outside
    // the cuboid or anywhere on the view side of the slab) through the
    // cuboid; false if it leaves without touching the slab
    // (max_steps: the raster's 160; the path tracer's light and shadow
    // rays take more, so grazing ones do not leak through)
    static bool symbol_march(texture2d<float> field, WrySymbolInstance instance,
                             float3 origin, float3 direction, thread SymbolHit& hit,
                             int max_steps = 160)
    {
        float3 half_extent = instance.half_extent.xyz;
        // Clip to the cuboid: slab test per axis, axes the ray runs along
        // unbounded (the ray passes through the cuboid, so it is inside
        // them)
        bool3 parallel = abs(direction) < 1e-6f;
        float3 safe = select(direction, float3(1.0f), parallel);
        float3 t_lo = (-half_extent - origin) / safe;
        float3 t_hi = ( half_extent - origin) / safe;
        float3 t_near = select(min(t_lo, t_hi), float3(-INFINITY), parallel);
        float3 t_far  = select(max(t_lo, t_hi), float3( INFINITY), parallel);
        float t = max(max(max(t_near.x, t_near.y), t_near.z), 0.0f);
        float t_exit = min(min(t_far.x, t_far.y), t_far.z);
        if (t_exit < t)
            return false;

        float3 p = origin + direction * t;
        float3 f = symbol_field(field, instance, p.xy);
        float d = symbol_slab_distance(instance, f.x, p.z);
        for (int i = 0; i != max_steps && d >= 0.02f; ++i) {
            t += d * 0.7f;
            if (t > t_exit)
                return false;
            p = origin + direction * t;
            f = symbol_field(field, instance, p.xy);
            d = symbol_slab_distance(instance, f.x, p.z);
        }
        if (d >= 0.02f)
            return false;

        // The gradient of whichever half owns the surface here
        float h = instance.half_extent.z;
        float rt = instance.top_radius;
        float rb = instance.bottom_radius;
        float2 wt = float2(f.x + rt, p.z - (h - rt));
        float2 wb = float2(f.x + rb, -p.z - (h - rb));
        bool top = symbol_rounded_distance(wt, rt) >= symbol_rounded_distance(wb, rb);
        float2 w = top ? wt : wb;
        float r = top ? rt : rb;
        float zs = top ? 1.0f : -1.0f;
        float2 g = f.yz;
        float3 n;
        if (w.x > 0.0f && w.y > 0.0f)
            n = float3(g * w.x, zs * w.y);          // fillet
        else if (w.y > w.x)
            n = float3(0.0f, 0.0f, zs);             // face
        else
            n = float3(g, 0.0f);                    // wall
        if (dot(n, n) < 1e-8f)
            n = float3(0.0f, 0.0f, zs);
        hit.position = p;
        hit.normal = normalize(n);
        // Colour: a face (and the upper half of its fillet) shows the
        // print under it; a wall (and the lower half of the fillet) shows
        // the atlas's margin half a texel outside the silhouette, which
        // the atlas tools fill with the edge's colour (or whatever is
        // painted there: tools/make_assets.py extend_margins,
        // tools/make_tile_atlas.py).  The atlas is straight alpha, so the
        // margin's colour is read as is, alpha ignored
        bool print = w.y > w.x;   // the face, or the fillet's upper half
        float2 uv_point = print ? p.xy : p.xy - g * (f.x - 0.5f);
        hit.uv = symbol_uv(instance, uv_point);
        hit.print = print;
        return true;
    }

    // ---- The board (the note above TerrainVertexFunctionOutput in
    // Shaders.metal)

    // The kind of a render-space cell, or WRY_TERRAIN_KIND_NONE off the map
    static int terrain_kind(constant WryTerrainUniforms& board, const device uchar* kinds, int2 cell) {
        int2 i = cell - board.origin;
        if (any(i < 0) || any(i >= board.size))
            return WRY_TERRAIN_KIND_NONE;
        return (int)kinds[i.y * board.size.x + i.x];
    }

    // A cell's tint, or the fallback where there is no cell
    static float3 terrain_tint(constant WryTerrainUniforms& board, const device uchar* kinds, int2 cell, float3 fallback) {
        int k = terrain_kind(board, kinds, cell);
        return k == WRY_TERRAIN_KIND_NONE ? fallback : board.tint[k].rgb;
    }

    // game/terrain.hpp terrain_variant on the world coordinate: its low
    // two bits are the variant the world map builder would pick, the
    // next two flip the mark
    static uint terrain_hash(int2 world) {
        uint h = (uint)world.x * 0x9E3779B9u ^ (uint)world.y * 0x85EBCA6Bu;
        h ^= h >> 16;
        h *= 0x7FEB352Du;
        h ^= h >> 15;
        h *= 0x846CA68Bu;
        h ^= h >> 16;
        return h;
    }

    // Coverage of one lattice line: g is the offset across it, along the
    // distance along it to the nearest corner (0 at a corner), arm the
    // fraction of the edge drawn from each corner, wpp the ground spanned
    // by a pixel across and along it
    static float terrain_line(float g, float along, float arm, float2 wpp,
                              constant WryTerrainUniforms& board) {
        float half_width = 0.5f * board.line_width;
        float clamped = max(half_width, 0.5f * board.line_min_pixels * wpp.x);
        float fade = half_width / clamped;
        float across = saturate((clamped - abs(g)) / wpp.x + 0.5f);
        float reach = saturate((0.5f * arm - along) / wpp.y + 0.5f);
        return across * reach * fade;
    }

    // The board's albedo at the ground point p (render space, cells centred
    // on the integers) of the cell `cell` = round(p) of kind `kind` (not
    // WRY_TERRAIN_KIND_NONE), with wpp the ground a pixel spans along x and
    // y (the raster pass's fwidth; tiny for an unfiltered point sample).
    // Three layers, in print order: the kind's pastel tint, the lattice of
    // lines on the cell edges, the kind's marks from the symbol field
    static float3 terrain_albedo(constant WryTerrainUniforms& board,
                                 const device uchar* kinds,
                                 texture2d<float> field,
                                 float2 p, int2 cell, int kind, float2 wpp)
    {
        constexpr sampler bilinear(mag_filter::linear,
                                   min_filter::linear,
                                   s_address::clamp_to_edge,
                                   t_address::clamp_to_edge);

        float wpp_mean = 0.5f * (wpp.x + wpp.y);
        float3 own = board.tint[kind].rgb;

        // The tint: read on the shifted lattice, blended with the
        // neighbours across the nearer edges over tint_blur, so a
        // boundary between kinds is a soft edge that misses the line
        float2 q = p - board.misregister;
        int2 tc = int2(round(q));
        float2 f = q - float2(tc);
        int2 towards = int2(sign(f));
        float2 edge = 0.5f - abs(f);
        float2 w = saturate(0.5f - edge / max(board.tint_blur, 1e-4f));
        float3 t00 = terrain_tint(board, kinds, tc, own);
        float3 t10 = terrain_tint(board, kinds, tc + int2(towards.x, 0), t00);
        float3 t01 = terrain_tint(board, kinds, tc + int2(0, towards.y), t00);
        float3 t11 = terrain_tint(board, kinds, tc + towards, t00);
        float3 tint = mix(mix(t00, t10, w.x), mix(t01, t11, w.x), w.y);

        // The lattice: lines on the half-integers.  g is the offset from
        // the cell's centre; the nearer line on each axis lies towards
        // sign(g), and the cell across it has a say in the line's look
        float2 g = p - float2(cell);
        int2 side = int2(sign(g));
        float4 own_lattice = board.lattice[kind];
        int kx = terrain_kind(board, kinds, cell + int2(side.x, 0));
        int ky = terrain_kind(board, kinds, cell + int2(0, side.y));
        float4 lx = kx == WRY_TERRAIN_KIND_NONE ? own_lattice : max(own_lattice, board.lattice[kx]);
        float4 ly = ky == WRY_TERRAIN_KIND_NONE ? own_lattice : max(own_lattice, board.lattice[ky]);
        // along a vertical line the nearest corner is at the half-integer y
        float2 along = 0.5f - abs(g);
        float vertical = terrain_line(0.5f - abs(g.x), along.y, lx.y, wpp.xy, board) * lx.x;
        float horizontal = terrain_line(0.5f - abs(g.y), along.x, ly.y, wpp.yx, board) * ly.x;
        float lattice = max(vertical, horizontal);

        // The marks: the kind's variant from the field's terrain row,
        // flipped by two more hash bits, on the kind's share of cells by
        // eight more, inked by its distance
        uint h = terrain_hash(cell + board.focus);
        uint variant = h & (WRY_TERRAIN_PATTERN_VARIANTS - 1);
        float share = float((h >> 4) & 255u) / 256.0f;
        float marked = share < own_lattice.w ? 1.0f : 0.0f;
        float2 fc = g + 0.5f;                       // across the cell, y up
        fc.x = (h & 4u) ? 1.0f - fc.x : fc.x;
        fc.y = (h & 8u) ? 1.0f - fc.y : fc.y;
        // (kinds 0-3 in the pattern row, the ores 4-6 in the ore row)
        int pattern_row = kind < 4 ? WRY_TERRAIN_PATTERN_ROW : WRY_TERRAIN_ORE_PATTERN_ROW;
        int pattern_column = (kind & 3) * WRY_TERRAIN_PATTERN_VARIANTS + (int)variant;
        float2 uv = (float2((float)pattern_column, (float)pattern_row)
                     + float2(fc.x, 1.0f - fc.y)) / (float)WRY_SYMBOL_ATLAS_CELLS;
        float d_texels = (field.sample(bilinear, uv).r - 0.5f) * (2.0f * WRY_SYMBOL_SDF_RANGE);
        float d = d_texels / 256.0f + board.mark_bias;       // 256 source texels per cell
        float mark = saturate(0.5f - d / wpp_mean) * own_lattice.z * marked;

        float ink = 1.0f - (1.0f - lattice) * (1.0f - mark);
        return mix(tint, board.line_color.rgb, ink);
    }

    // ---- Decals (the note above DecalVertexFunctionOutput in
    // Shaders.metal)

    // The G-buffer channels a decal edits, as the raster pass stores them
    struct DecalTarget {
        half4 light;      // emission
        half4 albedo;
        half4 normal;     // xyz unit, world
        half4 material;   // occlusion, roughness, metallic, clear coat
    };

    // The image uv of a decal-space point (the box is +-0.5)
    static float2 decal_uv(WryDecalInstance instance, float3 p) {
        return instance.uv_rect.xy + float2(p.x + 0.5f, 0.5f - p.y) * instance.uv_rect.zw;
    }

    // Paint the decal at image uv `uv` onto a surface point the caller has
    // found inside the box and facing within the cutoff; duvdx / duvdy are
    // the image's footprint (the raster pass's dfdx / dfdy of uv; zero for
    // an unfiltered point sample)
    static void decal_paint(WryDecalInstance instance, texture2d<half> image,
                            float2 uv, float2 duvdx, float2 duvdy,
                            thread DecalTarget& t)
    {
        constexpr sampler trilinear(mag_filter::linear,
                                    min_filter::linear,
                                    mip_filter::linear,
                                    s_address::clamp_to_edge,
                                    t_address::clamp_to_edge);

        const float footprint = exp2(instance.lod_bias);
        half4 s = image.sample(trilinear, uv, gradient2d(duvdx * footprint, duvdy * footprint));
        half coverage = s.a * half(instance.tint.a);
        half3 colour = half3(instance.tint.rgb) * s.rgb;

        half emitting = half(instance.emissive.a) * coverage;
        t.light = half4(mix(t.light.rgb, half3(instance.emissive.rgb), emitting), t.light.a);
        t.albedo = half4(mix(t.albedo.rgb, colour, coverage), t.albedo.a);
        half2 toward = half2(instance.material.xy);
        half2 weight = half2(instance.material.zw) * coverage;
        t.material = half4(t.material.r * (1.0h - half(instance.occlusion) * coverage),
                           mix(t.material.g, toward.x, weight.x),
                           mix(t.material.b, toward.y, weight.y),
                           t.material.a);

        if (instance.emboss != 0.0f) {
            float r = max(instance.emboss_radius, max(length(duvdx), length(duvdy)));
            gradient2d level(float2(r, 0.0f), float2(0.0f, r));
            float2 du = float2(r, 0.0f), dv = float2(0.0f, r);
            float east  = image.sample(trilinear, uv + du, level).a;
            float west  = image.sample(trilinear, uv - du, level).a;
            float north = image.sample(trilinear, uv - dv, level).a;   // up the image is -v
            float south = image.sample(trilinear, uv + dv, level).a;
            // The height's slope per world unit along the frame's x and y:
            // 2r of uv is 2r / uv_rect.z decal units of x, each
            // |columns[0]| world units long (and so for y)
            float3 ex = instance.model_transform.columns[0].xyz;
            float3 ey = instance.model_transform.columns[1].xyz;
            float height = instance.emboss * instance.tint.a;
            float2 slope = height * float2((east - west) * instance.uv_rect.z / length(ex),
                                           (north - south) * instance.uv_rect.w / length(ey)) / (2.0f * r);
            float3 n = float3(t.normal.xyz);
            float3 tt = ex - n * dot(n, ex);
            float3 b = ey - n * dot(n, ey);
            tt = normalize(tt);
            b = normalize(b - tt * dot(tt, b));
            t.normal = half4(half3(normalize(n - slope.x * tt - slope.y * b)), t.normal.w);
        }
    }

} // namespace deferred

#endif /* ShaderSurfaces_h */
