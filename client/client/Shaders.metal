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

enum AAPLRasterOrderGroup {
    AAPLRasterOrderGroupGBuffer,
    AAPLRasterOrderGroupLighting,
};

# pragma mark - Deferred physically-based rendering

// Physically-Based Rendering functions
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
// which describes the lighting model used in Wreck-It Ralph by Disney

struct DeferredGBufferVertexShaderOutput
{
    float4 clipSpacePosition [[position]];
    float4 worldSpacePosition;
    float4 eyeSpacePosition;
    float4 lightSpacePosition;
    float4 coordinate;
    float4 worldNormal;
    float4 worldTangent;
    float4 worldBitangent;
};

struct DeferredGBufferFragmentShaderOutput {
    half4 light [[color(AAPLColorIndexColor), raster_order_group(AAPLRasterOrderGroupLighting)]];
    half4 albedoMetallic [[color(AAPLColorIndexAlbedoMetallic), raster_order_group(AAPLRasterOrderGroupGBuffer)]];
    half4 normalRoughness [[color(AAPLColorIndexNormalRoughness), raster_order_group(AAPLRasterOrderGroupGBuffer)]];
    float depth [[color(AAPLColorIndexDepth), raster_order_group(AAPLRasterOrderGroupGBuffer)]];
};


[[vertex]] DeferredGBufferVertexShaderOutput
DeferredGBufferVertexShader(uint vertex_id [[ vertex_id ]],
                            uint instance_id [[ instance_id ]],
                            constant MeshUniforms &uniforms  [[buffer(AAPLBufferIndexUniforms)]],
                            const device MeshVertex *vertexArray [[buffer(AAPLBufferIndexVertices)]],
                            const device MeshInstanced *instancedArray [[buffer(AAPLBufferIndexInstanced)]])
{
    
    DeferredGBufferVertexShaderOutput out;
    
    MeshVertex in = vertexArray[vertex_id];
    MeshInstanced instance = instancedArray[instance_id];

    out.coordinate = in.coordinate;

    out.worldSpacePosition = instance.model_transform * in.position;
    out.eyeSpacePosition   = uniforms.view_transform * out.worldSpacePosition;
    out.clipSpacePosition  = uniforms.viewprojection_transform * out.worldSpacePosition;
    out.lightSpacePosition = uniforms.light_viewprojection_transform * out.worldSpacePosition;
    out.worldTangent       = instance.inverse_transpose_model_transform * in.tangent;
    out.worldBitangent     = instance.inverse_transpose_model_transform * -in.bitangent;
    out.worldNormal        = instance.inverse_transpose_model_transform * in.normal;

    return out;
}

[[fragment]] DeferredGBufferFragmentShaderOutput
DeferredGBufferFragmentShader(DeferredGBufferVertexShaderOutput in [[stage_in]],
                              bool front_facing [[front_facing]],
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
    
    half4 albedoSample = albedoTexture.sample(trilinearSampler, in.coordinate.xy);
    half4 normalSample = normalTexture.sample(trilinearSampler, in.coordinate.xy);
    half4 roughnessSample = roughnessTexture.sample(trilinearSampler, in.coordinate.xy);
    half4 metallicSample = metallicTexture.sample(trilinearSampler, in.coordinate.xy);
    half4 emissiveSample = emissiveTexture.sample(trilinearSampler, in.coordinate.xy);
    
    normalSample = normalSample * 2.0h - 1.0h;

    half3 normal = normalize(half3x3(half3(in.worldTangent.xyz),
                                     half3(in.worldBitangent.xyz),
                                     half3(in.worldNormal.xyz)) * normalSample.xyz);

    DeferredGBufferFragmentShaderOutput out;
    
    out.light = front_facing ? emissiveSample : half4(1.0h, 0.0h, 1.0h, 0.0h);
    out.albedoMetallic.rgb = front_facing ? albedoSample.rgb : 0.0h;
    out.albedoMetallic.a = front_facing ? metallicSample.r : 0.0h;
    out.normalRoughness.xyz = front_facing ? normal : 0.0h;
    out.normalRoughness.w = front_facing ? roughnessSample.r : 1.0h;
    out.depth = in.eyeSpacePosition.z;
    
    return out;
   
}







