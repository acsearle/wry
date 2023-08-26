//
//  Shaders.metal
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

/*
 See LICENSE folder for this sample’s licensing information.
 
 Abstract:
 Metal shaders used for this sample
 */

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

// Include header shared between this Metal shader code and C code executing Metal API commands
#include "ShaderTypes.h"

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

// Fragment function
[[fragment]] float4
fragmentShader(RasterizerData in [[stage_in]],
               texture2d<half> colorTexture [[ texture(AAPLTextureIndexColor) ]])
{
    constexpr sampler textureSampler (mag_filter::nearest,
                                      min_filter::nearest);
    
    // Sample the texture to obtain a color
    const half4 colorSample = colorTexture.sample(textureSampler, in.texCoord);
    
    // return the color of the texture
    return float4(colorSample) * in.color;
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





// Vertex shader outputs and per-fragment inputs
struct MeshRasterizerData
{
    float4 clipSpacePosition [[position]];
    float4 worldSpacePosition;
    float4 eyeSpacePosition;
    float4 lightSpacePosition;
    float2 texCoord;
    float3 normal;
    float3 tangent;
};

[[vertex]] MeshRasterizerData
meshVertexShader(uint vertexID [[ vertex_id ]],
                 uint instanceID [[ instance_id ]],
              const device MeshVertex *vertexArray [[ buffer(AAPLBufferIndexVertices) ]],
              constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]])

{
    MeshRasterizerData out;
    
    out.worldSpacePosition = uniforms.model_transform * vertexArray[vertexID].position;
    out.eyeSpacePosition = uniforms.view_transform * out.worldSpacePosition;
    out.clipSpacePosition = uniforms.viewprojection_transform * out.worldSpacePosition;
    out.lightSpacePosition = uniforms.light_viewprojection_transform * out.worldSpacePosition;
    out.texCoord = vertexArray[vertexID].texCoord;
    out.normal = vertexArray[vertexID].normal.xyz;
    out.tangent = vertexArray[vertexID].tangent.xyz;

    return out;
}

struct MeshFragmentOutput {
    half4 color [[color(AAPLColorIndexColor)]];
    half4 albedoMetallic [[color(AAPLColorIndexAlbedoMetallic)]];
    half4 normalRoughness [[color(AAPLColorIndexNormalRoughness)]];
    float depth [[color(AAPLColorIndexDepthAsColor)]];
};

// BRDF components

// learnopengl.com/PBR

float DistributionGGX(float3 normal, float3 halfway, float roughness) {
    float alpha = roughness * roughness;
    float a2 = alpha * alpha;
    float nh = saturate(dot(normal, halfway));
    float nh2 = nh * nh;
    float d = (nh2 * (a2 - 1.0f) + 1.0f);
    float d2 = d * d;
    float d3 = M_PI_F * d2;
    return a2 / d3;
}

// for point lights only
float GeometrySchlickGGXPoint(float NdotV, float roughness) {
    float r = roughness + 1.0f;
    float k = r * r / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + 1.0f);
}

float GeometrySmithPoint(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));
    float ggx1 = GeometrySchlickGGXPoint(NdotV, roughness);
    float ggx2 = GeometrySchlickGGXPoint(NdotL, roughness);
    
    return ggx1 * ggx2;
}

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness), F0) - F0) * pow(saturate(1.0f - cosTheta), 5.0);
}

[[fragment]] MeshFragmentOutput
meshFragmentShader(MeshRasterizerData in [[stage_in]],
                   constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]],
                   texture2d<float> colorTexture [[texture(AAPLTextureIndexColor) ]],
                   texture2d<float> normalTexture [[texture(AAPLTextureIndexNormal)]],
                   texture2d<float> roughnessTexture [[texture(AAPLTextureIndexRoughness)]],
                   texture2d<float> metallicTexture [[texture(AAPLTextureIndexMetallic)]],
                   texture2d<float> shadowTexture [[texture(AAPLTextureIndexShadow)]],
                   texturecube<float> environmentTexture [[texture(AAPLTextureIndexEnvironment)]])

