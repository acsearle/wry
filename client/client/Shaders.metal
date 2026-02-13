//
//  Shaders.metal
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

#include "ShaderTypes.h"

# pragma mark - Vector graphics rendeting functionality

namespace stroked {
    
    float2 bezier_xy_for_t(float t, float2 a, float2 b, float2 c) {
        float2 ab = mix(a, b, t);
        float2 bc = mix(b, c, t);
        float2 abc = mix(ab, bc, t);
        return abc;
    }
    
    float quadratic_root(float a, float b, float c, float t0, float t1) {
        // When the root becomes invalid we want continuity

        float d = b*b - 4.0*a*c;
        // if (d < 0.0)
        //    return t0;
        float q = -0.5 * (b + copysign(sqrt(max(d, 0.0)), b));
        float r0 = clamp(q / a, t0, t1); // (a != 0.0 ? q / a : -1.0);
        float r1 = clamp(c / q, t0, t1); // (q != 0.0 ? c / q : -1.0);
        float tmid = (t0 + t1) * 0.5;
        if (abs(r0 - tmid) <= abs(r1 - tmid)) {
            return r0;
        } else {
            return r1;
        }
    }

    float bezier_t_for_x(float x, float t0, float t1, float2 a, float2 b, float2 c) {
        float r = quadratic_root(a.x - 2.0*b.x + c.x,
                                 -2.0*a.x + 2.0*b.x,
                                 a.x - x,
                                 t0,
                                 t1);
        return r;
    }

    float bezier_t_for_y(float y, float t0, float t1, float2 a, float2 b, float2 c) {
        float r = quadratic_root(a.y - 2.0*b.y + c.y,
                      -2.0*a.y + 2.0*b.y,
                      a.y - y,
                      t0,
                      t1);
        return r;
    }
    
    
    
    
    
    
    
    
    
    
    struct BezierUniforms {
        float4x4 transformation;
    };
    
    struct BezierPerVertex {
        float4 position [[position]];
        float4 coordinate;
    };

    struct BezierPerPrimitive {
        uint begin, end;
    };

    
    using MeshOut = metal::mesh<BezierPerVertex,
        BezierPerPrimitive, 4, 2, metal::topology::triangle>;

    
    static constexpr constant uint32_t AAPLMaxTotalThreadsPerObjectThreadgroup = 1;
    static constexpr constant uint32_t AAPLMaxTotalThreadsPerMeshThreadgroup = 2;
    static constexpr constant uint32_t AAPLMaxThreadgroupsPerMeshGrid = 8;

//    struct BezierPayload
//    {
//        // AAPLVertex vertices[AAPLMaxMeshletVertexCount];
//        
//        // these are not uniforms; in the sample they are constant across one
//        // meshlet, of which there are many
//        float4x4 transform;
//        float3 color;
//        uint8_t lod;
//        uint32_t primitiveCount;
//        uint8_t vertexCount;
//        
//        /// The array of vertex indices for the meshlet into the vertices array.
//        ///
//        /// The object stage uses this to copy indices into the payload.
//        /// The mesh stage uses this to set the indices for the geometry.
//        uint8_t indices[1024];
//    };

    
    // In the sample code, the object function is run once per compute grid cell?
    // It uses its XYZ coordinate to look up work in a linear buffer, and
    // then seems to launch a fixed number of
    
//    [[object,
//      max_total_threads_per_threadgroup(AAPLMaxTotalThreadsPerObjectThreadgroup),
//      max_total_threadgroups_per_mesh_grid(AAPLMaxThreadgroupsPerMeshGrid)]]
//    void bezierObjectFunction(object_data BezierPayload& payload [[payload]],
//                                         mesh_grid_properties meshGridProperties,
//                                         uint3 positionInGrid [[threadgroup_position_in_grid]]
//                                         ) {
//        // uint threadIndex = positionInGrid.x;
//        
//        meshGridProperties.set_threadgroups_per_grid(uint3(1, 1, 1));
//    }
    
    
    // In the sample code, the mesh stage function does one vertex (or none)
    // and one primitive (or none), and always sets the primitive_count.
    // Presumably it is safe for every thread to clobber the primitive_count.
    // And they otherwise write different locations.
    
    
    [[mesh,
      max_total_threads_per_threadgroup(AAPLMaxTotalThreadsPerMeshThreadgroup)]]
    void bezierMeshFunction(MeshOut output,
                            // const object_data BezierPayload& payload [[payload]],
                            const device bezier::GlyphInformation* buf_gi [[buffer(0)]],
                            const device bezier::Character* buf_ch [[buffer(1)]],
                            uint lid [[thread_index_in_threadgroup]],
                            uint tid [[threadgroup_position_in_grid]]) {

        output.set_primitive_count(2);
        
        bezier::Character ch = buf_ch[tid];
        bezier::GlyphInformation gi = buf_gi[ch.glyph_id];

        {
            BezierPerPrimitive p{};
            p.begin = gi.bezier_begin;
            p.end = gi.bezier_end;
            output.set_primitive(0, p);
            output.set_primitive(1, p);
            output.set_index(0, 0);
            output.set_index(1, 1);
            output.set_index(2, 2);
            output.set_index(3, 3);
            output.set_index(4, 2);
            output.set_index(5, 1);
        }

        {
            BezierPerVertex v{};
            v.coordinate = float4(gi.a.x, gi.a.y, 0.0, 1.0);
            v.position = v.coordinate + float4(ch.position.xy, 0.0, 0.0);
            output.set_vertex(0, v);
            v.coordinate = float4(gi.a.x, gi.b.y, 0.0, 1.0);
            v.position = v.coordinate + float4(ch.position.xy, 0.0, 0.0);
            output.set_vertex(1, v);
            v.coordinate = float4(gi.b.x, gi.a.y, 0.0, 1.0);
            v.position = v.coordinate + float4(ch.position.xy, 0.0, 0.0);
            output.set_vertex(2, v);
            v.coordinate = float4(gi.b.x, gi.b.y, 0.0, 1.0);
            v.position = v.coordinate + float4(ch.position.xy, 0.0, 0.0);
            output.set_vertex(3, v);
        }

    }

    


    
    
    struct BezierFragmentIn {
        BezierPerVertex    v;
        BezierPerPrimitive p;
    };
    
    struct BezierFragmentOut {
        half4 color [[color(AAPLColorIndexColor),
                      raster_order_group(AAPLRasterOrderGroupLighting) ]];
    };
    
    
    
//    [[vertex]] BezierVertexShaderOut bezierVertexShader(uint vertex_id [[vertex_id]],
//                                                  constant BezierUniforms &uniforms  [[buffer(AAPLBufferIndexUniforms)]],
//                                                  const device BezierVertex *vertexArray [[buffer(AAPLBufferIndexVertices)]]) {
//        BezierVertexShaderOut result{};
//        return result;
//    }
    
    
    