// Hammersley low-discrepency sequence for quasi-Monte-Carlo integration

float2 HammersleySequence(ushort i, ushort N) {
    return float2(float(i) / float(N), reverse_bits(i) * 0.00001525878f);
}


// Microfacet distribution

float DistributionTrowbridgeReitz(float NdotH, float alpha2) {
    float nh2 = NdotH * NdotH;
    float d = (nh2 * (alpha2 - 1.0f) + 1.0f);
    float d2 = d * d;
    float d3 = M_PI_F * d2;
    return alpha2 / d3;
}

float3 SampleTrowbridgeReitz(float2 chi, float alpha2) {
    
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

float GeometrySchlick(float NdotV, float k) {
    return NdotV / (NdotV * (1.0f - k) + k);
}

float GeometrySmithK(float NdotV, float NdotL, float k) {
    float viewFactor = GeometrySchlick(NdotV, k);
    float lightFactor = GeometrySchlick(NdotL, k);
    return viewFactor * lightFactor;}

float GeometrySmithPoint(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0f;
    float k = r * r / 8.0f;
    return GeometrySmithK(NdotV, NdotL, k);
}

float GeometrySmithArea(float NdotV, float NdotL, float roughness) {
    float k = roughness * roughness / 2.0f;
    return GeometrySmithK(NdotV, NdotL, k);
}


// Fresnel factor

// Increased reflectivity at glancing angles; tends to brighten edges

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0 - roughness), F0) - F0) * pow(saturate(1.0f - cosTheta), 5.0);
}


struct LightingVertexInput {
    float4 position;
};

struct LightingVertexOutput {
    float4 position [[position]];
    float3 direction;
};



[[vertex]] LightingVertexOutput
meshLightingVertex(uint vertexID [[ vertex_id ]],
                   const device float4 *vertexArray [[ buffer(AAPLBufferIndexVertices) ]],
                   constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]])

{
    LightingVertexOutput out;
    out.position = vertexArray[vertexID];
    float4 direction = float4(out.position.x, out.position.y, 1.0, 0.0);
    out.direction = (uniforms.inverse_view_transform * direction).xyz;
    return out;
}


struct LightingFragmentOutput {
    half4 color [[color(AAPLColorIndexColor), raster_order_group(AAPLRasterOrderGroupLighting)]];
};

[[fragment]] LightingFragmentOutput
meshLightingFragment(LightingVertexOutput in [[stage_in]],
                     DeferredGBufferFragmentShaderOutput gbuffer,
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

    LightingFragmentOutput out;
    
    float3 albedo = float3(gbuffer.albedoMetallic.rgb);
    float metallic = float(gbuffer.albedoMetallic.a);
    float3 normal = float3(gbuffer.normalRoughness.xyz);
    float roughness = float(gbuffer.normalRoughness.w);
    float occlusion = 1.0f;

    float3 V = -normalize(in.direction.xyz);
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
    
    float3 F = FresnelSchlickRoughness(NdotV,
                                       F0,
                                       roughness);

    float3 kS = F;
    float3 kD = 1.0 - kS;
    
    float3 diffuse = albedo * diffuseSample.rgb * M_1_PI_F;
    
    float3 specular = (F * fresnelSample.r + fresnelSample.g) * reflectedSample.rgb;

    float3 Lo = (kD * diffuse + specular) * occlusion * uniforms.ibl_scale.rgb;
    
    out.color.rgb = half3(Lo);

    return out;
        
}