{
    constexpr sampler bilinearSampler(mag_filter::linear,
                                    min_filter::linear,
                                    s_address::repeat,
                                    t_address::repeat);

    constexpr sampler nearestSampler(mag_filter::nearest,
                                     min_filter::nearest,
                                     s_address::clamp_to_edge,
                                     t_address::clamp_to_edge);

    constexpr sampler trilinearSampler(mag_filter::linear,
                                       min_filter::linear,
                                       mip_filter::linear,
                                       s_address::repeat,
                                       t_address::repeat);


    MeshFragmentOutput out;
    
    // Sample the texture to obtain a color
    float4 colorSample = colorTexture.sample(bilinearSampler, in.texCoord);
    // Sample the texture to obtain a normal
    float4 normalSample = normalTexture.sample(bilinearSampler, in.texCoord);
    // Sample the rougness texture
    float4 roughnessSample = roughnessTexture.sample(bilinearSampler, in.texCoord);
    // Sample the metallic texture
    float4 metallicSample = metallicTexture.sample(bilinearSampler, in.texCoord);

    float shadowSample = shadowTexture.sample(nearestSampler, in.lightSpacePosition.xy).r;

    // hack in white
    //colorSample = float4(1.0f, 1.0f, 0.0f, 1.0f) * 0.3;
    // hack in flat
    //normalSample = mix(normalSample, float4(0.5, 0.5, 1.0, 0.0), 0.0f);
    // hack in rough
    //roughnessSample.r = 0.03;
    // hack in plastic
    //metallicSample.r = 1;
    float ao = 1.0; // ambient occlusion
    
    // Fresnel perpendicular reflectance
    float3 F0 = 0.04f;
    F0 = mix(F0, colorSample.rgb, metallicSample.r);
    
        
    
    
    // transform normal around
    float3 bitangent = cross(in.tangent, in.normal);
    float3 normal = normalSample.xyz * 2.0f - 1.0f;
    normal = float3x3(in.tangent, bitangent, in.normal) * normal;
    normal = normalize((uniforms.inverse_model_transform * float4(normal, 1.0f)).xyz);
    
    // view direction in world coordinates
    float3 view = normalize((uniforms.camera_world_position - in.worldSpacePosition).xyz);
    
    // half direction
    float3 halfway = normalize(view - uniforms.light_direction);
    
    float3 reflected = reflect(-view, normal);
    

    // float3 ambientSample = environmentLookup(normal, 1.0);
    
    float lod = log2(roughnessSample.r) + 4;
    float4 ambientSample = environmentTexture.sample(trilinearSampler, normal, level(5));
    float4 reflectedSample = environmentTexture.sample(trilinearSampler, reflected, level(lod));

    
    // simple lighting:
    
    // blinn-phong
    // float specular = pow(max(dot(normal, halfway), 0.0f), 64);
    // basic diffuse lighting term
    // float lambertian = max(dot(normal.xyz, -uniforms.light_direction), 0.0f);
    
    float shadowFactor = step(in.lightSpacePosition.z, shadowSample);

    // BRDF
    float3 Lo = 0.0f;
    
    // for the sun light
    {
        float cosTheta = saturate(dot(normal, halfway));
        float NdotL = saturate(dot(normal, -uniforms.light_direction));

        // microfacet distribution factor
        float NDF = DistributionGGX(normal, halfway, roughnessSample.r);
        
        // geometry factor
        float G = GeometrySmithPoint(normal, view, -uniforms.light_direction, roughnessSample.r);

        float3 F = fresnelSchlick(cosTheta, F0);
        
        float3 numerator = NDF * G * F;
        float denominator = 4.0f * dot(normal, view) * NdotL + 0.0001f;
        float3 specular = numerator / denominator;

        float3 kS = F;
        float3 kD = (1.0f - kS) * (1.0 - metallicSample.r);

        // float radiance = 3.0f; // of light source
        float3 radiance = float3(4, 3, 2);
        Lo += (kD * colorSample.xyz * M_1_PI_F + specular) * NdotL * radiance * shadowFactor;
        
    }
    
    // for environmental illumination
    {
        // how reflective are we
        float3 F = fresnelSchlickRoughness(saturate(dot(normal, view)), F0, roughnessSample.r);
        float3 kS = F;
        float3 kD = 1.0 - kS;
        float3 irradiance = ambientSample.rgb;
        float3 diffuse    = irradiance * colorSample.rgb;

        float3 specular = 1.0f * reflectedSample.rgb * F;
        
        float3 ambient = (kD * diffuse + specular) * ao;
        Lo += ambient;
                
    }
    
    
    
    
    float3 color = Lo;
    // hdr
    // color = color / (color + 1.0f);
    // gamma
    // color = pow(color, 1.0f / 2.2f);
    
    
    // out.color = half4(shadowFactor, shadowFactor, shadowFactor, 1.0f);
    out.color = half4(half3(color), 1.0f);
    // out.color += half4(half3(environmentLookup(reflected, 0.001f)), 1.0f) * 0.5;
    out.albedoMetallic = half4(half3(colorSample.rgb), metallicSample.r);
    out.normalRoughness = half4(half3(normal), roughnessSample.r);
    

    // shadowed
    //float c = lambertian * step(in.lightSpacePosition.z, shadowDepth);
    // can we make this more forgiving/smooth of glancing angle shadows?
    
    // float d = (normal.y + 1.0f) * 0.5f;
    // float e = (1.0f - normal.y) * 0.5f;
        
    // return half4(c * 0.5f + e * 0.5f, c * 0.25f + 0.25f, d * 0.5f, 1.0);
    
    // out.color = half4(c * 0.5f + e * 0.5f, c * 0.25f + 0.25f, d * 0.5f, 1.0);
    // out.color = colorSample;

    // out.color = half(c) * 0.5h + half(specular) * 0.5h;
    // out.normal = half4(half3(normal), 1.0);

    return out;
    // return half4(normal * 0.5 + 0.5);
    // return half4(half3(abs(normal).xyz), 1);
    // return half4(half3(abs(normal)), 1);
    
    // transform normal
    
    
    
    // return colorTexture.sample(textureSampler, in.texCoord) * step((half) in.lightSpacePosition.z, depth);
    
    // in.lightSpacePosition * 0.5f + 0.5f;
    // return half4(in.lightSpacePosition * 0.5f + 0.5f);
    // return shadowTexture.sample(textureSampler, in.lightSpacePosition.xy * 0.1) * 10.0;
    // return half4(in.worldSpacePosition);
    // return the color of the texture
    //return float4(colorSample) * in.color;
    // return float4(0.5, 0.5, 0.5, 0.5);
}