    struct BezierControlPoints {
        float2 a;
        float2 b;
        float2 c;
        float2 _padding;
    };
    
    
    //  Copyright (C) 2014 TroggleMonkey
    //
    //  Permission is hereby granted, free of charge, to any person obtaining a copy
    //  of this software and associated documentation files (the "Software"), to
    //  deal in the Software without restriction, including without limitation the
    //  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    //  sell copies of the Software, and to permit persons to whom the Software is
    //  furnished to do so, subject to the following conditions:
    //
    //  The above copyright notice and this permission notice shall be included in
    //  all copies or substantial portions of the Software.
    //
    //  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    //  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    //  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    //  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    //  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    //  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    //  IN THE SOFTWARE.
    
    float2 erf_tanh_approx(float2 x) {
        // This approximation produces visible problems
        return tanh(1.202760580 * x);
    }
    
    float2 erf6(const float2 x)
    {
        //  Float2 version:
        const float2 one = float2(1.0);
        const float2 sign_x = sign(x);
        const float2 t = one/(one + 0.47047*abs(x));
        const float2 result = one - t*(0.3480242 + t*(-0.0958798 + t*0.7478556))*
        exp(-(x*x));
        return result * sign_x;
    }
    
    float2 erf_shadertoy(float2 x) {
        return sign(x) * sqrt(1.0 - exp2(-1.787776 * x * x)); // likely faster version by @spalmer
    }

    float2 erfc_approx(float2 x) {
        return 1.0 - erf6(x);
    }

    
    [[fragment]] BezierFragmentOut
    bezierFragmentFunction(BezierFragmentIn input [[stage_in]],
                           const device bezier::BezierControlPoints* bez [[buffer(0)]]) {
        
        float2 coordinate = input.v.coordinate.xy;
    
        BezierFragmentOut result{};
        
        float cumulant = 0.0;
        for (uint j = input.p.begin; j != input.p.end; ++j) {
            BezierControlPoints curve;
            curve.a = coordinate.xy - bez[j].a;
            curve.b = coordinate.xy - bez[j].b;
            curve.c = coordinate.xy - bez[j].c;
            
            float ts[7];
            ts[0] = 0.0;
            ts[2] = bezier_t_for_x(0.0, 0.0, 1.0, curve.a, curve.b, curve.c);
            ts[4] = bezier_t_for_y(0.0, 0.0, 1.0, curve.a, curve.b, curve.c);
            ts[6] = 1.0;
            if (ts[2] > ts[4]) {
                float tmp = ts[2];
                ts[2] = ts[4];
                ts[4] = tmp;
            }
            ts[1] = (ts[0] + ts[2]) * 0.5;
            ts[3] = (ts[2] + ts[4]) * 0.5;
            ts[5] = (ts[4] + ts[6]) * 0.5;
            float2 points[7];
            for (int i = 0; i != 7; ++i) {
                points[i] = erfc_approx(bezier_xy_for_t(ts[i], curve.a, curve.b, curve.c)*float2(300.0,150.0));
            }
            for (int i = 0; i != 6; ++i) {
                cumulant -= (points[i+1].y - points[i].y) * (points[i].x + points[i+1].x);
            }
        }
            
        // cumulant = ts[3];
    
        cumulant = clamp(cumulant * 0.125, 0.0, 1.0);
        
        // result.color = half4(cumulant, cumulant, cumulant, 1.0);
        result.color.r = cumulant;
        result.color.g = cumulant;
        result.color.b = cumulant;
        result.color.a = 1.0;
        // result.color.r = ts[2];
        // result.color.g = ts[4];
        return result;
    }
    
} // namespace

# pragma mark - Physically-based rendering functionaliy

// Physically-based rendering functionality
//
// Mostly based on
//
//     [1] learnopengl.com/PBR
//
// itself based on
//
//     [2] https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
//
// which describes the lighting model used by Epic in UE4, which is itself based on
//
//     [3] https://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf
//
// which describes the lighting model used by Disney in Wreck-It Ralph



// Hammersley low-discrepency sequence for quasi-Monte-Carlo integration

float2 sequence_Hammersley(ushort i, ushort N) {
    return float2(float(i) / float(N), reverse_bits(i) * 0.00001525878f);
}

// Microfacet distribution

// Determines reflection sharpness and highlight size

float distribution_Trowbridge_Reitz(float NdotH, float alpha2) {
    float nh2 = NdotH * NdotH;
    float d = (nh2 * (alpha2 - 1.0f) + 1.0f);
    float d2 = d * d;
    float d3 = M_PI_F * d2;
    return alpha2 / d3;
}

float3 sample_Trowbridge_Reitz(float2 chi, float alpha2) {
    
    // note that this form of the quotient is robust against both
    // \alpha = 0 and \alpha = 1
    float cosTheta2 = (1.0 - chi.y) / (1.0 + (alpha2 - 1.0) * chi.y);
    
    float phi = 2.0f * M_PI_F * chi.x;
    float cosPhi = cos(phi);
    float sinPhi = sin(phi);
    
    float cosTheta = sqrt(cosTheta2);
    float sinTheta = sqrt(1.0 - cosTheta2);
    
    return float3(sinTheta * cosPhi,
                  sinTheta * sinPhi,
                  cosTheta);
    
}

// Geometry factor

// Associated with microfacet self-shadowing on rough surfaces and glancing
// rays; tends to darken edges

//TODO: we can inline these better and eliminate some duplication
float geometry_Schlick(float NdotV, float k) {
    return NdotV / (NdotV * (1.0f - k) + k);
}

float geometry_Smith_k(float NdotV, float NdotL, float k) {
    float viewFactor = geometry_Schlick(NdotV, k);
    float lightFactor = geometry_Schlick(NdotL, k);
    return viewFactor * lightFactor;}

float geometry_Smith_point(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0f;
    float k = r * r / 8.0f;
    return geometry_Smith_k(NdotV, NdotL, k);
}

float geometry_Smith_area(float NdotV, float NdotL, float roughness) {
    float k = roughness * roughness / 2.0f;
    return geometry_Smith_k(NdotV, NdotL, k);
}

// Fresnel factor

// Increased reflectivity at glancing angles; tends to brighten edges

float3 Fresnel_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float3 Fresnel_Schlick_roughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0 - roughness), F0) - F0) * pow(saturate(1.0f - cosTheta), 5.0);
}

// "Split sum" approximation lookup table

// TODO: make this a kernel operation
// TODO: just load it?
[[vertex]] float4
split_sum_vertex_function(ushort i [[vertex_id]],
                          const device float4* vertices [[buffer(AAPLBufferIndexVertices)]]) {
    return vertices[i];
}

