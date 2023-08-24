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
    out.clipSpacePosition = uniforms.viewprojection_transform * out.worldSpacePosition;
    out.lightSpacePosition = uniforms.light_viewprojection_transform * out.worldSpacePosition;
    out.texCoord = vertexArray[vertexID].texCoord;
    out.normal = vertexArray[vertexID].normal.xyz;
    out.tangent = vertexArray[vertexID].tangent.xyz;

    return out;
}

struct MeshFragmentOutput {
    half4 color [[color(AAPLColorIndexColor)]];
    half4 normal [[color(AAPLColorIndexNormal)]];
};

// BRDF components

// learnopengl.com/PBR

float DistributionGGX(float3 normal, float3 halfway, float roughness) {
    float alpha = roughness * roughness;
    float a2 = alpha * alpha;
    float nh = max(dot(normal, halfway), 0.0f);
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
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGXPoint(NdotV, roughness);
    float ggx2 = GeometrySchlickGGXPoint(NdotL, roughness);
    
    return ggx1 * ggx2;
}

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}


// very crude environment map
float3 environmentLookup(float3 w, float alpha) {
    float k = smoothstep(-alpha*alpha, +alpha*alpha, w.y);
    float j = smoothstep(-1, 0, w.y);
    // return float3(1.0f - blue, 0.5f, blue)*0.3f;
    //float red = (1.0f - blue) * 0.25f;
    //return float3(red, red * 0.25 + blue * 0.5f, blue);
    //return mix(float3(0.5f, 0.25f, 0.0f)*0.0f, float3(0.25f, 0.5f, 1.0f), k) * 1.0f;
    return mix(float3(0.5f, 0.25f, 0.125f)*j, float3(0.25f, 0.5f, 1.0f), k);
}

[[fragment]] MeshFragmentOutput
meshFragmentShader(MeshRasterizerData in [[stage_in]],
                   constant MeshUniforms &uniforms  [[ buffer(0) ]],
                   texture2d<float> colorTexture [[texture(AAPLTextureIndexColor) ]],
                   texture2d<float> normalTexture [[texture(AAPLTextureIndexNormal)]],
                   texture2d<float> roughnessTexture [[texture(AAPLTextureIndexRoughness)]],
                   texture2d<float> metallicTexture [[texture(AAPLTextureIndexMetallic)]],
                   texture2d<float> shadowTexture [[texture(AAPLTextureIndexShadow)]])
{
    constexpr sampler linearSampler(mag_filter::linear,
                                    min_filter::linear,
                                    s_address::repeat,
                                    t_address::repeat);

    constexpr sampler nearestSampler(mag_filter::nearest,
                                     min_filter::nearest,
                                     s_address::clamp_to_edge,
                                     t_address::clamp_to_edge);


    MeshFragmentOutput out;
    
    // Sample the texture to obtain a color
    float4 colorSample = colorTexture.sample(linearSampler, in.texCoord);
    // Sample the texture to obtain a normal
    float4 normalSample = normalTexture.sample(linearSampler, in.texCoord);
    // Sample the rougness texture
    float4 roughnessSample = roughnessTexture.sample(linearSampler, in.texCoord);
    // Sample the metallic texture
    float4 metallicSample = metallicTexture.sample(linearSampler, in.texCoord);

    in.lightSpacePosition.x = +in.lightSpacePosition.x;
    in.lightSpacePosition.y = -in.lightSpacePosition.y;
    float shadowSample = shadowTexture.sample(nearestSampler, in.lightSpacePosition.xy * 0.5f + 0.5f).r;

    // hack in white
    // colorSample = 0.5f;
    // hack in flat
    //normalSample = mix(normalSample, float4(0.5, 0.5, 1.0, 0.0), 0.0f);
    
    // float roughness = 0.1f;
    float roughness = roughnessSample.r;
    // float metalness = 1.0f;
    float metalness = metallicSample.r;
    float ao = 1.0; // ambient occlusion
    
    // Fresnel perpendicular reflectance
    float3 F0 = 0.04f;
    F0 = mix(F0, colorSample.rgb, metalness);
    
        
    
    
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
    

    float3 ambientSample = environmentLookup(normal, 1.0);
    float3 reflectedSample = environmentLookup(reflected, roughness);

    
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
        float cosTheta = max(dot(normal, halfway), 0.0);
        float NdotL = max(dot(normal, -uniforms.light_direction), 0.0);

        // microfacet distribution factor
        float NDF = DistributionGGX(normal, halfway, roughness);
        
        // geometry factor
        float G = GeometrySmithPoint(normal, view, -uniforms.light_direction, roughness);

        float3 F = fresnelSchlick(cosTheta, F0);
        
        float3 numerator = NDF * G * F;
        float denominator = 4.0f * dot(normal, view) * NdotL + 0.0001f;
        float3 specular = numerator / denominator;

        float3 kS = F;
        float3 kD = (1.0f - kS) * (1.0 - metalness);

        // float radiance = 3.0f; // of light source
        float3 radiance = float3(4, 3, 2);
        Lo += (kD * colorSample.xyz * M_1_PI_F + specular) * NdotL * radiance * shadowFactor;
        
    }
    
    // for environmental illumination
    {
        // how reflective are we
        float3 F = fresnelSchlickRoughness(max(dot(normal, view), 0.0), F0, roughness);
        float3 kS = F;
        float3 kD = 1.0 - kS;
        float3 irradiance = ambientSample;
        float3 diffuse    = irradiance * colorSample.rgb;

        float3 specular = 1.0f * reflectedSample * F;
        
        
        float3 ambient = (kD * diffuse + specular) * ao;
        Lo += ambient;
        
    }
    
    
    
    
    float3 color = Lo;
    //color = color / (color + 1.0f);
    //color = pow(color, 1.0f / 2.2f);
    
    
    // out.color = half4(shadowFactor, shadowFactor, shadowFactor, 1.0f);
    out.color = half4(half3(color), 1.0f);
    // out.color += half4(half3(environmentLookup(reflected, 0.001f)), 1.0f) * 0.5;
    out.normal = half4(half3(normal), 1.0f);
    

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