[[fragment]] MeshFragmentOutput
meshGbufferFragment(MeshRasterizerData in [[stage_in]],
                    constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]],
                   texture2d<half> colorTexture [[texture(AAPLTextureIndexColor) ]],
                   texture2d<half> normalTexture [[texture(AAPLTextureIndexNormal)]],
                   texture2d<half> roughnessTexture [[texture(AAPLTextureIndexRoughness)]],
                   texture2d<half> metallicTexture [[texture(AAPLTextureIndexMetallic)]])

{
    constexpr sampler bilinearSampler(mag_filter::linear,
                                      min_filter::linear,
                                      s_address::repeat,
                                      t_address::repeat);
    

    MeshFragmentOutput out;
    
    // todo: pack, compress these textures
    
    // Sample the texture to obtain a color
    half4 colorSample = colorTexture.sample(bilinearSampler, in.texCoord);
    // Sample the texture to obtain a normal
    half4 normalSample = normalTexture.sample(bilinearSampler, in.texCoord);
    // Sample the rougness texture
    half4 roughnessSample = roughnessTexture.sample(bilinearSampler, in.texCoord);
    // Sample the metallic texture
    half4 metallicSample = metallicTexture.sample(bilinearSampler, in.texCoord);
    // Sample the emssive
    half4 emissiveSample = 0;
    half4 occlusionSample = 0;
    
    // transform from tangent space to world space
    
    // orthogonalize
    half3 bitangent = normalize(half3(cross(in.tangent, in.normal)));
    half3 tangent = cross(half3(in.normal), bitangent);
    half3 normal = normalSample.xyz * 2.0h - 1.0h;

    // renormalize
    in.normal = normalize(in.normal);
    tangent = normalize(tangent);
    bitangent = normalize(bitangent);

    normal = half3x3(tangent, bitangent, half3(in.normal)) * half3(normal);
    normal = normalize(half3((uniforms.inverse_model_transform * float4(float3(normal), 1.0f)).xyz));
    
    out.color = emissiveSample;
    out.albedoMetallic.rgb = colorSample.rgb;
    out.albedoMetallic.a = metallicSample.r;
    out.normalRoughness.xyz = normal.xyz;
    out.normalRoughness.w = roughnessSample.r;
    out.depth = in.eyeSpacePosition.z;
    
    return out;
   
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
    half4 color [[color(AAPLColorIndexColor)]];
};