[[fragment]] float4 
split_sum_fragment_function(float4 position [[position]]) {
    
    float cosTheta = position.x / 256.0f;
    float roughness = position.y / 256.0f;
    
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
    float NdotV = cosTheta;
    
    // We are given N and V up to choice of coordinate system
    
    // We integrate over L, but the importance sampler generates samples of H
    // around an assumed normal of N = +Z.  This constrains our coordinate system
    // up to rotation around Z.  We chose to put V on the XZ plane.
    
    [[maybe_unused]] float3 N = float3(0, 0, 1);
    float3 V = float3(sinTheta, 0.0, cosTheta);
    
    float scale = 0.0;
    float offset = 0.0;
    
    ushort M = 1024;
    for (ushort i = 0; i != M; ++i) {
        float2 X = sequence_Hammersley(i, M);
        float3 H = sample_Trowbridge_Reitz(X, alpha2);
        float3 L = reflect(-V, H);
        float NdotH = saturate(H.z);
        float NdotL = saturate(L.z);
        float VdotH = saturate(dot(V, H));
        if (NdotL > 0) {
            float G = geometry_Smith_area(NdotV, NdotL, roughness);
            float G_vis = G * VdotH / (NdotV * NdotH);
            float Fc = pow(1.0 - VdotH, 5);
            scale += (1.0 - Fc) * G_vis;
            offset += Fc * G_vis;
            
        }
    }
    return float4(scale / M, offset / M, 0.0f, 0.0f);
    
}



# pragma mark - Deferred physically-based rendering

namespace deferred {
    
    // We follow Apple's example for tile-based deferred rendering
    //
    //     [4] https://developer.apple.com/documentation/metal/metal_sample_code_library/
    //         rendering_a_scene_with_forward_plus_lighting_using_tile_shaders?language=objc
    //
    // In this technique, the g-buffers for many parameters don't actually need
    // to be stored and loaded; instead the pipeline is transposed to do
    // all work on small imageblocks and the whole g-buffer is never materialized
    
    
    struct VertexFunctionOutput {
        float4 position_clip [[position]];
        float4 coordinate;
        float4 tangent_world;
        float4 bitangent_world;
        float4 normal_world;
    };
    
    // This is the g-buffer
    //
    // Note use of raster order groups
    //
    // Metal does not(?) allow reading back from depth buffer so we compute it
    // in user space
    
    // TODO: occlusion, clearcoat, clearcoat roughness
    //
    // TODO: is g-buffer storage 'free' until we free up the imageblock?
    //       perhaps we should just write out the world position
    
    struct FragmentFunctionOutput {

        // emission is written here in first pass
        // lighting is accumulated here by susbequent passes
        half4 light              [[color(AAPLColorIndexColor),
                                   raster_order_group(AAPLRasterOrderGroupLighting)]];

        // these surface properties are immutable on later passes
        half4 albedo_metallic    [[color(AAPLColorIndexAlbedoMetallic),
                                   raster_order_group(AAPLRasterOrderGroupGBuffer)]];
        
        half4 normal_roughness    [[color(AAPLColorIndexNormalRoughness),
                                    raster_order_group(AAPLRasterOrderGroupGBuffer)]];
        
        float depth               [[color(AAPLColorIndexDepth),
                                    raster_order_group(AAPLRasterOrderGroupGBuffer)]];
        
    };
    
    // TODO: consider an object function and a mesh function
    //       - expand tiles on the fly from their instance properties
    
    [[vertex]] VertexFunctionOutput
    vertex_function(uint vertex_id [[ vertex_id ]],
                    uint instance_id [[ instance_id ]],
                    constant MeshUniforms &uniforms  [[buffer(AAPLBufferIndexUniforms)]],
                    const device MeshVertex *vertexArray [[buffer(AAPLBufferIndexVertices)]],
                    const device MeshInstanced *instancedArray [[buffer(AAPLBufferIndexInstanced)]])
    {
        VertexFunctionOutput out;
        
        MeshVertex in = vertexArray[vertex_id];
        MeshInstanced instance = instancedArray[instance_id];
        
        out.coordinate = in.coordinate;
        
        float4 position_world = instance.model_transform * in.position;
        out.tangent_world   = instance.inverse_transpose_model_transform * in.tangent;
        out.bitangent_world = instance.inverse_transpose_model_transform * in.bitangent;
        out.normal_world    = instance.inverse_transpose_model_transform * in.normal;
        
        out.position_clip   = uniforms.viewprojection_transform * position_world;
        
        return out;
    }
    
    // TODO: Three.js shaders suggest there is a conventional structure for
    //       texture packing like OcclusionRoughnessMetallic <=> RGB

    [[fragment]] FragmentFunctionOutput
    fragment_function(VertexFunctionOutput in [[stage_in]],
                               bool front_facing [[front_facing]], //:todo: for debugging
                               constant MeshUniforms& uniforms  [[ buffer(AAPLBufferIndexUniforms) ]],
                               texture2d<half> emissiveTexture [[texture(AAPLTextureIndexEmissive) ]],
                               texture2d<half> albedoTexture [[texture(AAPLTextureIndexAlbedo) ]],
                               texture2d<half> metallicTexture [[texture(AAPLTextureIndexMetallic)]],
                               texture2d<half> normalTexture [[texture(AAPLTextureIndexNormal)]],
                               texture2d<half> roughnessTexture [[texture(AAPLTextureIndexRoughness)]])
    
    {
        
        constexpr sampler trilinearSampler(mag_filter::linear,
                                           min_filter::linear,
                                           mip_filter::linear,
                                           s_address::repeat,
                                           t_address::repeat);
        
        FragmentFunctionOutput out;
        
        half4 albedoSample = albedoTexture.sample(trilinearSampler, in.coordinate.xy);
        
        // the g-buffer can't support transparency so we just discard on a
        // threshold
        if (albedoSample.a < 0.5f)
            discard_fragment();
        
        half4 normalSample = normalTexture.sample(trilinearSampler, in.coordinate.xy);
        half4 roughnessSample = roughnessTexture.sample(trilinearSampler, in.coordinate.xy);
        half4 metallicSample = metallicTexture.sample(trilinearSampler, in.coordinate.xy);
        half4 emissiveSample = emissiveTexture.sample(trilinearSampler, in.coordinate.xy);
        
        normalSample = normalSample * 2.0h - 1.0h;
        
        half3 normal = normalize(half3x3(half3(in.tangent_world.xyz),
                                         half3(in.bitangent_world.xyz),
                                         half3(in.normal_world.xyz)) * normalSample.xyz);
                
        out.light = front_facing ? emissiveSample : half4(1.0h, 0.0h, 1.0h, 0.0h);
        out.albedo_metallic.rgb = front_facing ? albedoSample.rgb : 0.0h;
        out.albedo_metallic.a = front_facing ? metallicSample.b : 0.0h;
        out.normal_roughness.xyz = front_facing ? normal : 0.0h;
        out.normal_roughness.w = front_facing ? roughnessSample.g : 1.0h;
        
        // this choice of depth is the same as the hardware depth buffer
        // note we cannot read the hardware depth buffer in the same pass
        out.depth = in.position_clip.z;
        
        return out;
        
    }
    
    
    
    struct ShadowVertexFunctionOutput {
        float4 clipSpacePosition [[position]];
        float4 coordinate;
    };
        