[[fragment]] LightingFragmentOutput
meshPointLightFragment(LightingVertexOutput in [[stage_in]],
                       DeferredGBufferFragmentShaderOutput gbuffer,
                       constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]],
                       texture2d<float> shadowTexture [[texture(AAPLTextureIndexShadow)]])
{
    
    constexpr sampler nearestSampler(mag_filter::nearest,
                                     min_filter::nearest,
                                     s_address::clamp_to_edge,
                                     t_address::clamp_to_edge);
    
    LightingFragmentOutput out;
    
    float3 albedo = float3(gbuffer.albedoMetallic.rgb);
    float metallic = float(gbuffer.albedoMetallic.a);
    float3 N = float3(gbuffer.normalRoughness.xyz);
    float roughness = float(gbuffer.normalRoughness.w);
    float occlusion = 1.0f;
    float depth = gbuffer.depth;

    float3 V = -normalize(in.direction.xyz);
    float3 L = -uniforms.light_direction;
    float3 H = normalize(V + L);
    
    float4 position = float4(in.direction * depth, 0) + uniforms.origin;
    float4 lightSpacePosition = uniforms.light_viewprojection_transform * position;
        
    float shadowSample = shadowTexture.sample(nearestSampler, lightSpacePosition.xy).r;
    float shadowFactor = step(lightSpacePosition.z, shadowSample);
    
    float3 F0 = 0.04f;
    F0 = mix(F0, albedo, metallic);
        
    float HdotV = saturate(dot(H, V));
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));

    // distribution factor
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float D = DistributionTrowbridgeReitz(NdotH, alpha2);
    
    // geometry factor
    float G = GeometrySmithPoint(NdotV, NdotL, roughness);
    
    // fresnel factor
    float3 F = FresnelSchlick(HdotV, F0);
    
    float3 numerator = D * G * F;
    float denominator = 4.0f * NdotV * NdotL + 0.0001f;
    float3 specular = numerator / denominator;
    
    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0 - metallic);
    
    float3 Lo = (kD * albedo * M_1_PI_F + specular) * NdotL * uniforms.radiance * shadowFactor;
    
    out.color.rgb = half3(Lo);
    out.color.a = 1.0h;
    
    return out;
    
}







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
        float2 X = HammersleySequence(i, M);
        float3 H = T * SampleTrowbridgeReitz(X, alpha2);
        float3 R = reflect(-V, H);
        float4 environmentalSample = environmentMap.sample(bilinearSampler, R);
        sum += environmentalSample.rgb;
    }
    
    return float4(sum / M, 1.0f);
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
        float2 X = HammersleySequence(i, M);
        float3 H = T * SampleTrowbridgeReitz(X, alpha2);
        float3 R = reflect(-V, H);
        float phi = atan2(R.z, R.x) * 0.5 * M_1_PI_F;
        float theta = acos(R.y) * M_1_PI_F;
        // float4 environmentalSample = environmentMap.sample(bilinearSampler, R);
        float4 environmentalSample = environmentMap.sample(bilinearSampler, float2(phi, theta));
        sum += select(environmentalSample.rgb, float3(0), isinf(environmentalSample.rgb)) / M;
    }
    
    return float4(sum, 1.0f);
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






// note:
//
// allegedly: prefer to have vertices in their own buffer separate from other
// interpolants, and do some simple stereotypical operation on them (like
// a single matrix transform).  this lets the gpu use a fast fixed-function
// pipeline for the vertices


// should this just be a kernel op?

[[vertex]] float4
SplitSumVertex(ushort i [[vertex_id]],
                      const device float4* vertices [[buffer(AAPLBufferIndexVertices)]]) {
    return vertices[i];
}

[[fragment]] float4
SplitSumFragment(float4 position [[position]]) {
    
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
        float2 X = HammersleySequence(i, M);
        float3 H = SampleTrowbridgeReitz(X, alpha2);
        float3 L = reflect(-V, H);
        float NdotH = saturate(H.z);
        float NdotL = saturate(L.z);
        float VdotH = saturate(dot(V, H));
        if (NdotL > 0) {
            float G = GeometrySmithArea(NdotV, NdotL, roughness);
            float G_vis = G * VdotH / (NdotV * NdotH);
            float Fc = pow(1.0 - VdotH, 5);
            scale += (1.0 - Fc) * G_vis;
            offset += Fc * G_vis;
            
        }
    }
    return float4(scale / M, offset / M, 0.0f, 0.0f);
    
}





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
    // so, * 255.0 takes us back to original u8 value (linear interpolated)
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