[[fragment]] LightingFragmentOutput
meshLightingFragment(LightingVertexOutput in [[stage_in]],
                     float4 albedoMetallic [[color(AAPLColorIndexAlbedoMetallic)]],
                     float4 normalRoughness [[color(AAPLColorIndexNormalRoughness)]],
                     float depthAsColor [[color(AAPLColorIndexDepthAsColor)]],
                     constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]],
                     texturecube<float> environmentTexture [[texture(AAPLTextureIndexEnvironment)]])
{

    constexpr sampler trilinearSampler(mag_filter::linear,
                                       min_filter::linear,
                                       mip_filter::linear,
                                       s_address::repeat,
                                       t_address::repeat);


    
    float3 albedo = albedoMetallic.rgb;
    float3 metallic = albedoMetallic.a;
    float3 normal = normalRoughness.xyz;
    float roughness = normalRoughness.w;
    float occlusion = 1.0;
    
    float3 view = -normalize(in.direction.xyz);
    float3 reflected = reflect(-view, normal);

    float lod = log2(roughness) + 4;
    float3 diffuseSample = environmentTexture.sample(trilinearSampler,
                                                     normalRoughness.xyz,
                                                     level(5)).rgb;
    float3 reflectedSample = environmentTexture.sample(trilinearSampler,
                                                       reflected,
                                                       level(lod)).rgb;
    
    float3 F0 = 0.04f;
    F0 = mix(F0, albedo, metallic);
    
    float3 Lo = 0;

    {
        // how reflective are we
        float3 F = fresnelSchlickRoughness(saturate(dot(normal, view)),
                                           F0,
                                           roughness);
        float3 kS = F;
        float3 kD = 1.0 - kS;
        float3 irradiance = diffuseSample.rgb;
        float3 diffuse    = irradiance * albedo;
        
        float3 specular = 1.0f * reflectedSample.rgb * F;
        
        float3 ambient = (kD * diffuse + specular) * occlusion;
        Lo += ambient;
        
    }
    
    
    LightingFragmentOutput out;
    out.color.rgb = half3(Lo);
    return out;
        
}