    [[vertex]] ShadowVertexFunctionOutput
    shadow_vertex_function(uint vertex_id [[ vertex_id ]],
                                    uint instance_id [[ instance_id ]],
                                    constant MeshUniforms &uniforms  [[buffer(AAPLBufferIndexUniforms)]],
                                    const device MeshVertex *vertexArray [[buffer(AAPLBufferIndexVertices)]],
                                    const device MeshInstanced *instancedArray [[buffer(AAPLBufferIndexInstanced)]])
    {
        ShadowVertexFunctionOutput out;
        MeshVertex in = vertexArray[vertex_id];
        MeshInstanced instance = instancedArray[instance_id];
        out.coordinate = in.coordinate;
        float4 worldSpacePosition = instance.model_transform * in.position;
        out.clipSpacePosition = uniforms.light_viewprojection_transform * worldSpacePosition;
        return out;
    }
    
    
    [[fragment]] void
    shadow_fragment_function(ShadowVertexFunctionOutput in [[stage_in]],
                             texture2d<half> albedoTexture [[texture(AAPLTextureIndexAlbedo) ]])
    
    {
        constexpr sampler trilinearSampler(mag_filter::linear,
                                           min_filter::linear,
                                           mip_filter::linear,
                                           s_address::repeat,
                                           t_address::repeat);
        
        half4 albedoSample = albedoTexture.sample(trilinearSampler, in.coordinate.xy);
        
        if (albedoSample.a < 0.5f)
            discard_fragment();
        
    }
    
    
    
    
    
    
    
    
    
    // we also need a shader that draws the shadows of smoke, dust, clouds onto an
    // illumination map from the light's perspective; these don't write the z-buffer
    // but they are masked by it, and are just absorption
    
    
    
    // Deferred GBuffer decals
    //
    // Some decals - like tire tracks - will replace all surface properties with
    // changed normals, roughness etc.
    //
    // Others, like augmented reality symbols projected onto a surface, will not be
    // lit and are just emssive and supressing albedo
    
    
    struct LightingVertexFunctionOutput {
        float4 near_clip [[ position ]];
        float4 near_world;
    };
    
    struct LightingFragmentFunctionOutput {
        half4 color [[color(AAPLColorIndexColor), raster_order_group(AAPLRasterOrderGroupLighting)]];
    };
        