[[fragment]] LightingFragmentOutput
meshPointLightFragment(LightingVertexOutput in [[stage_in]],
                      float4 albedoMetallic [[color(AAPLColorIndexAlbedoMetallic)]],
                      float4 normalRoughness [[color(AAPLColorIndexNormalRoughness)]],
                      float depthAsColor [[color(AAPLColorIndexDepthAsColor)]],
                      constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]],
                      texture2d<float> shadowTexture [[texture(AAPLTextureIndexShadow)]])
{
    
    constexpr sampler nearestSampler(mag_filter::nearest,
                                     min_filter::nearest,
                                     s_address::clamp_to_edge,
                                     t_address::clamp_to_edge);
    
    float3 albedo = albedoMetallic.rgb;
    float3 metallic = albedoMetallic.a;
    float3 normal = normalRoughness.xyz;
    float roughness = normalRoughness.w;
    float occlusion = 1.0;
    
    float3 view = -normalize(in.direction.xyz);
    float3 halfway = normalize(view - uniforms.light_direction);
    float4 position = float4(in.direction * depthAsColor, 0) + uniforms.camera_world_position;
    float4 lightSpacePosition = uniforms.light_viewprojection_transform * position;
        
    // we only need to know distance for shadowing? (and fog i guess)
    float shadowSample = shadowTexture.sample(nearestSampler, lightSpacePosition.xy).r;
    float shadowFactor = step(lightSpacePosition.z, shadowSample);
    
    float3 F0 = 0.04f;
    F0 = mix(F0, albedo, metallic);
    
    float3 Lo = 0;
    
    // for the sun light
    {
        float cosTheta = saturate(dot(normal, halfway));
        float NdotL = saturate(dot(normal, -uniforms.light_direction));
        
        // microfacet distribution factor
        float NDF = DistributionGGX(normal, halfway, roughness);
        
        // geometry factor
        float G = GeometrySmithPoint(normal, view, -uniforms.light_direction, roughness);
        
        float3 F = fresnelSchlick(cosTheta, F0);
        
        float3 numerator = NDF * G * F;
        float denominator = 4.0f * dot(normal, view) * NdotL + 0.0001f;
        float3 specular = numerator / denominator;
        
        float3 kS = F;
        float3 kD = (1.0f - kS) * (1.0 - metallic);
        
        // float radiance = 3.0f; // of light source
        float3 radiance = float3(4, 3, 2);
        Lo += (kD * albedo * M_1_PI_F + specular) * NdotL * radiance * shadowFactor;
        
    }
    
    LightingFragmentOutput out;
    out.color.rgb = half3(Lo);
    out.color.a = 1;
    return out;
    
}








/*
 [[kernel]] void environmentMapPreFilterKernel(texturecube<half, access::read_write> inout,
 ushort3 gid [[thread_position_in_grid]]
 ) {
 constexpr sampler linearSampler(mag_filter::linear,
 min_filter::linear);
 
 ushort2 coord = gid.xy;
 ushort2 face = gid.z;
 
 
 
 
 }
 */





// filter a cube map with a scaled probe


struct cubeFilterVertexOut {
    float4 position [[position]];
    float4 normal;
    ushort face [[render_target_array_index]];
    ushort row;
};


[[vertex]] cubeFilterVertexOut
cubeFilterVertex(ushort i [[vertex_id]],
                 ushort j [[instance_id]],
                 const device float4 *v [[buffer(AAPLBufferIndexVertices)]],
                 constant cubeFilterUniforms &u [[buffer(AAPLBufferIndexUniforms)]])
{
    cubeFilterVertexOut out;
    out.position = v[i];
    out.face = j % 6;
    out.row = j / 6;
    out.normal = u.transforms[out.face] * out.position;
    return out;
}

[[fragment]] float4
cubeFilterAccumulate(cubeFilterVertexOut in [[stage_in]],
                    constant cubeFilterUniforms& uniforms [[ buffer(AAPLBufferIndexUniforms)]],
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
                v = (v - k * n) / (uniforms.alpha * uniforms.alpha) + k * n;
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
cubeFilterNormalize(cubeFilterVertexOut in [[stage_in]],
                    constant cubeFilterUniforms& uniforms [[ buffer(AAPLBufferIndexUniforms)]],
                    texturecube<float> environmentMap [[ texture(AAPLTextureIndexColor) ]])
{
    constexpr sampler nearestSampler(mag_filter::nearest, min_filter::nearest);
    float4 environmentSample = environmentMap.sample(nearestSampler, in.normal.xyz);
    return float4(environmentSample.rgb / environmentSample.a, 1);
}