    [[vertex]] LightingVertexFunctionOutput
    lighting_vertex_function(uint vertexID [[ vertex_id ]],
                       const device float4 *vertexArray [[ buffer(AAPLBufferIndexVertices) ]],
                       constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]])
    {
        LightingVertexFunctionOutput out;
        out.near_clip = vertexArray[vertexID];
        out.near_world = uniforms.inverse_viewprojection_transform * out.near_clip;
        return out;
    }
    
    
    
    [[fragment]] LightingFragmentFunctionOutput
    image_based_lighting_fragment_function(LightingVertexFunctionOutput in [[stage_in]],
                                           FragmentFunctionOutput gbuffer,
                                           constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]],
                                           texturecube<float> environmentTexture [[texture(AAPLTextureIndexEnvironment)]],
                                           texture2d<float> fresnelTexture [[texture(AAPLTextureIndexFresnel)]])
    {
        constexpr sampler bilinearSampler(mag_filter::linear,
                                          min_filter::linear,
                                          s_address::clamp_to_edge,
                                          t_address::clamp_to_edge);
        
        constexpr sampler trilinearSampler(mag_filter::linear,
                                           min_filter::linear,
                                           mip_filter::linear,
                                           s_address::repeat,
                                           t_address::repeat);
        
        LightingFragmentFunctionOutput out;
        
        float3 albedo    = float3(gbuffer.albedo_metallic.rgb);
        float  metallic  = float(gbuffer.albedo_metallic.a);
        float3 normal    = float3(gbuffer.normal_roughness.xyz);
        float  roughness = float(gbuffer.normal_roughness.w);
        // float  depth     = gbuffer.depth;
        float  occlusion = 1.0f;
        
        // compute ray direction (towards camera)
        float4 far_world = in.near_world + uniforms.inverse_viewprojection_transform.columns[2];
        float3 direction = in.near_world.xyz * far_world.w - far_world.xyz * in.near_world.w;
        
        float3 V = normalize(direction);
        float3 N = normal;
        float3 R = reflect(-V, N);
        
        float NdotV = saturate(dot(N, V));
        float lod = log2(roughness) + 4;
        
        float3 diffuseSample = environmentTexture.sample(trilinearSampler,
                                                         uniforms.ibl_transform * N,
                                                         level(4)).rgb;
        
        float3 reflectedSample = environmentTexture.sample(trilinearSampler,
                                                           uniforms.ibl_transform * R,
                                                           level(lod)).rgb;
        
        float4 fresnelSample = fresnelTexture.sample(bilinearSampler,
                                                     float2(NdotV, roughness));
        
        float3 F0 = 0.04f;
        F0 = mix(F0, albedo, metallic);
        
        float3 F = Fresnel_Schlick_roughness(NdotV, F0, roughness);
        
        float3 kS = F;
        float3 kD = 1.0 - kS;
        
        float3 diffuse = albedo * diffuseSample.rgb * M_1_PI_F;
        
        float3 specular = (F * fresnelSample.r + fresnelSample.g) * reflectedSample.rgb;
        
        float3 Lo = (kD * diffuse + specular) * occlusion * uniforms.ibl_scale.rgb;
        
        out.color.rgb = half3(Lo);
        //out.color.rgb = half3(-V);
        out.color.a = 1.0f;
        
        // out.color = clamp(out.color, 0.0h, HALF_MAX);
        
        return out;
        
    }
    
    
    [[fragment]] LightingFragmentFunctionOutput
    directional_lighting_fragment_function(LightingVertexFunctionOutput in [[stage_in]],
                                           FragmentFunctionOutput gbuffer,
                                           constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]],
                                           texture2d<float> shadowTexture [[texture(AAPLTextureIndexShadow)]])
    {
        constexpr sampler nearestSampler(mag_filter::nearest,
                                         min_filter::nearest,
                                         s_address::clamp_to_edge,
                                         t_address::clamp_to_edge);
        
        LightingFragmentFunctionOutput out;
        
        float3 albedo    = float3(gbuffer.albedo_metallic.rgb);
        float  metallic  = float(gbuffer.albedo_metallic.a);
        float3 normal    = float3(gbuffer.normal_roughness.xyz);
        float  roughness = float(gbuffer.normal_roughness.w);
        float  depth     = gbuffer.depth;
        // float  occlusion = 1.0f;
        
        float4 position_world = in.near_world + uniforms.inverse_viewprojection_transform.columns[2] * depth;
        float3 direction = in.near_world.xyz * position_world.w - position_world.xyz * in.near_world.w;
        
        float3 V = normalize(direction);
        float3 N = normal;
        float3 L = uniforms.light_direction;
        float3 H = normalize(V + L);
        
        float4 coordinate_light = uniforms.light_viewprojectiontexture_transform * position_world;
        coordinate_light /= coordinate_light.w;
        
        float shadowSample = shadowTexture.sample(nearestSampler, coordinate_light.xy).r;
        float shadowFactor = step(coordinate_light.z, shadowSample);
        
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
        
        float3 Lo = (kD * albedo * M_1_PI_F + specular) * NdotL * uniforms.radiance * shadowFactor;
        
        // Lo = clamp(Lo, 0.0f, 4096.0f);
        
        out.color.rgb = half3(Lo);
        //out.color.rgb = half3(in.position_clip.xyz/in.position_clip.w) * 0.01h;
        //out.color.rg = half2(lightSpacePosition.xy);
        //out.color.b = shadowSample;
        //out.color.rgb = lightSpacePosition.z;
        out.color.a = 1.0h;
        // out.color.rgb = half3(lightSpacePosition.xyz > 0.5f);
        
        out.color = clamp(out.color, 0.0f, HALF_MAX);
        
        return out;
        
    }
    
    
    
    [[fragment]] LightingFragmentFunctionOutput
    point_lighting_fragment_function(LightingVertexFunctionOutput in [[stage_in]],
                                     FragmentFunctionOutput gbuffer,
                                     constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]]
                                     // , texturecube<float> environmentTexture [[texture(AAPLTextureIndexEnvironment)]]
                                     )
    {
        constexpr sampler trilinearSampler(mag_filter::linear,
                                           min_filter::linear,
                                           mip_filter::linear,
                                           s_address::repeat,
                                           t_address::repeat);
        
        LightingFragmentFunctionOutput out;
        
        float3 albedo    = float3(gbuffer.albedo_metallic.rgb);
        float  metallic  = float(gbuffer.albedo_metallic.a);
        float3 normal    = float3(gbuffer.normal_roughness.xyz);
        float  roughness = float(gbuffer.normal_roughness.w);
        float  depth     = gbuffer.depth;
        // float  occlusion = 1.0f;

        // position of the fragment in the world
        float4 position_world = in.near_world + uniforms.inverse_viewprojection_transform.columns[2] * depth;
        position_world /= position_world.w;
        float3 direction = in.near_world.xyz * position_world.w - position_world.xyz * in.near_world.w;

        // position of the fragment in the light's coordinate system
        float4 position_light = uniforms.light_viewprojection_transform * position_world;
        position_light /= position_light.w;
        float falloff = length_squared(position_light.xyz);
        
        
        /*
        float3 radiance = environmentTexture.sample(trilinearSampler,
                                                         //uniforms.ibl_transform * N,
                                                         position_light.xyz,
                                                         level(0)).rgb;
        float3 radianceScatter = environmentTexture.sample(trilinearSampler,
                                                    //uniforms.ibl_transform * N,
                                                    position_light.xyz,
                                                    level(4)).rgb;
         */
        //position_light.xyz = normalize(position_light.xyz);
        
        // spotlight?
        float3 radiance = 10 * smoothstep(-0.5, 0.0, -length_squared(position_light.xz) / (position_light.y * position_light.y));
        float3 radianceScatter = 0.01;


        
        float3 V = normalize(direction);
        float3 N = normal;
        float3 L = normalize(uniforms.light_position.xyz - position_world.xyz);
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
        
        float3 Lo = (kD * albedo * M_1_PI_F + specular) * NdotL * radiance / falloff;
        
        {
            // Also need to compute scattering of the light source to give it a
            // halo.  The geometrical part is
            // distance along ray to surface
            // distance along ray to closest point to light
            // distance between ray and light
            // this is then gonna be a Cauchy integral, atan
            
            // position_world
            // position_light
            // direction
            float3 fragment_to_camera = V;
            float3 fragment_to_light = (uniforms.light_position - position_world).xyz;
            // Lo = 0.5 + 0.5 * cos(length(uniforms.light_position - position_world));
            // Lo = 1.0 / (1.0 + length_squared(cross(normalize(fragment_to_light), fragment_to_camera)));
            
            float3 y = length(cross(V, fragment_to_light));
            float3 x = dot(V, fragment_to_light);
            
            //Lo = x;
            
            // we want to integrate
            //
            // 1 / (x^2 + y^2) over x to camera (assumed far from point light)
            
            // atan(x/y) / y
            
            Lo += 0.1 * (M_PI_2_F + atan2(x, y)) / y * radianceScatter;
            
            
            // we want to know the distance from the light to the line
            // that is given by
            // float strength = 0.01 / (0.01 + length_squared(cross(normalize(direction), fragment_to_light)));
            
            // And we want to know the distance along the line that we are
            // integrating the scattering
            
            //float distance = dot(normalize(direction), fragment_to_light) / 0.1;
            //Lo += strength * (1.0 + atan(distance)) * uniforms.radiance;
            //Lo = (M_PI_2_F + atan(distance)) * strength;
            // Lo = distance_to_ray_squared;
        }
        
        
        
        
        out.color.rgb = half3(Lo);
        out.color.a = 1.0h;
        out.color = clamp(out.color, 0.0f, HALF_MAX);
        
        return out;
        
    }
    
    
    // "decal"-like things
    //
    // simplest: screen-space overlay
    //     example: GUI
    //     implementation: in postprocessing pass
    // "augmented-reality": respects Z, does not interact with lighting
    //     example: glyph that is free-floating but associated with geometry
    //     implementation: conventional depth-tested polygon
    // "projected-ar": respects Z, projected onto real surface, no lighting
    //     example: glyph wrapped on its associated geometry
    //     implementation: draw bounding surface, lookup depth, compute
    //     coordinate, clip discard, lookup texture, alpha discard, apply
    // "greeble": respects Z, projected onto real surface, alters material
    //     properties before lighting applied; example: stickers, tire tracks
    
    
    [[fragment]] LightingFragmentFunctionOutput
    decal_fragment_function(LightingVertexFunctionOutput in [[stage_in]],
                            FragmentFunctionOutput gbuffer,
                            constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]],
                            texture2d<float> decal_texture [[texture(AAPLTextureIndexColor)]]) {
        
        LightingFragmentFunctionOutput out;

        // intersect the camera ray with the depth buffer
        
        float  depth          = gbuffer.depth;
        float4 position_world = in.near_world + uniforms.inverse_viewprojection_transform.columns[2] * depth;

        // project the world to texture coordinate space
        
        // inverse_model_transform - per instance
        // inverse_texture_transform - also per instance?
        
        // what is the relationship with tangent/bitangent/normal
        
        // discard out of bounds
        
        // write color
        
        return out;
        
    }
    
    
    
    
    
} // namespace deferred



#pragma mark - Cube map filtering

struct CubeFilterVertexOut {
    float4 position [[position]];
    float4 normal;
    ushort face [[render_target_array_index]];
    ushort row;
};



[[vertex]] CubeFilterVertexOut
CubeFilterVertex(ushort i [[vertex_id]],
                 ushort j [[instance_id]],
                 const device float4 *v [[buffer(AAPLBufferIndexVertices)]],
                 constant CubeFilterUniforms &u [[buffer(AAPLBufferIndexUniforms)]])
{
    
    CubeFilterVertexOut out;
    out.position = v[i];
    out.face = j % 6;
    out.row = j / 6;
    out.normal = u.transforms[out.face] * out.position;
    
    return out;
    
}

[[fragment]] float4
CubeFilterAccumulate3(CubeFilterVertexOut in [[stage_in]],
                      constant CubeFilterUniforms& uniforms [[ buffer(AAPLBufferIndexUniforms)]],
                      texture2d<float> environmentMap [[ texture(AAPLTextureIndexColor) ]])
{
    
    constexpr sampler bilinearSampler(mag_filter::linear,
                                      min_filter::linear,
                                      s_address::repeat,
                                      t_address::clamp_to_edge
                                      );
    
    float3 N = normalize(in.normal.xyz);
    float3 V = N;
    
    float alpha2 = uniforms.alpha2;
    
    // construct sample coordinate system
    float3 U = (N.z < 0.5) ? float3(0, 0, 1) : float3(0, 1, 0);
    float3x3 T;
    T.columns[0] = normalize(cross(N, U));
    T.columns[1] = normalize(cross(N, T.columns[0]));
    T.columns[2] = N;
    
    
    float3 sum = 0;
    ushort M = 16384;
    for (ushort i = 0; i != M; ++i) {
        float2 X = sequence_Hammersley(i, M);
        float3 H = T * sample_Trowbridge_Reitz(X, alpha2);
        float3 R = reflect(-V, H);
        float phi = atan2(R.y, R.x) * 0.5 * M_1_PI_F;
        float theta = acos(R.z) * M_1_PI_F;
        // float4 environmentalSample = environmentMap.sample(bilinearSampler, R);
        float4 environmentalSample = environmentMap.sample(bilinearSampler, float2(phi, theta));
        sum += select(environmentalSample.rgb, float3(0), isinf(environmentalSample.rgb)) / M;
    }
    
    return float4(sum, 1.0f);
}








// note:
//
// allegedly: prefer to have vertices in their own buffer separate from other
// interpolants, and do some simple stereotypical operation on them (like
// a single matrix transform).  this lets the gpu use a fast fixed-function
// pipeline for the vertices





struct whiskerVertexOut {
    float4 clipSpacePosition [[position]];
    float4 color;
};

[[vertex]] whiskerVertexOut
whiskerVertexShader(uint vertexID [[ vertex_id ]],
                    uint instanceID [[ instance_id ]],
                    const device float4 *vertexArray [[buffer(AAPLBufferIndexVertices)]],
                    constant MeshUniforms &uniforms  [[buffer(AAPLBufferIndexUniforms)]],
                    const device MeshInstanced *instancedArray [[buffer(AAPLBufferIndexInstanced)]])
{
    whiskerVertexOut out;
    
    float4 worldSpacePosition = instancedArray[instanceID].model_transform * vertexArray[vertexID];
    out.clipSpacePosition = uniforms.viewprojection_transform * worldSpacePosition;
    
    out.color = 0.0f;
    out.color[(vertexID >> 1) % 3] = 1.0f;
    
    return out;
}

struct whiskerFragmentOut {
    half4 color [[color(AAPLColorIndexColor), raster_order_group(AAPLRasterOrderGroupLighting) ]];
};

[[fragment]] whiskerFragmentOut
whiskerFragmentShader(whiskerVertexOut in [[stage_in]],
                      constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]])
{
    whiskerFragmentOut out;
    out.color = half4(in.color);
    return out;
}





struct pointsVertexOut {
    float4 clipSpacePosition [[position]];
    float4 color;
    float point_size [[point_size]];
};

[[vertex]] pointsVertexOut
pointsVertexShader(uint vertexID [[ vertex_id ]],
                    uint instanceID [[ instance_id ]],
                    const device MeshVertex *vertexArray [[buffer(AAPLBufferIndexVertices)]],
                    constant MeshUniforms &uniforms  [[buffer(AAPLBufferIndexUniforms)]],
                   const device MeshInstanced *instancedArray [[buffer(AAPLBufferIndexInstanced)]])
{
    pointsVertexOut out;
    
    float4 worldSpacePosition = instancedArray[instanceID].model_transform * vertexArray[vertexID].position;
    out.clipSpacePosition = uniforms.viewprojection_transform * worldSpacePosition;

    out.color = 1.0f;
    out.point_size = 8.0f;
    
    return out;
}

struct pointsFragmentOut {
    half4 color [[color(AAPLColorIndexColor), raster_order_group(AAPLRasterOrderGroupLighting) ]];
};

[[fragment]] pointsFragmentOut
pointsFragmentShader(pointsVertexOut in [[stage_in]],
                      constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]])
{
    pointsFragmentOut out;
    out.color = half4(in.color);
    return out;
}







// Vertex shader outputs and per-fragment inputs
struct RasterizerData
{
    float4 clipSpacePosition [[position]];
    float2 texCoord;
    float4 color;
    float3 light_direction;
};

[[vertex]] RasterizerData
vertexShader(uint vertexID [[ vertex_id ]],
             const device MyVertex *vertexArray [[ buffer(AAPLBufferIndexVertices) ]],
             constant MyUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]])

{
    RasterizerData out;
    
    out.clipSpacePosition = float4(uniforms.position_transform
                                   * float4(vertexArray[vertexID].position, 0, 1));
    out.texCoord = vertexArray[vertexID].texCoord;
    
    // out.color = float4(vertexArray[vertexID].color) / 255;
    out.color = unpack_unorm4x8_srgb_to_float(vertexArray[vertexID].color);
    
    return out;
}

[[vertex]] RasterizerData
vertexShader4(uint vertexID [[ vertex_id ]],
              const device MyVertex4 *vertexArray [[ buffer(AAPLBufferIndexVertices) ]],
              constant MyUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]])

{
    RasterizerData out;
    
    out.clipSpacePosition = float4(uniforms.position_transform
                                   * float4(vertexArray[vertexID].position));
    out.texCoord = vertexArray[vertexID].texCoord;
    
    // out.color = float4(vertexArray[vertexID].color) / 255;
    out.color = unpack_unorm4x8_srgb_to_float(vertexArray[vertexID].color);
    
    return out;
}


/*
 fragment float4
 fragmentShader(RasterizerData in [[stage_in]])
 {
 return in.color;
 }
 */

struct basicFragmentShaderOut {
    half4 color [[color(AAPLColorIndexColor), raster_order_group(AAPLRasterOrderGroupLighting) ]];
};

// Fragment function
[[fragment]] basicFragmentShaderOut
fragmentShader(RasterizerData in [[stage_in]],
               texture2d<half> colorTexture [[ texture(AAPLTextureIndexColor) ]])
{
    constexpr sampler textureSampler (mag_filter::nearest,
                                      min_filter::nearest);
    
    // Sample the texture to obtain a color
    const half4 colorSample = colorTexture.sample(textureSampler, in.texCoord);
    
    // return the color of the texture
    return { colorSample * half4(in.color) };
    // return float4(0.5, 0.5, 0.5, 0.5);
}

// Fragment function
[[fragment]] float4
fragmentShader_sdf(RasterizerData in [[stage_in]],
                   texture2d<half> colorTexture [[ texture(AAPLTextureIndexColor) ]])
{
    constexpr sampler textureSampler (mag_filter::linear,
                                      min_filter::linear);
    
    // signed distance fields are 128 at edge
    // +/- 16 per pixel
    // aka, the slope is 16/255
    
    // aka, the distance is in texels+8, in 4.4 fixed point
    //
    // so, * 255.0 takes us back to original uchar value (linear interpolated)
    // - 128.0 to put the zero where it should be
    // / 16.0 to give signed distance in texels
    
    // one we have signed distance in texels,
    // * $magnification_factor to convert to signed distance in screen pixels
    // this is * 10 in current hardcoded demo
    // how to do in general?  when anisotropic?  when perspective?
    // map [-0.5, +0.5] to [0, 1]
    
    
    // Sample the texture to obtain a color
    const half4 colorSample = colorTexture.sample(textureSampler, in.texCoord);
    
    const half4 colorSample2 = colorTexture.sample(textureSampler, in.texCoord + float2(0,-0.001));
    
    // return the color of the texture
    // return float4(colorSample) * in.color;
    // return float4(0.5, 0.5, 0.5, 0.5);
    
    float a = saturate((colorSample.a * 255.0 - 128.0) / 16.0 * 10.0 + 0.5);
    float b = saturate((colorSample.a * 255.0 - 128.0 + 16.0 * 16.0 / 10.0)  / 16.0 * 10.0 + 0.5);
    float c = saturate((colorSample2.a * 255.0 - 64.0)  / 64.0);
    // return in.color * saturate((colorSample.a * 255.0 - 128.0) / 16.0 * 10.0 + 0.5);
    return in.color * float4(a, a, a, c * (1 - b) + b);
}


struct TrivialVertexFunctionOutput {
    float4 position [[position]];
    float4 coordinate;
    
};

[[vertex]] TrivialVertexFunctionOutput TrivialVertexFunction(uint vertex_id [[vertex_id]],
                                                             const device float4* vertices [[ buffer(AAPLBufferIndexVertices)]]) {
    float4 position = vertices[vertex_id];
    TrivialVertexFunctionOutput out;
    out.position = position;
    out.coordinate.x = position.x * 0.5f + 0.5f;
    out.coordinate.y = position.y * -0.5f + 0.5f;
    return out;
}

[[fragment]] half4 TrivialFragmentFunction(TrivialVertexFunctionOutput stage_in [[stage_in]],
                                            texture2d<half> texture [[ texture(AAPLTextureIndexColor) ]]) {
    constexpr sampler linearSampler(mag_filter::linear,
                                    min_filter::linear);
    return texture.sample(linearSampler, stage_in.coordinate.xy);
}


constant bool has_per_draw_position_transform [[function_constant(AAPLFunctionConstantIndexHasPerDrawPositionTransform)]];
constant bool has_per_instance_position_transform [[function_constant(AAPLFunctionConstantIndexHasPerInstancePositionTransform)]];
constant bool has_per_draw_coordinate_transform [[function_constant(AAPLFunctionConstantIndexHasPerDrawCoordinateTransform)]];
constant bool has_per_instance_coordinate_transform [[function_constant(AAPLFunctionConstantIndexHasPerInstanceCoordinateTransform)]];
constant bool has_per_draw_color_transform [[function_constant(AAPLFunctionConstantIndexHasPerDrawColorTransform)]];

struct direct_vertex_per_draw {
    float4x4 position_transform;
    float4x4 coordinate_transform;
};

struct direct_vertex_per_instance {
    float4x4 position_transform;
    float4x4 coordinate_transform;
};

struct direct_vertex_stage_in {
    float4 position [[attribute(AAPLAttributeIndexPosition)]];
    float4 coordinate [[attribute(AAPLAttributeIndexCoordinate)]];
};

struct direct_fragment_per_draw {
    half4x4 color_transform;
};

struct direct_fragment_stage_in {
    float4 position [[position]];
    float4 coordinate;
};

struct direct_fragment_stage_out {
    half4 color [[color(AAPLColorIndexColor)]];
};

[[vertex]] auto
direct_vertex_function(uint vertex_id [[vertex_id]],
                       uint instance_id [[instance_id]],
                       direct_vertex_stage_in stage_in [[stage_in]],
                       constant direct_vertex_per_draw& per_draw [[buffer(AAPLBufferIndexUniforms)]],
                       device const direct_vertex_per_instance* per_instance[[buffer(AAPLBufferIndexInstanced)]])
-> direct_fragment_stage_in
{
    direct_fragment_stage_in stage_out;
    
    float4 position;
    position = stage_in.position;
    
    float4x4 position_transform;
    if (has_per_draw_position_transform) {
        position_transform = per_draw.position_transform;
    }
    if (has_per_instance_position_transform) {
        position_transform = per_instance[instance_id].position_transform;
    }
    
    stage_out.position = position_transform * position;
    
    float4 coordinate;
    coordinate = stage_in.coordinate;

    float4x4 coordinate_transform;
    if (has_per_draw_coordinate_transform)
        coordinate_transform = per_draw.coordinate_transform;
    if (has_per_instance_coordinate_transform)
        coordinate = per_instance[instance_id].coordinate_transform * coordinate;
    stage_out.coordinate = coordinate;
    
    return stage_out;
};


[[fragment]] auto
direct_fragment_function(direct_fragment_stage_in stage_in [[stage_in]],
                         constant direct_fragment_per_draw& per_draw [[buffer(AAPLBufferIndexUniforms)]],
                         texture2d<half> texture_color [[texture(AAPLTextureIndexColor)]])
-> direct_fragment_stage_out {
    sampler bilinear(mag_filter::linear, min_filter::linear);
    direct_fragment_stage_out stage_out;
    half4 color;
    color = texture_color.sample(bilinear, stage_in.coordinate.xy);
    
    half4x4 color_transform;
    color_transform = per_draw.color_transform;
    color = color_transform * color;
    
    stage_out.color = color;
    return stage_out;
}





constant float3 kRec709Luma = float3(0.2126, 0.7152, 0.0722);

[[kernel]] void DepthProcessing(texture2d<float, access::write> outTexture [[texture(0)]],
                                texture2d<float, access::read> luminanceTexture [[texture(1)]],
                                texture2d<float, access::sample> maskTexture [[texture(2)]],
                                uint2 thread_position_in_grid [[thread_position_in_grid]]) {
    
    constexpr sampler linearSampler(mag_filter::linear,
                                    min_filter::linear,
                                    s_address::clamp_to_zero,
                                    t_address::clamp_to_zero);
    
    // (output) coordinates
    
    uint i = thread_position_in_grid.x;
    uint j = thread_position_in_grid.y;

    float4 result = 0.0f;
    
    uint w = outTexture.get_width();
    uint h = outTexture.get_height();
    
    float s = (i + 0.5) / w;
    float t = (j + 0.5) / h;

    float tanTheta = (t - 0.5) * 2.0 * 95.0 / 250.0;
    float f = 140.0;

    
    /*
     
     // debug output
     
    result.g = maskTexture.sample(linearSampler, float2(s, t)).r;
    result.r = luminanceTexture.read(uint2(i, j)).r;
    outTexture.write(result, thread_position_in_grid);
    return;
     
     */
    
    //result.g = luminanceTexture.read(uint2(i, j)).r;

    
    // simulation:

    /*
    float d = 250.0;
    float c = tanTheta * d;
    
    uint k = i;
    float a = (k + 0.5) / h * 220.0f * 2.0f + 10.0f;

    float b = mix(a, c, f / d) / 240.0f;
    
    result.b = maskTexture.sample(linearSampler, float2(0.5, b)).g;

    outTexture.write(result, thread_position_in_grid);
     */
    
    
    // computation
    
    float d = 200.0 + s * 240.0;
    float c = tanTheta * d;
    
    for (uint k = 0; k != h; ++k) {
        float a = (k + 0.5) / h * 220.0f + 10.0f;
        float b = mix(a, c, f / d) / 240.0f;
        
        float s1 = dot(luminanceTexture.read(uint2(k, j)).rgb, kRec709Luma);
        float s2 = maskTexture.sample(linearSampler, float2(0.5, b)).g;
                                         
        result += s1 * s2;
                   
    }
    
    // intuitively:
    //
    // we have proposed a specific point in space and a specifc light position
    // if the point is inside the shadow volume of the light, it should be dark
    // if the point is illuminated by the light, it will depend on the surface
    // properties, which we don't know
    //
    // thus, the brightness of a shadowed point counts against the hypothesis;
    // we get no depth information from a lit point.
    //
    // so, accumulate when shadowed
    
    // P(x) ~ e(-2x)
    //
    // int_0^1 e(-2x) dx = -1/2 e^(-2x) |_0^1 = -1/2 (e^-2) + 1/2 = 0.432

    result = 1.0 - exp(-result);
    

    outTexture.write(result, thread_position_in_grid);

    
}



// Draw a grid of squares, with geometry from the thread grid, and per-tile
// properties from a 2D index into some buffer

// All threads in a threadgroup share a single
//     object_data [[payload]]
//     metal::mesh

// Which is alarming
// threadgroups of only one thread are presumably a big waste

#define kMeshThreadgroups 32

struct GridObjectOutput {
    // User-defined payload; one entry for each mesh threadgroup. This
    // is an array because the data will be shared by the mesh grid.
    float value[kMeshThreadgroups];
};

// Only one thread per must write to mesh_grid_properties

[[object, max_total_threadgroups_per_mesh_grid(kMeshThreadgroups)]]
void GridObjectFunction(uint2 threadgroup_size [[threads_per_threadgroup]],
                        uint lane [[thread_index_in_threadgroup]],
                        object_data GridObjectOutput& output [[payload]],
                        mesh_grid_properties mgp) {
    
}

struct vertex_t {
    float4 position [[position]];
    float2 coordinate;
    // other user-defined properties
};
struct primitive_t {
    float3 normal;
};
// A mesh declaration that can export one cube.
using tile_mesh_t = metal::mesh<deferred::VertexFunctionOutput, primitive_t, 8 /*corners*/, 6*2 /*faces*/, metal::topology::triangle>;

// "uniform"
struct view_info_t {
    float4x4 view_proj;
};

// from Object shader
struct cube_info_t {
    float4x3 world;
    float3 color;
};




// [[payload]] is common to all threads in the threadgroup
// mesh<...> is common to all threads in the threadgroup

// do we actually need any payload compute?

[[mesh, max_total_threads_per_threadgroup(12)]]
void GridMeshFunction(tile_mesh_t output,
                const object_data GridObjectOutput &cube [[payload]],
                constant view_info_t &view [[buffer(0)]],
                // uint2 gid [[threadgroup_position_in_grid]],
                // uint lane [[thread_index_in_threadgroup]]
                      uint2 thread_position_in_grid [[thread_position_in_grid]]
                      ) {
    
    
    
}



#if 0





[[fragment]] float4
CubeFilterAccumulate(CubeFilterVertexOut in [[stage_in]],
                     constant CubeFilterUniforms& uniforms [[ buffer(AAPLBufferIndexUniforms)]],
                     texturecube<float> environmentMap [[ texture(AAPLTextureIndexColor) ]])
{
    
    float3 n = normalize(in.normal.xyz);
    float4 w = float4(0,0,0,1);
    float3 sum = 0;
    float weight = 0;
    ushort2 coord;
    ushort face;
    ushort q = environmentMap.get_width();
    
    // cube filter by explicit summation over environment pixels
    
    for (face = 0; face != 6; ++face) {
        coord.y = in.row;
        // for (coord.y = 0; coord.y != q; ++coord.y) {
        for (coord.x = 0; coord.x != q; ++coord.x) {
            float4 environmentSample = environmentMap.read(coord.xy, face, 0);
            w.xy = (float2(coord.xy) + 0.5f) * 2 / q - 1.0f;
            w.y = -w.y;
            float3 v = normalize((uniforms.transforms[face] * w).xyz);
            
            float k = dot(v, n);
            v = (v - k * n) / uniforms.alpha2 + k * n;
            v = normalize(v);
            
            // weight by (original) angle of emission
            float u = w.w / length(w);
            
            // weight by (adjusted) angle of incidence
            float t = saturate(dot(v, n));
            
            float s = u * t;
            
            weight += s;
            sum += environmentSample.rgb * s;
            
        }
        // }
    }
    
    return float4(sum, weight);
}




[[fragment]] float4
CubeFilterAccumulate2(CubeFilterVertexOut in [[stage_in]],
                      constant CubeFilterUniforms& uniforms [[ buffer(AAPLBufferIndexUniforms)]],
                      texturecube<float> environmentMap [[ texture(AAPLTextureIndexColor) ]])
{
    
    constexpr sampler bilinearSampler(mag_filter::linear,
                                      min_filter::linear);
    
    float3 N = normalize(in.normal.xyz);
    float3 V = N;
    
    float alpha2 = uniforms.alpha2;
    
    // construct sample coordinate system
    float3 U = (N.z < 0.5) ? float3(0, 0, 1) : float3(0, 1, 0);
    float3x3 T;
    T.columns[0] = normalize(cross(N, U));
    T.columns[1] = normalize(cross(N, T.columns[0]));
    T.columns[2] = N;
    
    
    float3 sum = 0;
    ushort M = 16384;
    for (ushort i = 0; i != M; ++i) {
        float2 X = sequence_Hammersley(i, M);
        float3 H = T * sample_Trowbridge_Reitz(X, alpha2);
        float3 R = reflect(-V, H);
        float4 environmentalSample = environmentMap.sample(bilinearSampler, R);
        sum += environmentalSample.rgb;
    }
    
    return float4(sum / M, 1.0f);
}


[[fragment]] float4
CubeFilterNormalize(CubeFilterVertexOut in [[stage_in]],
                    constant CubeFilterUniforms& uniforms [[ buffer(AAPLBufferIndexUniforms)]],
                    texturecube<float> environmentMap [[ texture(AAPLTextureIndexColor) ]])
{
    constexpr sampler nearestSampler(mag_filter::nearest, min_filter::nearest);
    float4 environmentSample = environmentMap.sample(nearestSampler, in.normal.xyz);
    return float4(environmentSample.rgb / environmentSample.a, 1);
}



#endif
