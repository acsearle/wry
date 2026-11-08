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

namespace otf {
    
    float2 bezier_xy_for_t(float t, float2 a, float2 b, float2 c) {
        float2 ab = mix(a, b, t);
        float2 bc = mix(b, c, t);
        float2 abc = mix(ab, bc, t);
        return abc;
    }
    
    float2 bezier_xy_for_t(float t, float2 a, float2 b, float2 c, float2 d) {
        float2 ab = mix(a, b, t);
        float2 bc = mix(b, c, t);
        float2 cd = mix(c, d, t);
        float2 abc = mix(ab, bc, t);
        float2 bcd = mix(bc, cd, t);
        float2 abcd = mix(abc, bcd, t);
        return abcd;
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

    float quadratic_root_best(float a, float b, float c, float t0, float t1) {
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
    
    float2 quadratic_root_both(float a, float b, float c, float t0, float t1) {
        float d = b*b - 4.0*a*c;
        float q = -0.5 * (b + copysign(sqrt(max(d, 0.0)), b));
        return clamp(float2{q / a, c / q}, t0, t1);
    }

    
    float bezier_t_for_x(float x, float t0, float t1, float2 a, float2 b, float2 c) {
        float r = quadratic_root(a.x - 2.0*b.x + c.x,
                                 -2.0*a.x + 2.0*b.x,
                                 a.x - x,
                                 t0,
                                 t1);
        return r;
    }
    
    float2 bezier_ts_for_x(float x, float t0, float t1, float2 a, float2 b, float2 c) {
        float2 r = quadratic_root_both(a.x - 2.0*b.x + c.x,
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
    
    // newton step...?
    float cubic_root_1nms(float t0, float t1, float a, float b, float c, float d) {
        // solve a*t**3 + b*t**2 + c*t+d = 0
        float t2 = (t0 + t1) * 0.5;
        float y_t2 = ((a*t2 + b)*t2 + c)*t2 + d;
        float dydt_t2 = ((3*a*t2) + 2*b)*t2 + c;
        float q = y_t2 / dydt_t2;
        // clamp or bisect
        float t3 = clamp(t2 - y_t2 / dydt_t2,
                         q > 0.0 ? t0 : t2,
                         q > 0.0 ? t2 : t1);
        return t3;
    }
    
    
    float cbrt(float x) {
        return sign(x) * pow(abs(x), 1.0 / 3.0);
    }
    
    
    float solve_cubic(float a, float b, float c, float d) {
        // normalize
        a /= d;
        b /= d;
        c /= d;
        
        float p = (3.0*b - a*a)/3.0;
        float p3 = p/3.0;
        float q = (2.0*a*a*a - 9.0*a*b + 27.0 * c)/27.0;
        float q2 = q/2;
        float discriminant = q2*q2 + p3*p3*p3;
        float sd = sqrt(discriminant);
        float u1 = cbrt(sd - q2);
        float v1 = cbrt(sd + q2);
        float root1 = u1 - v1 - a/3.0;
        return root1;
    }
    
    float bezier_t_for_y(float y, float t0, float t1, float2 a, float2 b, float2 c, float2 d) {
//        float r = quadratic_root(a.x - 2.0*b.x + c.x,
//                                 -2.0*a.x + 2.0*b.x,
//                                 a.x - x,
//                                 t0,
//                                 t1);
//        return r;
        
        a.y -= y;
        b.y -= y;
        c.y -= y;
        d.y -= y;
        float r = solve_cubic(3.0*a.y - 6.0*b.y + 3.0*c.y,
                              -3.0*a.y + 3.0*b.y,
                              a.y,
                              -a.y+ 3.0*b.y -3.0*c.y+d.y);
        return clamp(r, t0, t1);
        
    }
    
    
    
    
    
    
    
    
    
    
    
    
   
    
    struct BezierPerVertex {
        float4 position [[position]];
        float4 coordinate [[center_no_perspective]];
    };

    struct BezierPerPrimitive {
        uint begin, end;
        float2 position;
    };

    
    using MeshOut = metal::mesh<BezierPerVertex,
        BezierPerPrimitive, 4, 2, metal::topology::triangle>;

    
    static constexpr constant uint32_t AAPLMaxTotalThreadsPerObjectThreadgroup = 1;
    static constexpr constant uint32_t AAPLMaxTotalThreadsPerMeshThreadgroup = 2;
    static constexpr constant uint32_t AAPLMaxThreadgroupsPerMeshGrid = 8;
    
    // The mesh output is shared across the threadgroup
    
    // We currently have on thread per threadgroup that produces the vertices
    // for one character's quad
    
    // We currently spread characters across the grid, but we could
    
    [[mesh,
      max_total_threads_per_threadgroup(1024)]]
    void bezierMeshFunction(MeshOut output,
                            // const object_data BezierPayload& payload [[payload]],
                            constant otf::BezierUniforms& uniforms [[buffer(3)]],
                            const device otf::GlyphData* buf_gi [[buffer(0)]],
                            const device otf::PlacedGlyph* buf_ch [[buffer(1)]],
                            const device otf::PerParagraph* buf_pp [[buffer(2)]],
                            uint lid [[thread_index_in_threadgroup]],
                            uint tid [[threadgroup_position_in_grid]]) {
        
        output.set_primitive_count(2);
        
        otf::PlacedGlyph ch = buf_ch[tid];
        otf::GlyphData gi = buf_gi[ch.glyph_index];

        {
            BezierPerPrimitive p{};
            p.begin = gi.bezier_begin;
            p.end = gi.bezier_end;
            p.position = ch.position;
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
            float2 a[4];

            // Construct the bounding box
            a[0] = float2(gi.a.x, gi.a.y);
            a[1] = float2(gi.a.x, gi.b.y);
            a[2] = float2(gi.b.x, gi.a.y);
            a[3] = float2(gi.b.x, gi.b.y);
                        
            float4 q[4];
            float2 b[4];
            for (int i = 0; i != 4; ++i) {
                // Translate and transform to screen space
                q[i] = uniforms.transformation * float4(a[i] + ch.position, 0, 1);
                // Project
                b[i] = q[i].xy / q[i].w;
            }
            
            // Construct a bounding box again, and inflate it in pixels
            // TODO: In the common case of mostly-axis-aligned text, this is
            // fine.  In the case of rotated text, it is suboptimal and can
            // greatly increase the area. The alternative, inflating the quad,
            // hits problems when the transform produces an acute corner,
            // needing some end cap strategy and more triangles.

            float2 c[4];
            c[0] = min(min(b[0], b[1]), min(b[2], b[3])) - uniforms.pixel_size * 2;
            c[3] = max(max(b[0], b[1]), max(b[2], b[3])) + uniforms.pixel_size * 2;
            c[1] = float2(c[0].x, c[3].y);
            c[2] = float2(c[3].x, c[0].y);

            // TODO: Under extreme transformations, this AABB might cross the
            // horizon of the perspective projection.

            // The corners of the box define rays we need to project back to
            // texture space
            
            // First, we have c.xyzw where we know xy, w = 1, and z is unknown
            // We undo the homogneous transformation: d.xyzw = c.xyzw * d.w
            // We apply the inverse transform: e = B * d
            // We solve for e.z = 0 and e.w = 1
            // e = B * (c.xy01 * d.w + c.0010 * d.w * d.z)
            // 0 = (B * (c.xy01 + c.0010 * d.z)).z
            //
            for (int i = 0; i != 4; ++i) {
                float4 f = uniforms.inverse_transformation * float4(c[i].xy, 0, 1);
                float4 g = uniforms.inverse_transformation * float4(0, 0, 1, 0);
                // These divisions can be singular if the points lie on the
                // horizon of a perspective projection (or the matrix itself is
                // singular)
                float h = - f.z / g.z;
                float4 p = f + g * h;
                p /= p.w;
                // Now p.z = 0 and p.w = 1
                
                BezierPerVertex v;
                v.position = uniforms.transformation * p;
                // v.position = q[i];
                // v.position = p;
                // v.coordinate = float4(v.position.xy - ch.position, 0, 1);
                // screen space coordinate
                v.coordinate = v.position / v.position.w;
                output.set_vertex(i, v);
            }
            
            
            
            
            
            
            
            
            
            
            
            
            
            

            
            
//                
//            BezierPerVertex v;
//            
//
//            // We need to inflate the quads to allow the antialiasing to spread
//            // out a few extra pixels.  Ideally this would be done in, or with
//            // knowledge of, screen space.
//            float d = 1.0 / 32.0;
//            
//            v.coordinate = float4(gi.a.x - d, gi.a.y - d, 1.0, 1.0);
//            v.position = v.coordinate + float4(ch.position.xy, 0.0, 0.0);
//            v.position.w += (v.position.x + v.position.y) / 4.0;
//            v.position.xyz /= float3(19.2,10.8,10.8) * 0.5;
//            output.set_vertex(0, v);
//            v.coordinate = float4(gi.a.x - d, gi.b.y + d, 1.0, 1.0);
//            v.position = v.coordinate + float4(ch.position.xy, 0.0, 0.0);
//            v.position.w += (v.position.x + v.position.y) / 4.0;
//            v.position.xyz /= float3(19.2,10.8,10.8) * 0.5;
//            output.set_vertex(1, v);
//            v.coordinate = float4(gi.b.x + d, gi.a.y - d, 1.0, 1.0);
//            v.position = v.coordinate + float4(ch.position.xy, 0.0, 0.0);
//            v.position.w += (v.position.x + v.position.y) / 4.0;
//            v.position.xyz /= float3(19.2,10.8,10.8) * 0.5;
//            output.set_vertex(2, v);
//            v.coordinate = float4(gi.b.x + d, gi.b.y + d, 1.0, 1.0);
//            v.position = v.coordinate + float4(ch.position.xy, 0.0, 0.0);
//            v.position.w += (v.position.x + v.position.y) / 4.0;
//            v.position.xyz /= float3(19.2,10.8,10.8) * 0.5;
//            output.set_vertex(3, v);
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
    
    
    
//    struct QuadraticBezier {
//        float2 a;
//        float2 b;
//        float2 c;
//        float2 _padding;
//    };
    
    
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
        // tanh(2.0/sqrt(pi) * x)
        return tanh(1.202760580 * x);
    }

    float2 erf_tanh_approx2(float2 x) {
        // tanh(2.0/sqrt(pi) * x + 11/123 x*x*x)
        return tanh(1.202760580 * x + 0.0894308943*x*x*x);
    }

    float2 erf6(const float2 x)
    {
        // Abramowitz and Stegun #2
        const float2 one = float2(1.0);
        const float2 sign_x = sign(x);
        const float2 t = one/(one + 0.47047*abs(x));
        const float2 result = one - t*(0.3480242 + t*(-0.0958798 + t*0.7478556))*
        exp(-(x*x));
        return result * sign_x;
    }
    
    float2 erf_shadertoy(float2 x) {
        // destroys precision around 0.0?
        // Reduced Sergei Winitzki global Padé approximant?
        return sign(x) * sqrt(1.0 - exp2(-1.787776 * x * x)); // likely faster version by @spalmer
    }
        
    float2 erfc_via_smoothstep(float2 x) {
        return 2.0*smoothstep(x, -1.5, +1.5);
    }

    float2 erfc_approx(float2 x) {
        return 1.0 - erf6(x);
    }
    
    
    float2x2 inverse(float2x2 m) {
        float determinant = m[0][0] * m[1][1] - m[0][1] * m[1][0];
        return float2x2{
            {m[1][1], -m[0][1]},
            {-m[1][0],  m[0][0]}
        } / determinant;
    }
        
    [[fragment]] BezierFragmentOut
    bezierFragmentFunction(BezierFragmentIn input [[stage_in]],
                           const device otf::CubicBezier* cubic_bezier_curves [[buffer(0)]],
                           constant otf::BezierUniforms& uniforms [[buffer(3)]]) {
        
        // Compute the fill of a closed compound cubic Bezier path, antialiased
        // with a Gaussian kernel.  Exact if the path is not self-intersecting;
        // self-intersections will result in regions with multiple coverage.
        
        // The path is looked up from a table and translated, but not otherwise
        // transformed.
        // TODO: This structure is suitable for text layout, but not for more
        // general rendering.  Supply a bespoke color and transform to each
        // path.
                
        // For the pixel center,
        //   input.v.position provides viewport coordinates,
        // [0, width] x [0, height]
        //   input.v.coodinate provides normalized device coordinates of the
        // pixel center, [-1, 1] x [-1, 1] x [0, 1] x [1]
        // TODO:
        float2 coordinate = input.v.coordinate.xy;
        
        // page to screen transformation
        float4x4 A = uniforms.transformation;
                
        // stack of Bezier curves to be processed
        const int STACK_SIZE = 64;
        float4 stack[STACK_SIZE][4];
        int index = 0;
        // Font glyphs typically have tens of curves.
        // TODO: Larger glyphs, or convergence problems, will run off the end of
        // the buffer and crash the shader; what do we want to do here?
        
        for (uint j = input.p.begin; j != input.p.end; ++j) {

            // Transform from page to screen
            float4 a = A * float4(cubic_bezier_curves[j].a + input.p.position, 0, 1);
            float4 b = A * float4(cubic_bezier_curves[j].b + input.p.position, 0, 1);
            float4 c = A * float4(cubic_bezier_curves[j].c + input.p.position, 0, 1);
            float4 d = A * float4(cubic_bezier_curves[j].d + input.p.position, 0, 1);
            // Translate from screen to pixel
            a.xy -= coordinate * a.w;
            b.xy -= coordinate * b.w;
            c.xy -= coordinate * c.w;
            d.xy -= coordinate * d.w;
            // Scale according to pixel size
            a.xy /= uniforms.pixel_size * M_SQRT2_F;
            b.xy /= uniforms.pixel_size * M_SQRT2_F;
            c.xy /= uniforms.pixel_size * M_SQRT2_F;
            d.xy /= uniforms.pixel_size * M_SQRT2_F;
            // Store in the buffer
            stack[index][0] = a;
            stack[index][1] = b;
            stack[index][2] = c;
            stack[index][3] = d;
            ++index;
        }
        // Buffer now contains Bezier control points in a coordinate system
        // that is not yet divided through by w, but is otherwise centered on
        // and scaled to the pixel we are rasterizing

        // TODO: With the exception of the pixel-dependent translation, the
        // transformation above is common to all fragments of the primitive and
        // could be somehow computed in the mesh shader
                         
        float cumulant = 0.0;
        while (index) {
            --index;
            float4 a = stack[index][0];
            float4 b = stack[index][1];
            float4 c = stack[index][2];
            float4 d = stack[index][3];
            // Bounding volume of control points.
            float4 lo = min(min(a, b), min(c, d));
            float4 hi = max(max(a, b), max(c, d));
            // Divide the coordinates into nonhomogeneous 2d, and then map
            // through erf() to the integral's preferred coordinates.
            // TODO: This error estimate may be too small for extreme w
            float2 plo = erf6(lo.xy / lo.w);
            float2 phi = erf6(hi.xy / lo.w);
            // Error is proportional to the size of this region
            float error = (phi.x - plo.x) * (phi.y - plo.y);
            // TODO: We seem insensitive to the error threshold (which is good)
            // but we could budget it as we subdivide to get formal convergence
            // guarantees
            if (error <= 1.0 / 64.0) {
                float2 pa = erf6(a.xy / a.w);
                float2 pd = erf6(d.xy / d.w);
                // Use trapezoid rule
                cumulant += (pd.y - pa.y) * (pa.x + pd.x);
            } else {
                // Subdivide until we can use the trapezoid rule
                // Use de Casteljau's agorithm to generate the control points
                // of the left and right halves of the curve
                float4 ab = mix(a, b, 0.5);
                float4 bc = mix(b, c, 0.5);
                float4 cd = mix(c, d, 0.5);
                float4 abbc = mix(ab, bc, 0.5);
                float4 bccd = mix(bc, cd, 0.5);
                float4 abbbcccd = mix(abbc, bccd, 0.5);
                stack[index][0] = a;
                stack[index][1] = ab;
                stack[index][2] = abbc;
                stack[index][3] = abbbcccd;
                ++index;
                stack[index][0] = abbbcccd;
                stack[index][1] = bccd;
                stack[index][2] = cd;
                stack[index][3] = d;
                ++index;
            }
        }
         
        // Factor of 0.125 is because erfc produces a 2 by 2 square region,
        // and `pa.x + pb.x` is twice the mean.
        cumulant = clamp(cumulant * 0.125, -1.0, 1.0);
    
        BezierFragmentOut result{};

        result.color.r = 0 * cumulant;
        result.color.g = 0 * cumulant;
        result.color.b = 0 * cumulant;
        result.color.a = 1 * cumulant;
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

// Clear coat
//
// A thin smooth dielectric layer over the base BRDF (glTF
// KHR_materials_clearcoat, Filament's clear-coat model): a second GGX
// specular lobe with F0 = 0.04 and its own roughness, sharing the base's
// normal.  Its Fresnel weight, scaled by the strength s, is reflected;
// the rest transmits to the base and back, so the base's whole response
// is attenuated by it (Filament's single (1 - Fc) for both legs):
//
//     direct:  f = f_base * (1 - s Fc(VdotH)) + s D_c Fc / (4 LdotH^2)  (Kelemen)
//     ambient: L = L_base * (1 - s Ec) + s Ec prefiltered(R, r_c),
//              Ec = 0.04 A(NdotV, r_c) + B(NdotV, r_c)   (the split-sum LUT)
//
// s is the G-buffer's material.a; the coat's roughness r_c is one
// constant (a varnish is smooth; the G-buffer's normal.w is free if a
// material ever wants its own).  It is 1/8, the prefiltered sky's level
// 1 and the sharpest level the coat reads: level 0 (1/16) currently
// samples as non-finite (black), which wants investigating first.  It is 1/8, the prefiltered sky's level
// 1, the sharpest the coat reads: level 0 (1/16) currently samples as
// non-finite (black) and wants investigating before anything smoother.  s = 0 skips the lobe: an uncoated
// surface shades by the same formula as before (the branch's presence
// moves fast-math codegen by at most an 8-bit step at a few pixels).

constant float kClearCoatF0 = 0.04f;
constant float kClearCoatRoughness = 0.125f;

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
        // MeshInstanced::albedo: a linear tint multiplying the albedo
        // texture (white for an untinted instance)
        half4 tint [[flat]];
    };
    
    // This is the g-buffer
    //
    // Note use of raster order groups
    //
    // Metal does not allow reading the depth attachment within the pass, so
    // the G-buffer carries its own: linear view depth (clip w), from which
    // every reader recovers the surface as camera + ray * depth with the
    // MeshUniforms ray basis.  Cleared to 0, which is the sky.
    
    // TODO: is g-buffer storage 'free' until we free up the imageblock?
    //       perhaps we should just write out the world position

    struct FragmentFunctionOutput {

        // emission is written here in first pass
        // lighting is accumulated here by susbequent passes
        half4 light              [[color(AAPLColorIndexColor),
                                   raster_order_group(AAPLRasterOrderGroupLighting)]];

        // these surface properties are immutable on later passes
        half4 albedo              [[color(AAPLColorIndexAlbedo),
                                    raster_order_group(AAPLRasterOrderGroupGBuffer)]];

        half4 normal              [[color(AAPLColorIndexNormal),
                                    raster_order_group(AAPLRasterOrderGroupGBuffer)]];

        // scalar material fields, packed one per channel following the
        // glTF occlusion-roughness-metallic channel convention:
        // r = ambient occlusion, g = roughness, b = metallic, and
        // a = clear-coat strength (0 for none; see kClearCoatRoughness)
        half4 material            [[color(AAPLColorIndexMaterial),
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
        out.tint = half4(instance.albedo);
        
        float4 position_world = instance.model_transform * in.position;
        out.tangent_world   = instance.inverse_transpose_model_transform * in.tangent;
        out.bitangent_world = instance.inverse_transpose_model_transform * in.bitangent;
        out.normal_world    = instance.inverse_transpose_model_transform * in.normal;
        
        out.position_clip   = uniforms.viewprojection_transform * position_world;
        
        return out;
    }
    
    // TODO: Three.js shaders suggest there is a conventional structure for
    //       texture packing like OcclusionRoughnessMetallic <=> RGB

    // The body of the mesh fragment functions below: the textures'
    // sampler is theirs to choose
    static FragmentFunctionOutput
    fragment_body(VertexFunctionOutput in,
                  bool front_facing,
                  constant MeshUniforms& uniforms,
                  texture2d<half> emissiveTexture,
                  texture2d<half> albedoTexture,
                  texture2d<half> metallicTexture,
                  texture2d<half> normalTexture,
                  texture2d<half> roughnessTexture,
                  texture2d<half> occlusionTexture,
                  sampler textureSampler)
    {
        
        FragmentFunctionOutput out;
        
        half4 albedoSample = albedoTexture.sample(textureSampler, in.coordinate.xy);
        
        // the g-buffer can't support transparency so we just discard on a
        // threshold
        if (albedoSample.a < 0.5f)
            discard_fragment();
        
        half4 normalSample = normalTexture.sample(textureSampler, in.coordinate.xy);
        half4 roughnessSample = roughnessTexture.sample(textureSampler, in.coordinate.xy);
        half4 metallicSample = metallicTexture.sample(textureSampler, in.coordinate.xy);
        half4 emissiveSample = emissiveTexture.sample(textureSampler, in.coordinate.xy);
        half4 occlusionSample = occlusionTexture.sample(textureSampler, in.coordinate.xy);

        normalSample = normalSample * 2.0h - 1.0h;

        half3 normal = normalize(half3x3(half3(in.tangent_world.xyz),
                                         half3(in.bitangent_world.xyz),
                                         half3(in.normal_world.xyz)) * normalSample.xyz);

        out.light = front_facing ? emissiveSample : half4(1.0h, 0.0h, 1.0h, 0.0h);
        out.albedo.rgb = front_facing ? albedoSample.rgb * in.tint.rgb : 0.0h;
        out.albedo.a = 1.0h;
        out.normal.xyz = front_facing ? normal : 0.0h;
        out.normal.w = 0.0h;
        out.material.r = front_facing ? occlusionSample.r : 1.0h;
        out.material.g = front_facing ? roughnessSample.g : 1.0h;
        out.material.b = front_facing ? metallicSample.b : 0.0h;
        out.material.a = 0.0h;
        
        // Linear view depth; the rasterized position's w is 1 / clip w
        out.depth = 1.0f / in.position_clip.w;
        
        return out;
        
    }

    [[fragment]] FragmentFunctionOutput
    fragment_function(VertexFunctionOutput in [[stage_in]],
                               bool front_facing [[front_facing]], //:todo: for debugging
                               constant MeshUniforms& uniforms  [[ buffer(AAPLBufferIndexUniforms) ]],
                               texture2d<half> emissiveTexture [[texture(AAPLTextureIndexEmissive) ]],
                               texture2d<half> albedoTexture [[texture(AAPLTextureIndexAlbedo) ]],
                               texture2d<half> metallicTexture [[texture(AAPLTextureIndexMetallic)]],
                               texture2d<half> normalTexture [[texture(AAPLTextureIndexNormal)]],
                               texture2d<half> roughnessTexture [[texture(AAPLTextureIndexRoughness)]],
                               texture2d<half> occlusionTexture [[texture(AAPLTextureIndexOcclusion)]])
    {
        constexpr sampler trilinearSampler(mag_filter::linear,
                                           min_filter::linear,
                                           mip_filter::linear,
                                           s_address::repeat,
                                           t_address::repeat);
        return fragment_body(in, front_facing, uniforms, emissiveTexture, albedoTexture,
                             metallicTexture, normalTexture, roughnessTexture,
                             occlusionTexture, trilinearSampler);
    }

    // The world-map quad: one texel per tile, so magnification shows the
    // tiles as the flat cells they are rather than a blur between them
    [[fragment]] FragmentFunctionOutput
    map_fragment_function(VertexFunctionOutput in [[stage_in]],
                               bool front_facing [[front_facing]],
                               constant MeshUniforms& uniforms  [[ buffer(AAPLBufferIndexUniforms) ]],
                               texture2d<half> emissiveTexture [[texture(AAPLTextureIndexEmissive) ]],
                               texture2d<half> albedoTexture [[texture(AAPLTextureIndexAlbedo) ]],
                               texture2d<half> metallicTexture [[texture(AAPLTextureIndexMetallic)]],
                               texture2d<half> normalTexture [[texture(AAPLTextureIndexNormal)]],
                               texture2d<half> roughnessTexture [[texture(AAPLTextureIndexRoughness)]],
                               texture2d<half> occlusionTexture [[texture(AAPLTextureIndexOcclusion)]])
    {
        constexpr sampler mapSampler(mag_filter::nearest,
                                     min_filter::linear,
                                     mip_filter::linear,
                                     s_address::repeat,
                                     t_address::repeat);
        return fragment_body(in, front_facing, uniforms, emissiveTexture, albedoTexture,
                             metallicTexture, normalTexture, roughnessTexture,
                             occlusionTexture, mapSampler);
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
    
    
    
    
    
    
    
    
    
    // ---- Thickened symbols.
    //
    // The field is render/symbol_sdf.hpp's: per cell, the planar signed
    // distance d2 to the symbol's silhouette and its unit gradient, in
    // source texels.  The slab (WrySymbolInstance) is that silhouette
    // extruded over |z| <= h with its top and bottom edges filleted by
    // circular arcs of radii rt and rb:
    //
    //     top    = extrude(d2 + rt, h - rt) rounded by rt, for z >= 0
    //     bottom = the same with rb, z mirrored
    //     slab   = top intersect bottom  =>  max(top, bottom)
    //
    // Each half's distance is exact for the slab on its own side of z = 0
    // and a lower bound on the other (the half-slab contains the slab), so
    // the max is safe to march.  The normal is the analytic gradient: the
    // field's gradient on the wall, +-z on the faces, and on the fillet
    // their blend by the position around the quarter circle -- no finite
    // differences of the sampled field, so bilinear facets never reach the
    // shading.
    //
    // The vertex function expands the instance to a cuboid; the fragment
    // function clips the ray to it and marches.  Distances are in texels
    // throughout; the field's clamp to +-WRY_SYMBOL_SDF_RANGE is a lower
    // bound, which is all a march needs, and the bilinear reconstruction
    // can steepen to sqrt(2), so steps are scaled by 0.7.

    constant float3 symbol_cube_corner[8] = {
        float3(-1.0f, -1.0f, -1.0f), float3( 1.0f, -1.0f, -1.0f),
        float3(-1.0f,  1.0f, -1.0f), float3( 1.0f,  1.0f, -1.0f),
        float3(-1.0f, -1.0f,  1.0f), float3( 1.0f, -1.0f,  1.0f),
        float3(-1.0f,  1.0f,  1.0f), float3( 1.0f,  1.0f,  1.0f),
    };

    // Outward counter-clockwise, as the OBJ meshes are wound
    constant ushort symbol_cube_index[36] = {
        4, 5, 7,  4, 7, 6,   // +z
        0, 2, 3,  0, 3, 1,   // -z
        1, 3, 7,  1, 7, 5,   // +x
        0, 4, 6,  0, 6, 2,   // -x
        2, 6, 7,  2, 7, 3,   // +y
        0, 1, 5,  0, 5, 4,   // -y
    };

    struct SymbolVertexFunctionOutput {
        float4 position_clip [[position]];
        float3 position_slab;
        uint instance_id [[flat]];
    };

    [[vertex]] SymbolVertexFunctionOutput
    symbol_vertex_function(uint vertex_id [[vertex_id]],
                           uint instance_id [[instance_id]],
                           constant MeshUniforms& uniforms [[buffer(AAPLBufferIndexUniforms)]],
                           const device WrySymbolInstance* instances [[buffer(AAPLBufferIndexInstanced)]])
    {
        SymbolVertexFunctionOutput out;
        WrySymbolInstance instance = instances[instance_id];
        out.position_slab = symbol_cube_corner[symbol_cube_index[vertex_id]] * instance.half_extent.xyz;
        float4 position_world = instance.model_transform * float4(out.position_slab, 1.0f);
        out.position_clip = uniforms.viewprojection_transform * position_world;
        out.instance_id = instance_id;
        return out;
    }

    [[vertex]] SymbolVertexFunctionOutput
    symbol_shadow_vertex_function(uint vertex_id [[vertex_id]],
                                  uint instance_id [[instance_id]],
                                  constant MeshUniforms& uniforms [[buffer(AAPLBufferIndexUniforms)]],
                                  const device WrySymbolInstance* instances [[buffer(AAPLBufferIndexInstanced)]])
    {
        SymbolVertexFunctionOutput out;
        WrySymbolInstance instance = instances[instance_id];
        out.position_slab = symbol_cube_corner[symbol_cube_index[vertex_id]] * instance.half_extent.xyz;
        float4 position_world = instance.model_transform * float4(out.position_slab, 1.0f);
        out.position_clip = uniforms.light_viewprojection_transform * position_world;
        out.instance_id = instance_id;
        return out;
    }

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
    static bool symbol_march(texture2d<float> field, WrySymbolInstance instance,
                             float3 origin, float3 direction, thread SymbolHit& hit)
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
        for (int i = 0; i != 160 && d >= 0.02f; ++i) {
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

    struct SymbolFragmentFunctionOutput {
        half4 light    [[color(AAPLColorIndexColor),    raster_order_group(AAPLRasterOrderGroupLighting)]];
        half4 albedo   [[color(AAPLColorIndexAlbedo),   raster_order_group(AAPLRasterOrderGroupGBuffer)]];
        half4 normal   [[color(AAPLColorIndexNormal),   raster_order_group(AAPLRasterOrderGroupGBuffer)]];
        half4 material [[color(AAPLColorIndexMaterial), raster_order_group(AAPLRasterOrderGroupGBuffer)]];
        float depth    [[color(AAPLColorIndexDepth),    raster_order_group(AAPLRasterOrderGroupGBuffer)]];
        // The hit's depth, for the depth test against the meshes; the
        // cuboid's own rasterized depth is meaningless
        float depth_test [[depth(any)]];
    };

    [[fragment]] SymbolFragmentFunctionOutput
    symbol_fragment_function(SymbolVertexFunctionOutput in [[stage_in]],
                             constant MeshUniforms& uniforms [[buffer(AAPLBufferIndexUniforms)]],
                             const device WrySymbolInstance* instances [[buffer(AAPLBufferIndexInstanced)]],
                             texture2d<float> field [[texture(AAPLTextureIndexNormal)]],
                             texture2d<half> albedoTexture [[texture(AAPLTextureIndexAlbedo)]])
    {
        constexpr sampler trilinearSampler(mag_filter::linear,
                                           min_filter::linear,
                                           mip_filter::linear,
                                           s_address::clamp_to_edge,
                                           t_address::clamp_to_edge);

        WrySymbolInstance instance = instances[in.instance_id];
        // The eye in slab space; the rasterized cuboid point fixes the ray
        float4 eye = instance.inverse_model_transform * uniforms.camera_position_world;
        float3 origin = eye.xyz / eye.w;
        float3 direction = normalize(in.position_slab - origin);
        SymbolHit hit;
        if (!symbol_march(field, instance, origin, direction, hit))
            discard_fragment();

        float4 position_world = instance.model_transform * float4(hit.position, 1.0f);
        float4 position_clip = uniforms.viewprojection_transform * position_world;
        float3 normal_world = normalize((instance.model_transform * float4(hit.normal, 0.0f)).xyz);

        // Texels per pixel across the cuboid face, for the atlas mip level
        // of a face's print.  A wall reads the base level: its margin
        // colour is uniform, and the cuboid's footprint at a grazing wall
        // would pick a mip that averages the print into it
        float2 footprint = fwidth(in.position_slab.xy);
        float lod = hit.print ? max(log2(max(footprint.x, footprint.y)), 0.0f) : 0.0f;
        // The atlas is straight alpha with its margins coloured, so the
        // colour is read as is wherever the march sends the uv (see
        // symbol_march); the surface is opaque
        half3 albedo = albedoTexture.sample(trilinearSampler, hit.uv, level(lod)).rgb;

        SymbolFragmentFunctionOutput out;
        out.light = half4(0.0h, 0.0h, 0.0h, 0.0h);
        out.albedo = half4(albedo, 1.0h);
        out.normal = half4(half3(normal_world), 0.0h);
        out.material = half4(1.0h, 0.5h, 0.0h, half(instance.clear_coat));   // occlusion, roughness, metallic, clear coat
        out.depth = position_clip.w;
        out.depth_test = position_clip.z / position_clip.w;
        return out;
    }

    struct SymbolShadowFragmentFunctionOutput {
        float depth [[depth(any)]];
    };

    [[fragment]] SymbolShadowFragmentFunctionOutput
    symbol_shadow_fragment_function(SymbolVertexFunctionOutput in [[stage_in]],
                                    constant MeshUniforms& uniforms [[buffer(AAPLBufferIndexUniforms)]],
                                    const device WrySymbolInstance* instances [[buffer(AAPLBufferIndexInstanced)]],
                                    texture2d<float> field [[texture(AAPLTextureIndexNormal)]])
    {
        WrySymbolInstance instance = instances[in.instance_id];
        // The directional light's rays run along -light_direction; start
        // from well outside the cuboid, whichever of its faces this is
        float3 direction = normalize((instance.inverse_model_transform * float4(-uniforms.light_direction, 0.0f)).xyz);
        float3 half_extent = instance.half_extent.xyz;
        float3 origin = in.position_slab - direction * (4.0f * (half_extent.x + half_extent.y + half_extent.z));
        SymbolHit hit;
        if (!symbol_march(field, instance, origin, direction, hit))
            discard_fragment();
        // The encoder's depth bias applies to rasterized depth, not to
        // depth written here, so bias the caster ourselves against
        // self-shadowing, as the quads' slope-scaled bias did: push the
        // hit down the light's ray (away from the light; never along the
        // normal, which would raise the caster above its own lit faces),
        // a texel plus a texel per unit of slope, so grazing walls, whose
        // depth sweeps through a shadow-map texel, get the most
        float slope = 1.0f / max(dot(hit.normal, -direction), 0.05f);
        float3 biased = hit.position + direction * (1.0f + slope);
        float4 position_world = instance.model_transform * float4(biased, 1.0f);
        float4 position_clip = uniforms.light_viewprojection_transform * position_world;
        SymbolShadowFragmentFunctionOutput out;
        out.depth = position_clip.z / position_clip.w;
        return out;
    }

    // ---- Symbol decals: the slab's ambient occlusion on what lies under it.
    //
    // Treating the nearest silhouette edge as a straight stroke of the
    // cell's width w, thickness t and fillets r, extruded without end,
    // the fraction of the cosine-weighted sky it hides from a point h
    // below its underside and d outside its edge is the two-dimensional
    // configuration factor
    //
    //     blocked = (sin(phi2) - sin(phi1)) / 2
    //
    // phi1, phi2 the angles from the vertical of the tangent rays to the
    // section.  The section is the convex hull of its corner discs, so
    // each tangent touches a disc: the far bottom disc gives phi1
    // everywhere on the ground, the near bottom disc gives phi2 under the
    // stroke and the near top disc outside it (they swap directly under
    // the wall, which is the one crease in the result), and a tangent's
    // sine is algebraic in the disc's offset (cx, cz) and radius r:
    //
    //     sin(phi) = (cx * sqrt(cx^2 + cz^2 - r^2) -+ cz * r) / (cx^2 + cz^2)
    //
    // Exact along a stroke; at its ends, corners and junctions it is off
    // by the order of the far-edge term.  The tail is (h + t/2) t / (2 d^2),
    // so the instance's reach is where that drops below a threshold, and
    // the last stroke width before the reach fades out.  Only the ambient
    // light reads the occlusion channel it writes
    // (image_based_lighting_fragment_function), so direct light and the
    // shadow map are untouched.
    //
    // The decal is a box under the slab, from its underside to the ground,
    // its footprint the cell plus the reach; each fragment reads the
    // G-buffer's depth, finds the surface point in slab space, and
    // multiplies the occlusion channel in place under the G-buffer raster
    // order group.  No depth test: the box's own depth means nothing.

    // The camera ray through an NDC point, with unit component along the
    // view axis (ShaderTypes.h, MeshUniforms): camera + ray * depth is the
    // surface at linear view depth
    static float3 camera_ray(constant MeshUniforms& uniforms, float2 ndc) {
        return uniforms.camera_ray_centre.xyz
             + ndc.x * uniforms.camera_ray_dx.xyz
             + ndc.y * uniforms.camera_ray_dy.xyz;
    }

    struct SymbolDecalVertexFunctionOutput {
        float4 position_clip [[position]];
        // The camera ray through the pixel (MeshUniforms ray basis), affine
        // in the pixel, so interpolated without perspective
        float3 ray [[center_no_perspective]];
        uint instance_id [[flat]];
    };

    // The ground plane in slab space: the point under the slab's origin
    static float symbol_ground_z(WrySymbolInstance instance) {
        float4 origin = instance.model_transform.columns[3];
        float4 ground = instance.inverse_model_transform * float4(origin.xy, 0.0f, origin.w);
        return ground.z / ground.w;
    }

    [[vertex]] SymbolDecalVertexFunctionOutput
    symbol_decal_vertex_function(uint vertex_id [[vertex_id]],
                                 uint instance_id [[instance_id]],
                                 constant MeshUniforms& uniforms [[buffer(AAPLBufferIndexUniforms)]],
                                 const device WrySymbolInstance* instances [[buffer(AAPLBufferIndexInstanced)]])
    {
        SymbolDecalVertexFunctionOutput out;
        WrySymbolInstance instance = instances[instance_id];
        float3 corner = symbol_cube_corner[symbol_cube_index[vertex_id]];
        float z_under = -instance.half_extent.z;
        float3 p;
        p.xy = corner.xy * (instance.half_extent.xy + instance.decal_reach);
        p.z = corner.z > 0.0f ? z_under : min(symbol_ground_z(instance), z_under) - 1.0f;
        float4 position_world = instance.model_transform * float4(p, 1.0f);
        out.position_clip = uniforms.viewprojection_transform * position_world;
        float2 ndc = out.position_clip.xy / out.position_clip.w;
        out.ray = camera_ray(uniforms, ndc);
        out.instance_id = instance_id;
        return out;
    }

    // The sines of the lower (x) and upper (y) tangent angles from the
    // origin to a disc of radius r at (cx, cz), angles from the vertical
    static float2 symbol_tangent_sines(float cx, float cz, float r) {
        float rho2 = cx * cx + cz * cz;
        float s = sqrt(max(rho2 - r * r, 0.0f));
        return float2(cx * s - cz * r, cx * s + cz * r) / rho2;
    }

    // The blocked fraction of the sky at a point h below the underside of
    // a stroke occupying x in [-w, 0], thickness t, bottom and top fillets
    // rb and rt, the point d outside its edge (d negative under it)
    static float symbol_stroke_occlusion(float d, float h, float w, float t, float rb, float rt) {
        float2 near_bottom = symbol_tangent_sines(-rb - d, h + rb, rb);
        float2 near_top = symbol_tangent_sines(-rt - d, h + t - rt, rt);
        float2 far_bottom = symbol_tangent_sines(-w + rb - d, h + rb, rb);
        return 0.5f * (max(near_bottom.y, near_top.y) - far_bottom.x);
    }

    struct SymbolDecalFragmentFunctionOutput {
        half4 material [[color(AAPLColorIndexMaterial), raster_order_group(AAPLRasterOrderGroupGBuffer)]];
    };

    [[fragment]] SymbolDecalFragmentFunctionOutput
    symbol_decal_fragment_function(SymbolDecalVertexFunctionOutput in [[stage_in]],
                                   FragmentFunctionOutput gbuffer,
                                   constant MeshUniforms& uniforms [[buffer(AAPLBufferIndexUniforms)]],
                                   const device WrySymbolInstance* instances [[buffer(AAPLBufferIndexInstanced)]],
                                   texture2d<float> field [[texture(AAPLTextureIndexNormal)]])
    {
        constexpr sampler bilinear(mag_filter::linear,
                                   min_filter::linear,
                                   s_address::clamp_to_edge,
                                   t_address::clamp_to_edge);

        WrySymbolInstance instance = instances[in.instance_id];
        // The surface under this pixel, from the G-buffer's depth, in slab space
        float4 position_world = float4(uniforms.camera_position_world.xyz + in.ray * gbuffer.depth, 1.0f);
        float4 q = instance.inverse_model_transform * position_world;
        float3 p = q.xyz / q.w;
        float3 half_extent = instance.half_extent.xyz;
        float cell_half = instance.half_extent.w;
        float reach = instance.decal_reach;
        float2 outside = max(abs(p.xy) - cell_half, 0.0f);
        float h = -half_extent.z - p.z;      // below the underside, texels
        if (h <= 0.0f || p.z < symbol_ground_z(instance) - 1.0f || any(outside > reach))
            discard_fragment();

        // The planar distance to the silhouette: the fine channel within
        // its range, the coarse one beyond, plus the way to the cell from
        // outside it, where the sampler clamps (exact for an edge along
        // the cell's border, an overestimate otherwise, which only fades
        // the decal sooner)
        float4 s = field.sample(bilinear, symbol_uv(instance, clamp(p.xy, -cell_half, cell_half)));
        float fine = (s.r - 0.5f) * (2.0f * WRY_SYMBOL_SDF_RANGE);
        float coarse = (s.a - 0.5f) * (2.0f * WRY_SYMBOL_SDF_FAR_RANGE);
        float d = (abs(fine) < WRY_SYMBOL_SDF_RANGE - 0.5f ? fine : coarse) + length(outside);

        float w = instance.stroke_width;
        float t = 2.0f * half_extent.z;
        float blocked = symbol_stroke_occlusion(d, max(h, 0.25f), w, t,
                                                instance.bottom_radius, instance.top_radius);
        float window = 1.0f - smoothstep(reach - w, reach, d);
        float occlusion = float(gbuffer.material.r) * (1.0f - saturate(blocked) * window);

        SymbolDecalFragmentFunctionOutput out;
        out.material = half4(half(occlusion), gbuffer.material.gba);
        return out;
    }

    // ---- Decals: images projected onto the G-buffer (WryDecalInstance).
    //
    // The same scheme as the occlusion decal above: a box rasterized
    // after the frame's geometry, no depth test, each fragment finding
    // the surface under it from the G-buffer's depth and editing the
    // G-buffer in place under its raster order groups.  This one paints:
    // the stored albedo moves toward the image's colour by its coverage,
    // the material's roughness and metallic toward the instance's by the
    // coverage times their weights, the emitted light (the light
    // attachment, before any light accumulates) toward the instance's
    // emissive by the coverage times its weight, and the occlusion down
    // by the coverage times the instance's.  Surfaces facing away from
    // the decal's +z by more than the cutoff are left alone, so a print
    // on a tile's face stops at the tile's walls.
    //
    // Embossing treats the coverage as a height field h = emboss * c
    // over the surface and tilts the stored normal n by its slope,
    // n' = normalize(n - grad h), the gradient taken across the surface
    // in the frame's x and y (Gram-Schmidt against n, so it follows a
    // surface the box meets at an angle).  The slope is a central
    // difference of the coverage over 2r of uv, r the instance's radius
    // or the pixel's footprint if that is larger, sampled at the mip
    // level of r: the bevel is about r wide, and fades out (the slope
    // shrinks with the widening step) rather than aliasing far away.

    struct DecalVertexFunctionOutput {
        float4 position_clip [[position]];
        float3 ray [[center_no_perspective]];
        uint instance_id [[flat]];
    };

    [[vertex]] DecalVertexFunctionOutput
    decal_vertex_function(uint vertex_id [[vertex_id]],
                          uint instance_id [[instance_id]],
                          constant MeshUniforms& uniforms [[buffer(AAPLBufferIndexUniforms)]],
                          const device WryDecalInstance* instances [[buffer(AAPLBufferIndexInstanced)]])
    {
        DecalVertexFunctionOutput out;
        WryDecalInstance instance = instances[instance_id];
        float3 corner = symbol_cube_corner[symbol_cube_index[vertex_id]] * 0.5f;
        float4 position_world = instance.model_transform * float4(corner, 1.0f);
        out.position_clip = uniforms.viewprojection_transform * position_world;
        float2 ndc = out.position_clip.xy / out.position_clip.w;
        out.ray = camera_ray(uniforms, ndc);
        out.instance_id = instance_id;
        return out;
    }

    struct DecalFragmentFunctionOutput {
        half4 light    [[color(AAPLColorIndexColor),    raster_order_group(AAPLRasterOrderGroupLighting)]];
        half4 albedo   [[color(AAPLColorIndexAlbedo),   raster_order_group(AAPLRasterOrderGroupGBuffer)]];
        half4 normal   [[color(AAPLColorIndexNormal),   raster_order_group(AAPLRasterOrderGroupGBuffer)]];
        half4 material [[color(AAPLColorIndexMaterial), raster_order_group(AAPLRasterOrderGroupGBuffer)]];
    };

    [[fragment]] DecalFragmentFunctionOutput
    decal_fragment_function(DecalVertexFunctionOutput in [[stage_in]],
                            FragmentFunctionOutput gbuffer,
                            constant MeshUniforms& uniforms [[buffer(AAPLBufferIndexUniforms)]],
                            const device WryDecalInstance* instances [[buffer(AAPLBufferIndexInstanced)]],
                            texture2d<half> image [[texture(AAPLTextureIndexAlbedo)]])
    {
        constexpr sampler trilinear(mag_filter::linear,
                                    min_filter::linear,
                                    mip_filter::linear,
                                    s_address::clamp_to_edge,
                                    t_address::clamp_to_edge);

        WryDecalInstance instance = instances[in.instance_id];
        // The surface under this pixel, in decal space
        float4 position_world = float4(uniforms.camera_position_world.xyz + in.ray * gbuffer.depth, 1.0f);
        float4 q = instance.inverse_model_transform * position_world;
        float3 p = q.xyz / q.w;
        float2 uv = instance.uv_rect.xy + float2(p.x + 0.5f, 0.5f - p.y) * instance.uv_rect.zw;
        // The image's footprint, before any lane leaves (the neighbours'
        // uv is the surface's, which is what the mip level should see)
        float2 duvdx = dfdx(uv), duvdy = dfdy(uv);

        if (gbuffer.depth <= 0.0f || any(abs(p) > 0.5f))
            discard_fragment();
        float3 axis = normalize(instance.model_transform.columns[2].xyz);
        if (dot(float3(gbuffer.normal.xyz), axis) < instance.normal_cutoff)
            discard_fragment();

        half4 s = image.sample(trilinear, uv, gradient2d(duvdx, duvdy));
        half coverage = s.a * half(instance.tint.a);
        half3 colour = half3(instance.tint.rgb) * s.rgb;

        DecalFragmentFunctionOutput out;
        half emitting = half(instance.emissive.a) * coverage;
        out.light = half4(mix(gbuffer.light.rgb, half3(instance.emissive.rgb), emitting), gbuffer.light.a);
        out.albedo = half4(mix(gbuffer.albedo.rgb, colour, coverage), gbuffer.albedo.a);
        half2 toward = half2(instance.material.xy);
        half2 weight = half2(instance.material.zw) * coverage;
        out.material = half4(gbuffer.material.r * (1.0h - half(instance.occlusion) * coverage),
                             mix(gbuffer.material.g, toward.x, weight.x),
                             mix(gbuffer.material.b, toward.y, weight.y),
                             gbuffer.material.a);

        out.normal = gbuffer.normal;
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
            float3 n = float3(gbuffer.normal.xyz);
            float3 t = ex - n * dot(n, ex);
            float3 b = ey - n * dot(n, ey);
            t = normalize(t);
            b = normalize(b - t * dot(t, b));
            out.normal = half4(half3(normalize(n - slope.x * t - slope.y * b)), gbuffer.normal.w);
        }
        return out;
    }

    // ---- The board ------------------------------------------------------
    //
    // The terrain as a printed board (WryTerrainUniforms): one quad at
    // z = 0 over the cells in view, each fragment finding its cell's
    // kind in the per-frame kind map and inking the board itself.  Three
    // layers, in print order: the kind's pastel tint, read on a lattice
    // shifted by the misregistration and blended across cell edges over
    // tint_blur; the lattice of lines on the cell edges, a world width
    // that is clamped to a screen minimum and fades past it (the ink
    // stays constant as the lines thin); and the kind's marks from the
    // symbol field's terrain row, antialiased from the distance.
    // Everything is a function of the ground point, so there is nothing
    // to seam between cells and no texture to mip.

    struct TerrainVertexFunctionOutput {
        float4 position_clip [[position]];
        float2 ground;          // render space, cells centred on the integers
    };

    [[vertex]] TerrainVertexFunctionOutput
    terrain_vertex_function(uint vertex_id [[vertex_id]],
                            constant MeshUniforms& uniforms [[buffer(AAPLBufferIndexUniforms)]],
                            constant WryTerrainUniforms& board [[buffer(AAPLBufferIndexInstanced)]])
    {
        // Two triangles, wound as the mesh quads are, over the map's
        // cells, which are centred on the integers
        // (the draw culls nothing, so the winding only matters for front_facing readers)
        float2 corner = float2(float((0x34u >> vertex_id) & 1u), float((0x1Au >> vertex_id) & 1u));
        float2 ground = float2(board.origin) - 0.5f + corner * float2(board.size);
        TerrainVertexFunctionOutput out;
        out.ground = ground;
        out.position_clip = uniforms.viewprojection_transform * float4(ground, 0.0f, 1.0f);
        return out;
    }

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

    [[fragment]] FragmentFunctionOutput
    terrain_fragment_function(TerrainVertexFunctionOutput in [[stage_in]],
                              constant MeshUniforms& uniforms [[buffer(AAPLBufferIndexUniforms)]],
                              constant WryTerrainUniforms& board [[buffer(AAPLBufferIndexInstanced)]],
                              const device uchar* kinds [[buffer(AAPLBufferIndexVertices)]],
                              texture2d<float> field [[texture(AAPLTextureIndexNormal)]])
    {
        constexpr sampler bilinear(mag_filter::linear,
                                   min_filter::linear,
                                   s_address::clamp_to_edge,
                                   t_address::clamp_to_edge);

        float2 p = in.ground;
        // Ground spanned by a pixel along each axis, before any discard
        float2 wpp = max(fwidth(p), 1e-5f);
        float wpp_mean = 0.5f * (wpp.x + wpp.y);

        int2 cell = int2(round(p));
        int kind = terrain_kind(board, kinds, cell);
        if (kind == WRY_TERRAIN_KIND_NONE)
            discard_fragment();
        kind = clamp(kind, 0, WRY_TERRAIN_KINDS - 1);
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
        float3 albedo = mix(tint, board.line_color.rgb, ink);

        FragmentFunctionOutput out;
        out.light = half4(0.0h);
        out.albedo = half4(half3(albedo), 1.0h);
        out.normal = half4(0.0h, 0.0h, 1.0h, 0.0h);
        out.material = half4(1.0h, half(board.roughness), 0.0h, 0.0h);
        out.depth = 1.0f / in.position_clip.w;
        return out;
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
        // The camera ray through the pixel (MeshUniforms ray basis)
        float3 ray [[center_no_perspective]];
    };
    
    struct LightingFragmentFunctionOutput {
        half4 color [[color(AAPLColorIndexColor), raster_order_group(AAPLRasterOrderGroupLighting)]];
    };

    // The image-based light accumulates apart from the direct light; the
    // composite pass adds it back, scaled by the screen-space occlusion
    struct AmbientFragmentFunctionOutput {
        half4 color [[color(AAPLColorIndexAmbient), raster_order_group(AAPLRasterOrderGroupLighting)]];
    };
        
    [[vertex]] LightingVertexFunctionOutput
    lighting_vertex_function(uint vertexID [[ vertex_id ]],
                       const device float4 *vertexArray [[ buffer(AAPLBufferIndexVertices) ]],
                       constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]])
    {
        LightingVertexFunctionOutput out;
        out.near_clip = vertexArray[vertexID];
        out.ray = camera_ray(uniforms, out.near_clip.xy / out.near_clip.w);
        return out;
    }
    
    
    
    [[fragment]] AmbientFragmentFunctionOutput
    image_based_lighting_fragment_function(LightingVertexFunctionOutput in [[stage_in]],
                                           FragmentFunctionOutput gbuffer,
                                           constant MeshUniforms &uniforms  [[ buffer(AAPLBufferIndexUniforms) ]],
                                           texturecube<float> environmentTexture [[texture(AAPLTextureIndexEnvironment)]],
                                           texturecube<float> irradianceTexture [[texture(AAPLTextureIndexIrradiance)]],
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
        
        AmbientFragmentFunctionOutput out;
        
        float3 albedo    = float3(gbuffer.albedo.rgb);
        float3 normal    = float3(gbuffer.normal.xyz);
        float  occlusion = float(gbuffer.material.r);
        float  roughness = float(gbuffer.material.g);
        float  metallic  = float(gbuffer.material.b);
        
        float3 V = normalize(-in.ray);       // towards the camera
        float3 N = normal;
        float3 R = reflect(-V, N);
        
        float NdotV = saturate(dot(N, V));
        float lod = log2(roughness) + 4;
        
        // The sky's cosine-weighted mean radiance about N (CubeFilterIrradiance)
        float3 diffuseSample = irradianceTexture.sample(bilinearSampler,
                                                        uniforms.ibl_transform * N).rgb;
        
        float3 reflectedSample = environmentTexture.sample(trilinearSampler,
                                                           uniforms.ibl_transform * R,
                                                           level(lod)).rgb;
        
        float4 fresnelSample = fresnelTexture.sample(bilinearSampler,
                                                     float2(NdotV, roughness));
        
        float3 F0 = 0.04f;
        F0 = mix(F0, albedo, metallic);
        
        float3 F = Fresnel_Schlick_roughness(NdotV, F0, roughness);
        
        float3 kS = F;
        float3 kD = (1.0 - kS) * (1.0 - metallic);
        
        // The irradiance map holds the cosine-weighted mean sky radiance
        // L about N; a Lambertian surface under a sky of irradiance
        // E = pi L reflects albedo / pi * E = albedo * L.  No further
        // 1 / pi: a white ground under a uniform sky is exactly as bright
        // as the sky.
        float3 diffuse = albedo * diffuseSample.rgb;
        
        float3 specular = (F * fresnelSample.r + fresnelSample.g) * reflectedSample.rgb;
        
        float3 Lo = (kD * diffuse + specular) * occlusion * uniforms.ibl_scale.rgb;
        out.color.rgb = half3(Lo);

        // Clear coat (see kClearCoatRoughness): the varnish reflects its
        // share of the sharper prefiltered sky and passes the rest
        float clear_coat = float(gbuffer.material.a);
        if (clear_coat > 0.0f) {
            float coatLod = max(log2(kClearCoatRoughness) + 4, 1.0f);   // not level 0
            float3 coatSample = environmentTexture.sample(trilinearSampler,
                                                          uniforms.ibl_transform * R,
                                                          level(coatLod)).rgb;
            float4 coatFresnel = fresnelTexture.sample(bilinearSampler,
                                                       float2(NdotV, kClearCoatRoughness));
            float Ec = clear_coat * (kClearCoatF0 * coatFresnel.r + coatFresnel.g);
            out.color.rgb = half3(Lo * (1.0f - Ec) + Ec * coatSample * occlusion * uniforms.ibl_scale.rgb);
        }

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
        
        float3 albedo    = float3(gbuffer.albedo.rgb);
        float3 normal    = float3(gbuffer.normal.xyz);
        float  roughness = float(gbuffer.material.g);
        float  metallic  = float(gbuffer.material.b);
        float  depth     = gbuffer.depth;
        // direct light is not attenuated by baked ambient occlusion

        float4 position_world = float4(uniforms.camera_position_world.xyz + in.ray * depth, 1.0f);

        float3 V = normalize(-in.ray);       // towards the camera
        float3 N = normal;
        float3 L = uniforms.light_direction;
        float3 H = normalize(V + L);
        
        float4 coordinate_light = uniforms.light_viewprojectiontexture_transform * position_world;
        coordinate_light /= coordinate_light.w;

        // Off the shadow map we have no occluder information, so treat the
        // point as lit.  The map covers the screen plus a margin, so only
        // geometry well above the ground plane near a screen edge lands
        // here; letting clamp_to_edge answer instead smears the boundary
        // texels' occluders into streaks.
        float shadowFactor = 1.0f;
        if (all(0.0f <= coordinate_light.xy) && all(coordinate_light.xy <= 1.0f)) {
            float shadowSample = shadowTexture.sample(nearestSampler, coordinate_light.xy).r;
            shadowFactor = step(coordinate_light.z, shadowSample);
        }
        
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
        out.color.rgb = half3(Lo);

        // Clear coat (see kClearCoatRoughness): a second, sharper,
        // dielectric lobe on the same normal; the base sees what it
        // transmits
        float clear_coat = float(gbuffer.material.a);
        if (clear_coat > 0.0f) {
            float coatAlpha = kClearCoatRoughness * kClearCoatRoughness;
            float Dc = distribution_Trowbridge_Reitz(NdotH, coatAlpha * coatAlpha);
            // Kelemen's visibility, G / (4 NdotV NdotL) ~ 1 / (4 LdotH^2),
            // as Filament's coat uses
            float Vc = 0.25f / max(HdotV * HdotV, 1e-4f);
            float Fc = clear_coat * Fresnel_Schlick(HdotV, float3(kClearCoatF0)).x;
            float coat = Dc * Vc * Fc;
            out.color.rgb = half3(Lo * (1.0f - Fc) + coat * NdotL * uniforms.radiance * shadowFactor);
        }

        // Lo = clamp(Lo, 0.0f, 4096.0f);

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
        
        float3 albedo    = float3(gbuffer.albedo.rgb);
        float3 normal    = float3(gbuffer.normal.xyz);
        float  roughness = float(gbuffer.material.g);
        float  metallic  = float(gbuffer.material.b);
        float  depth     = gbuffer.depth;
        // direct light is not attenuated by baked ambient occlusion

        // position of the fragment in the world
        float4 position_world = float4(uniforms.camera_position_world.xyz + in.ray * depth, 1.0f);
        float3 direction = -in.ray;          // towards the camera

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
    
    
    // ---- Screen-space ambient occlusion: two estimators behind one switch
    //
    // Both read the stored depth (linear view depth, the Depth colour
    // attachment; 0 is the sky) and normal G-buffers after the geometry
    // pass, at half resolution (each thread owns a 2 x 2 block and reads
    // the block's top-left texel; the taps still walk the full-resolution
    // depth), and write (visibility, depth) per block, which a depth-aware
    // blur smooths at the same resolution and the composite upsamples
    // bilaterally; the cost is texture fetches, so a quarter of the
    // threads is a quarter of the time.  The composite multiplies the
    // factor into the image-based light alone (the direct light and its
    // shadow map are untouched, as with the symbol decals).
    //
    // GTAO (Jimenez et al. 2016, as XeGTAO lays it out): for a few screen
    // directions through the pixel, find the horizon angle on each side by
    // walking the depth buffer out to the radius, project the normal into
    // the slice, and integrate the cosine-weighted visible arc
    // analytically -- the same (sin phi2 - sin phi1) / 2 integral the
    // symbol decals evaluate for one shape, here for whatever the depth
    // buffer holds.  Physical, so no strength knob is needed.
    //
    // Luft, Colditz and Deussen 2006 ("Image Enhancement by Unsharp
    // Masking the Depth Buffer"): darken a pixel by how much farther it
    // lies than the mean depth around it.  A perceptual cue rather than a
    // light transport, with a strength to taste.
    //
    // The camera is fixed and elevated, so a world radius is nearly a
    // constant pixel radius; it is projected exactly per pixel anyway.


    // The render-space point under a pixel, from its linear view depth:
    // camera + ray * depth, the ray affine in the pixel (the MeshUniforms
    // ray basis, copied into these uniforms)
    static float3 ssao_position(constant WrySSAOUniforms& u, float2 pixel, float depth) {
        float2 ndc = float2(2.0f * pixel.x / u.viewport.x - 1.0f, 1.0f - 2.0f * pixel.y / u.viewport.y);
        float3 ray = u.camera_ray_centre.xyz + ndc.x * u.camera_ray_dx.xyz + ndc.y * u.camera_ray_dy.xyz;
        return u.camera_position_world.xyz + ray * depth;
    }

    // The radius in pixels at a depth
    static float ssao_radius_pixels(constant WrySSAOUniforms& u, float depth) {
        return clamp(u.radius * u.pixels_per_unit / depth, 2.0f, 256.0f);
    }

    // Interleaved gradient noise (Jimenez), a per-block rotation of the
    // sample pattern that the blur below averages out
    static float ssao_noise(uint2 g) {
        return fract(52.9829189f * fract(0.06711056f * (float)g.x + 0.00583715f * (float)g.y));
    }

    // The depth pyramid the GTAO taps read (XeGTAO's prefilter): level 0 is
    // the Depth G-buffer copied, each coarser level the 2 x 2 below it
    // filtered toward its far side, so that a near occluder does not bloat
    // into the coarse levels and over-darken; sky (0) texels weigh nothing
    static float ssao_depth_mip_filter(float4 d, float radius) {
        float far = max(max(d.x, d.y), max(d.z, d.w));
        if (far <= 0.0f)
            return 0.0f;
        float4 w = saturate((radius - (far - d)) / (0.5f * radius));
        w = select(w, float4(0.0f), d <= 0.0f);
        return dot(d, w) / dot(w, float4(1.0f));
    }

    // One thread per 2 x 2 base texels; a 16 x 16 threadgroup covers a
    // 32 x 32 tile and reduces the coarser levels in threadgroup memory.
    // Dispatched by threadgroups (whole tiles), so the edge threads bound-
    // check every read and write and contribute sky to the reduction
    [[kernel]] void
    ssao_depth_pyramid_kernel(texture2d<float, access::read> depthTexture [[texture(0)]],
                              texture2d<float, access::write> level0 [[texture(1)]],
                              texture2d<float, access::write> level1 [[texture(2)]],
                              texture2d<float, access::write> level2 [[texture(3)]],
                              texture2d<float, access::write> level3 [[texture(4)]],
                              texture2d<float, access::write> level4 [[texture(5)]],
                              constant WrySSAOUniforms& u [[buffer(0)]],
                              uint2 g [[thread_position_in_grid]],
                              uint2 l [[thread_position_in_threadgroup]])
    {
        threadgroup float tile1[16][16];
        threadgroup float tile2[8][8];
        threadgroup float tile3[4][4];
        uint w = depthTexture.get_width();
        uint h = depthTexture.get_height();
        uint2 b = 2u * g;
        float4 d = float4(0.0f);
        if (b.x < w && b.y < h) {
            d.x = depthTexture.read(b).r;
            level0.write(float4(d.x, 0.0f, 0.0f, 0.0f), b);
            if (b.x + 1u < w) {
                d.y = depthTexture.read(b + uint2(1u, 0u)).r;
                level0.write(float4(d.y, 0.0f, 0.0f, 0.0f), b + uint2(1u, 0u));
            }
            if (b.y + 1u < h) {
                d.z = depthTexture.read(b + uint2(0u, 1u)).r;
                level0.write(float4(d.z, 0.0f, 0.0f, 0.0f), b + uint2(0u, 1u));
            }
            if (b.x + 1u < w && b.y + 1u < h) {
                d.w = depthTexture.read(b + uint2(1u, 1u)).r;
                level0.write(float4(d.w, 0.0f, 0.0f, 0.0f), b + uint2(1u, 1u));
            }
        }
        float v = ssao_depth_mip_filter(d, u.radius);
        if (g.x < level1.get_width() && g.y < level1.get_height())
            level1.write(float4(v, 0.0f, 0.0f, 0.0f), g);
        tile1[l.y][l.x] = v;
        threadgroup_barrier(mem_flags::mem_threadgroup);
        if ((l.x & 1u) == 0u && (l.y & 1u) == 0u) {
            v = ssao_depth_mip_filter(float4(tile1[l.y][l.x], tile1[l.y][l.x + 1u],
                                             tile1[l.y + 1u][l.x], tile1[l.y + 1u][l.x + 1u]), u.radius);
            uint2 p = g / 2u;
            if (p.x < level2.get_width() && p.y < level2.get_height())
                level2.write(float4(v, 0.0f, 0.0f, 0.0f), p);
            tile2[l.y / 2u][l.x / 2u] = v;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        if ((l.x & 3u) == 0u && (l.y & 3u) == 0u) {
            uint2 q = l / 2u;
            v = ssao_depth_mip_filter(float4(tile2[q.y][q.x], tile2[q.y][q.x + 1u],
                                             tile2[q.y + 1u][q.x], tile2[q.y + 1u][q.x + 1u]), u.radius);
            uint2 p = g / 4u;
            if (p.x < level3.get_width() && p.y < level3.get_height())
                level3.write(float4(v, 0.0f, 0.0f, 0.0f), p);
            tile3[l.y / 4u][l.x / 4u] = v;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        if ((l.x & 7u) == 0u && (l.y & 7u) == 0u) {
            uint2 q = l / 4u;
            v = ssao_depth_mip_filter(float4(tile3[q.y][q.x], tile3[q.y][q.x + 1u],
                                             tile3[q.y + 1u][q.x], tile3[q.y + 1u][q.x + 1u]), u.radius);
            uint2 p = g / 8u;
            if (p.x < level4.get_width() && p.y < level4.get_height())
                level4.write(float4(v, 0.0f, 0.0f, 0.0f), p);
        }
    }

    // Nearest texel of the nearest level; off-screen taps read sky
    constexpr sampler ssao_depth_sampler(coord::normalized, filter::nearest, mip_filter::nearest, address::clamp_to_zero);

    // XeGTAO's mip selection: a tap d pixels out reads level log2(d) - 3.3,
    // so level 0 within ~10 pixels, level 1 to ~20, and so on
    static float ssao_depth_mip(float distance_px) {
        return clamp(log2(distance_px) - 3.3f, 0.0f, (float)(WRY_SSAO_DEPTH_PYRAMID_LEVELS - 1));
    }

    [[kernel]] void
    ssao_gtao_kernel(texture2d<float, access::sample> depthPyramid [[texture(0)]],
                     texture2d<float, access::read> normalTexture [[texture(1)]],
                     texture2d<float, access::write> aoTexture [[texture(2)]],
                     texture2d<float, access::read> depthTexture [[texture(3)]],
                     constant WrySSAOUniforms& u [[buffer(0)]],
                     uint2 g [[thread_position_in_grid]])
    {
        if (g.x >= aoTexture.get_width() || g.y >= aoTexture.get_height())
            return;
        uint2 f = 2u * g;                    // the block's representative texel
        float depth = u.pyramid ? depthPyramid.read(f, 0).r : depthTexture.read(f).r;
        if (depth <= 0.0f) {
            aoTexture.write(float4(1.0f, 0.0f, 0.0f, 0.0f), g);
            return;
        }
        float2 pixel = float2(f) + 0.5f;
        float3 P = ssao_position(u, pixel, depth);
        float3 N = normalize(normalTexture.read(f).xyz);
        float3 V = normalize(u.camera_position_world.xyz - P);
        float radius_px = ssao_radius_pixels(u, depth);
        float noise = ssao_noise(g);
        float step_noise = ssao_noise(g + uint2(37u, 91u));
        float2 inv_viewport = 1.0f / u.viewport;

        float visibility = 0.0f;
        for (int s = 0; s != u.slices; ++s) {
            float phi = ((float)s + noise) * (M_PI_F / (float)u.slices);
            float2 dir = float2(cos(phi), sin(phi));
            // The slice's direction in the world: along the screen
            // direction at this pixel's depth, flattened against V
            float3 along = ssao_position(u, pixel + dir * radius_px, depth) - P;
            float3 slice_dir = along - V * dot(along, V);
            float slice_len = length(slice_dir);
            if (slice_len < 1e-6f)
                continue;
            slice_dir /= slice_len;

            float horizon_cos[2] = {-1.0f, -1.0f};
            for (int side = 0; side != 2; ++side) {
                float sgn = side == 0 ? 1.0f : -1.0f;
                for (int i = 0; i != u.steps; ++i) {
                    float t = ((float)i + 0.5f + step_noise * 0.5f) / (float)u.steps;
                    t = t * t;                       // denser near the pixel
                    float distance_px = t * radius_px;
                    float2 sp = pixel + sgn * dir * distance_px;
                    float sd;
                    if (u.pyramid) {
                        sd = depthPyramid.sample(ssao_depth_sampler, sp * inv_viewport, level(ssao_depth_mip(distance_px))).r;
                    } else {
                        if (any(sp < 0.0f) || any(sp >= u.viewport))
                            break;
                        sd = depthTexture.read(uint2(sp)).r;
                    }
                    if (sd <= 0.0f)
                        continue;
                    float3 S = ssao_position(u, sp, sd);
                    float3 d = S - P;
                    float dist = length(d);
                    if (dist < 1e-4f)
                        continue;
                    float c = dot(d / dist, V);
                    // Fade occluders out over the last quarter of the radius
                    float falloff = saturate((u.radius - dist) / (0.25f * u.radius));
                    c = mix(-1.0f, c, falloff);
                    horizon_cos[side] = max(horizon_cos[side], c);
                }
            }

            float3 axis = normalize(cross(slice_dir, V));
            float3 projN = N - axis * dot(N, axis);
            float projLen = length(projN);
            if (projLen < 1e-4f)
                continue;
            float cosN = clamp(dot(projN / projLen, V), -1.0f, 1.0f);
            float n = sign(dot(slice_dir, projN)) * acos(cosN);
            float h0 = -acos(horizon_cos[1]);    // the negative side
            float h1 = acos(horizon_cos[0]);     // the positive side
            h0 = n + clamp(h0 - n, -M_PI_F * 0.5f, M_PI_F * 0.5f);
            h1 = n + clamp(h1 - n, -M_PI_F * 0.5f, M_PI_F * 0.5f);
            float arc0 = (cosN + 2.0f * h0 * sin(n) - cos(2.0f * h0 - n)) * 0.25f;
            float arc1 = (cosN + 2.0f * h1 * sin(n) - cos(2.0f * h1 - n)) * 0.25f;
            visibility += projLen * (arc0 + arc1);
        }
        float ao = saturate(visibility / (float)u.slices);
        ao = 1.0f - u.strength * (1.0f - ao);
        aoTexture.write(float4(ao, depth, 0.0f, 0.0f), g);
    }

    [[kernel]] void
    ssao_luft_kernel(texture2d<float, access::read> depthTexture [[texture(0)]],
                     texture2d<float, access::read> normalTexture [[texture(1)]],
                     texture2d<float, access::write> aoTexture [[texture(2)]],
                     constant WrySSAOUniforms& u [[buffer(0)]],
                     uint2 g [[thread_position_in_grid]])
    {
        if (g.x >= aoTexture.get_width() || g.y >= aoTexture.get_height())
            return;
        uint2 f = 2u * g;                    // the block's representative texel
        float z = depthTexture.read(f).r;
        if (z <= 0.0f) {
            aoTexture.write(float4(1.0f, 0.0f, 0.0f, 0.0f), g);
            return;
        }
        float2 pixel = float2(f) + 0.5f;
        float radius_px = ssao_radius_pixels(u, z);
        // The Gaussian-weighted mean view depth around the pixel (sigma =
        // half the radius): a 9 x 9 grid of taps rotated per pixel so the
        // grid never shows, the blur pass after smoothing the rotation
        float noise = ssao_noise(g) * (M_PI_F * 0.5f);
        float2x2 rotate = float2x2(float2(cos(noise), sin(noise)), float2(-sin(noise), cos(noise)));
        float sum = 0.0f, weight = 0.0f;
        for (int j = -4; j <= 4; ++j) {
            for (int i = -4; i <= 4; ++i) {
                float2 o = float2(i, j) / 4.0f;
                float r2 = dot(o, o);
                if (r2 > 1.0f)
                    continue;
                float2 sp = pixel + (rotate * o) * radius_px;
                if (any(sp < 0.0f) || any(sp >= u.viewport))
                    continue;
                uint2 sg = uint2(sp);
                float sd = depthTexture.read(sg).r;
                float sz = sd <= 0.0f ? z + u.radius : sd;
                float w = exp(-4.0f * r2);
                sum += sz * w;
                weight += w;
            }
        }
        float blurred = weight > 0.0f ? sum / weight : z;
        // Farther than the surroundings: darken, over one radius
        float ao = 1.0f - u.strength * saturate((z - blurred) / u.radius);
        aoTexture.write(float4(ao, z, 0.0f, 0.0f), g);
    }

    // The blur over the (visibility, depth) blocks, a (2r + 1) square
    // window with r = u.blur_radius, that stays on the surface: taps
    // weighted down as their depth departs from the centre's (sigma a
    // tenth of the radius), sky taps dropped.  The depth rides along for
    // the composite's upsample.  Three kernels run the same filter
    // (u.blur picks one, a measuring aid until the choice is made): the
    // direct window, (2r + 1)^2 reads per block; separable, two
    // dispatches of 2r + 1 reads each through the other occlusion
    // texture, the second pass weighting by the first pass's unchanged
    // centre depths (an approximation of the window: the depth weights
    // do not factor); and the direct window over a threadgroup-memory
    // tile, (16 + 2r)^2 / 256 reads per block

    static float ssao_blur_weight(float2 s, float z, float sigma) {
        if (s.g <= 0.0f)
            return 0.0f;
        float dz = (s.g - z) / sigma;
        return exp(-dz * dz);
    }

    [[kernel]] void
    ssao_blur_kernel(texture2d<float, access::read> aoTexture [[texture(0)]],
                     texture2d<float, access::write> outTexture [[texture(1)]],
                     constant WrySSAOUniforms& u [[buffer(0)]],
                     uint2 g [[thread_position_in_grid]])
    {
        int2 size = int2(outTexture.get_width(), outTexture.get_height());
        if (g.x >= (uint)size.x || g.y >= (uint)size.y)
            return;
        float2 centre = aoTexture.read(g).rg;
        float z = centre.g;
        if (z <= 0.0f) {
            outTexture.write(float4(1.0f, 0.0f, 0.0f, 0.0f), g);
            return;
        }
        float sigma = 0.1f * u.radius;
        int r = u.blur_radius;
        float sum = 0.0f, weight = 0.0f;
        for (int j = -r; j <= r; ++j) {
            for (int i = -r; i <= r; ++i) {
                int2 sp = int2(g) + int2(i, j);
                if (any(sp < 0) || any(sp >= size))
                    continue;
                float2 s = aoTexture.read(uint2(sp)).rg;
                float w = ssao_blur_weight(s, z, sigma);
                sum += s.r * w;
                weight += w;
            }
        }
        outTexture.write(float4(weight > 0.0f ? sum / weight : 1.0f, z, 0.0f, 0.0f), g);
    }

    // One axis of the separable blur: axis is (1, 0) then (0, 1)
    [[kernel]] void
    ssao_blur_axis_kernel(texture2d<float, access::read> aoTexture [[texture(0)]],
                          texture2d<float, access::write> outTexture [[texture(1)]],
                          constant WrySSAOUniforms& u [[buffer(0)]],
                          constant int2& axis [[buffer(1)]],
                          uint2 g [[thread_position_in_grid]])
    {
        int2 size = int2(outTexture.get_width(), outTexture.get_height());
        if (g.x >= (uint)size.x || g.y >= (uint)size.y)
            return;
        float2 centre = aoTexture.read(g).rg;
        float z = centre.g;
        if (z <= 0.0f) {
            outTexture.write(float4(1.0f, 0.0f, 0.0f, 0.0f), g);
            return;
        }
        float sigma = 0.1f * u.radius;
        int r = u.blur_radius;
        float sum = 0.0f, weight = 0.0f;
        for (int k = -r; k <= r; ++k) {
            int2 sp = int2(g) + k * axis;
            if (any(sp < 0) || any(sp >= size))
                continue;
            float2 s = aoTexture.read(uint2(sp)).rg;
            float w = ssao_blur_weight(s, z, sigma);
            sum += s.r * w;
            weight += w;
        }
        outTexture.write(float4(weight > 0.0f ? sum / weight : 1.0f, z, 0.0f, 0.0f), g);
    }

    // The direct window over a tile of the texture staged in threadgroup
    // memory: each 16 x 16 threadgroup loads its (16 + 2r)^2 texels once
    // (out-of-range texels as sky, which the filter drops).  Dispatched as
    // whole threadgroups so every thread loads and reaches the barrier;
    // the threads past the edge then write nothing
    #define WRY_SSAO_BLUR_TILE (16 + 2 * WRY_SSAO_BLUR_MAX_RADIUS)
    [[kernel]] void
    ssao_blur_tiled_kernel(texture2d<float, access::read> aoTexture [[texture(0)]],
                           texture2d<float, access::write> outTexture [[texture(1)]],
                           constant WrySSAOUniforms& u [[buffer(0)]],
                           uint2 g [[thread_position_in_grid]],
                           uint2 l [[thread_position_in_threadgroup]],
                           uint2 tg [[threadgroup_position_in_grid]])
    {
        threadgroup float2 tile[WRY_SSAO_BLUR_TILE * WRY_SSAO_BLUR_TILE];
        int2 size = int2(outTexture.get_width(), outTexture.get_height());
        int r = u.blur_radius;
        int t = 16 + 2 * r;
        int2 origin = int2(tg) * 16 - r;
        int lid = (int)l.y * 16 + (int)l.x;
        for (int idx = lid; idx < t * t; idx += 256) {
            int2 sp = origin + int2(idx % t, idx / t);
            bool inside = all(sp >= 0) && all(sp < size);
            tile[idx] = inside ? aoTexture.read(uint2(sp)).rg : float2(1.0f, 0.0f);
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        if (g.x >= (uint)size.x || g.y >= (uint)size.y)
            return;
        int2 c = int2(l) + r;
        float2 centre = tile[c.y * t + c.x];
        float z = centre.g;
        if (z <= 0.0f) {
            outTexture.write(float4(1.0f, 0.0f, 0.0f, 0.0f), g);
            return;
        }
        float sigma = 0.1f * u.radius;
        float sum = 0.0f, weight = 0.0f;
        for (int j = -r; j <= r; ++j) {
            for (int i = -r; i <= r; ++i) {
                float2 s = tile[(c.y + j) * t + (c.x + i)];
                float w = ssao_blur_weight(s, z, sigma);
                sum += s.r * w;
                weight += w;
            }
        }
        outTexture.write(float4(weight > 0.0f ? sum / weight : 1.0f, z, 0.0f, 0.0f), g);
    }

    // ---- The composite: direct + occlusion * ambient, in a second pass
    // over the two light attachments once the occlusion texture exists.
    // The occlusion is at half resolution, so each pixel blends the four
    // blocks around it bilinearly, each weighted by how close the block's
    // depth is to the pixel's own (the stored depth, bound as a texture
    // here): the halo stops at a silhouette instead of bleeding across it.
    // A pixel no block sits on (a one-pixel feature) takes the plain
    // bilinear blend.

    struct CompositeVertexFunctionOutput {
        float4 position [[position]];
    };

    [[vertex]] CompositeVertexFunctionOutput
    composite_vertex_function(uint vertex_id [[vertex_id]],
                              const device float4* vertices [[buffer(AAPLBufferIndexVertices)]])
    {
        CompositeVertexFunctionOutput out;
        out.position = vertices[vertex_id];
        return out;
    }

    struct CompositeFragmentFunctionInput {
        half4 color   [[color(AAPLColorIndexColor),   raster_order_group(AAPLRasterOrderGroupLighting)]];
        half4 ambient [[color(AAPLColorIndexAmbient), raster_order_group(AAPLRasterOrderGroupLighting)]];
    };

    [[fragment]] LightingFragmentFunctionOutput
    composite_fragment_function(CompositeVertexFunctionOutput in [[stage_in]],
                                CompositeFragmentFunctionInput g,
                                constant WrySSAOUniforms& u [[buffer(AAPLBufferIndexUniforms)]],
                                texture2d<float, access::read> aoTexture [[texture(AAPLTextureIndexColor)]],
                                texture2d<float, access::read> depthTexture [[texture(AAPLTextureIndexNormal)]])
    {
        float ao = 1.0f;
        if (u.mode != WRY_SSAO_OFF) {
            uint2 p = uint2(in.position.xy);
            float z = depthTexture.read(p).r;
            if (z > 0.0f) {
                // Block h represents texel 2h, so in block units the pixel
                // sits at p / 2: even pixels on a block, odd ones between
                float2 s = float2(p) * 0.5f;
                int2 h0 = int2(floor(s));
                float2 f = s - float2(h0);
                int2 hmax = int2(aoTexture.get_width() - 1, aoTexture.get_height() - 1);
                float sigma = 0.1f * u.radius;
                float sum = 0.0f, weight = 0.0f, plain = 0.0f;
                for (int j = 0; j != 2; ++j) {
                    for (int i = 0; i != 2; ++i) {
                        int2 h = clamp(h0 + int2(i, j), int2(0), hmax);
                        float2 c = aoTexture.read(uint2(h)).rg;
                        float bw = (i ? f.x : 1.0f - f.x) * (j ? f.y : 1.0f - f.y);
                        float dz = (c.g - z) / sigma;
                        float w = bw * (c.g > 0.0f ? exp(-dz * dz) : 0.0f);
                        sum += c.r * w;
                        weight += w;
                        plain += c.r * bw;
                    }
                }
                ao = weight > 1e-3f ? sum / weight : plain;
            }
        }
        LightingFragmentFunctionOutput out;
        if (u.show)
            out.color = half4(half3(ao), 1.0h);
        else
            out.color = half4(g.color.rgb + g.ambient.rgb * (half)ao, 1.0h);
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

// The irradiance map: for each direction N, the cosine-weighted mean of
// the environment's radiance over N's hemisphere, E(N) / pi, so that a
// Lambertian surface reflects albedo times the texel (no further
// normalization).  Sampled directly in the hemisphere with a cosine
// density (Malley's method), so every sample carries equal weight.
[[fragment]] float4
CubeFilterIrradiance(CubeFilterVertexOut in [[stage_in]],
                     texture2d<float> environmentMap [[ texture(AAPLTextureIndexColor) ]])
{
    constexpr sampler bilinearSampler(mag_filter::linear,
                                      min_filter::linear,
                                      s_address::repeat,
                                      t_address::clamp_to_edge);

    float3 N = normalize(in.normal.xyz);
    float3 U = (abs(N.z) < 0.5) ? float3(0, 0, 1) : float3(0, 1, 0);
    float3x3 T;
    T.columns[0] = normalize(cross(N, U));
    T.columns[1] = normalize(cross(N, T.columns[0]));
    T.columns[2] = N;

    float3 sum = 0;
    ushort M = 4096;
    for (ushort i = 0; i != M; ++i) {
        float2 X = sequence_Hammersley(i, M);
        float r = sqrt(X.x);
        float phi = 2.0f * M_PI_F * X.y;
        float3 D = T * float3(r * cos(phi), r * sin(phi), sqrt(1.0f - X.x));
        float u = atan2(D.y, D.x) * 0.5 * M_1_PI_F;
        float v = acos(D.z) * M_1_PI_F;
        float4 environmentalSample = environmentMap.sample(bilinearSampler, float2(u, v));
        sum += select(environmentalSample.rgb, float3(0), isinf(environmentalSample.rgb)) / M;
    }
    return float4(sum, 1.0f);
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
    
    
    // Karis's prefilter: with V = N, each sample's reflected direction is
    // weighted by N.L and the sum normalized by the weights, which drops
    // the directions below the surface that wide lobes would otherwise
    // fold in and approximates the missing geometry and cosine terms of
    // the split-sum's first factor
    float3 sum = 0;
    float weight = 0;
    ushort M = 16384;
    for (ushort i = 0; i != M; ++i) {
        float2 X = sequence_Hammersley(i, M);
        float3 H = T * sample_Trowbridge_Reitz(X, alpha2);
        float3 R = reflect(-V, H);
        float NdotL = dot(N, R);
        if (NdotL <= 0.0f)
            continue;
        float phi = atan2(R.y, R.x) * 0.5 * M_1_PI_F;
        float theta = acos(R.z) * M_1_PI_F;
        float4 environmentalSample = environmentMap.sample(bilinearSampler, float2(phi, theta));
        sum += select(environmentalSample.rgb, float3(0), isinf(environmentalSample.rgb)) * NdotL;
        weight += NdotL;
    }
    
    return float4(sum / max(weight, 1e-6f), 1.0f);
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

// Like fragmentShader, but linearly filtered.  Backdrop images (splash / menu)
// are magnified on screen, where nearest filtering makes the slowly-panning
// photo crawl / shimmer; bilinear sampling smooths it out.
[[fragment]] basicFragmentShaderOut
imageFragmentShader(RasterizerData in [[stage_in]],
                    texture2d<half> colorTexture [[ texture(AAPLTextureIndexColor) ]])
{
    constexpr sampler textureSampler (mag_filter::linear,
                                      min_filter::linear);
    const half4 colorSample = colorTexture.sample(textureSampler, in.texCoord);
    return { colorSample * half4(in.color) };
}

#pragma mark - GUI quads

// See WryQuadInstance (ShaderTypes.h).

struct QuadRasterizerData {
    float4 clipSpacePosition [[position]];
    float2 texCoord;
    // Fragment position in pixels relative to the shape center.
    float2 local;
    float4 color [[flat]];
    float4 radii [[flat]];
    float2 half_extent [[flat]];
    float sigma [[flat]];
    uint flags [[flat]];
};

[[vertex]] QuadRasterizerData
quadVertexShader(uint vertexID [[ vertex_id ]],
                 uint instanceID [[ instance_id ]],
                 const device WryQuadInstance* instances [[ buffer(AAPLBufferIndexVertices) ]],
                 constant MyUniforms& uniforms [[ buffer(AAPLBufferIndexUniforms) ]])
{
    const WryQuadInstance q = instances[instanceID];
    // Triangle strip: 0 = (x0, y0), 1 = (x1, y0), 2 = (x0, y1), 3 = (x1, y1)
    const bool2 hi = bool2(vertexID & 1, vertexID & 2);
    const float2 position = select(q.draw.xy, q.draw.zw, hi);
    const float2 center = (q.shape.xy + q.shape.zw) * 0.5;

    QuadRasterizerData out;
    out.clipSpacePosition = uniforms.position_transform * float4(position, 0.0, 1.0);
    out.texCoord = select(q.uv.xy, q.uv.zw, hi);
    out.local = position - center;
    out.color = unpack_unorm4x8_srgb_to_float(q.color);
    out.radii = q.radii;
    out.half_extent = (q.shape.zw - q.shape.xy) * 0.5;
    out.sigma = q.sigma;
    out.flags = q.flags;
    return out;
}

// Coverage of a rounded box blurred by an isotropic Gaussian of standard
// deviation sigma, at p relative to the box center (y-down).  h is the
// half-extent; radii are per corner, ordered (x0,y0) (x1,y0) (x1,y1)
// (x0,y1), each at most min(h).
//
// After Evan Wallace, "Fast Rounded Rectangle Shadows", generalized to
// per-corner radii.  The Gaussian is separable, so integrate exactly in x
// (each row of a rounded box is one interval, whose ends depend on that
// row's left and right corner radii) and numerically in y (midpoint rule
// over the +/- 3 sigma window clipped to the box's rows).  Against brute-
// force 2D convolution, for sigma 0.5..4 and radii 0..8 px, the maximum
// error is ~0.006 with 6 samples (4 samples: ~0.014).  The previous
// distance-to-edge approximation erred by 0.25 at sharp corners, where the
// true value is the product of two edge integrals.
static float rounded_box_coverage(float2 p, float2 h, float4 radii, float sigma) {
    constexpr int N = 6;
    const float lo = clamp(-3.0f * sigma, p.y - h.y, p.y + h.y);
    const float hi = clamp(+3.0f * sigma, p.y - h.y, p.y + h.y);
    const float step = (hi - lo) / N;
    const float k = M_SQRT1_2_F / sigma; // erf argument scale
    float sum = 0.0f;
    for (int i = 0; i != N; ++i) {
        const float t = lo + step * (i + 0.5f); // Gaussian offset
        const float y = p.y - t;                // row, relative to center
        // (left, right) corner radii for this row
        const float2 r = (y < 0.0f) ? radii.xy : radii.wz;
        const float2 delta = min(h.y - r - abs(y), 0.0f);
        // (left, right) extent of the row from the center
        const float2 e = h.x - r + sqrt(max(0.0f, r * r - delta * delta));
        // Phi((x + e.l) / sigma) - Phi((x - e.r) / sigma)
        const float2 E = otf::erf6(float2(p.x + e.x, p.x - e.y) * k);
        sum += 0.5f * (E.x - E.y) * exp(-0.5f * (t / sigma) * (t / sigma));
    }
    return sum * step / (sigma * sqrt(2.0f * M_PI_F));
}

[[fragment]] basicFragmentShaderOut
quadFragmentShader(QuadRasterizerData in [[stage_in]],
                   texture2d<half> colorTexture [[ texture(AAPLTextureIndexColor) ]])
{
    half4 color = half4(in.color);

    if (in.flags & WryQuadFlagTextured) {
        constexpr sampler nearestSampler(mag_filter::nearest,
                                         min_filter::nearest);
        constexpr sampler trilinearSampler(mag_filter::linear,
                                           min_filter::linear,
                                           mip_filter::linear);
        half4 sample = (in.flags & WryQuadFlagFiltered)
            ? colorTexture.sample(trilinearSampler, in.texCoord)
            : colorTexture.sample(nearestSampler, in.texCoord);
        if (in.flags & WryQuadFlagStraight)
            sample.rgb *= sample.a;
        if (in.flags & WryQuadFlagOutlined) {
            // A glyph over its outline: the coverage dilated by a disc of
            // radius WRY_GLYPH_OUTLINE_RADIUS texels, as the max over the
            // integer offsets inside it (21 taps, the center being the
            // sample already taken).  The coverage is Gaussian-prefiltered
            // (font.mm), so the max of shifted copies keeps an antialiased
            // edge.  font.mm pads each glyph so no tap leaves its image.
            const float2 texel = 1.0f / float2(colorTexture.get_width(),
                                               colorTexture.get_height());
            constexpr int R = int(WRY_GLYPH_OUTLINE_RADIUS);
            constexpr float R2 = WRY_GLYPH_OUTLINE_RADIUS * WRY_GLYPH_OUTLINE_RADIUS;
            half dilated = sample.a;
            for (int dy = -R; dy <= R; ++dy)
                for (int dx = -R; dx <= R; ++dx)
                    if (float(dx * dx + dy * dy) <= R2)
                        dilated = max(dilated,
                                      colorTexture.sample(nearestSampler,
                                                          in.texCoord + float2(dx, dy) * texel).a);
            // Light strokes are thinner than the prefilter, so their peak
            // coverage is well below one; doubling saturates the outline
            // (its edge moves out to the 25% coverage contour).  Black,
            // nearly opaque, faded with the tint.
            constexpr half kOutlineAlpha = 0.9h;
            const half4 fill = color * sample;
            const half outline = saturate(dilated * 2.0h) * kOutlineAlpha * color.a;
            color = fill + (1.0h - fill.a) * half4(0.0h, 0.0h, 0.0h, outline);
        } else {
            color *= sample;
        }
    }

    if (in.flags & WryQuadFlagMasked) {
        // sigma = 0.5 for edges matches the prefilter of the rasterized
        // glyphs (font.mm coverage_at_pixel), so box edges and text edges
        // agree; larger sigma makes a drop shadow.
        color *= half(rounded_box_coverage(in.local, in.half_extent,
                                           in.radii, in.sigma));
    }

    return { color };
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


namespace mip {

    // One level of a mip chain from the level above: the separable kernel
    // WryMipUniforms::taps along x then y, decimating by 2, in linear
    // light (an sRGB texture decodes on read and encodes on write).  Both
    // passes run over a threadgroup-memory tile: the source block is read
    // once, filtered along x into `rows`, then along y.  Taps are clamped
    // to the destination texel's atlas cell (or the texture), per axis;
    // clamped taps only ever move toward the centre, so they stay inside
    // the loaded block
    constant constexpr int SOURCE_TILE = 2 * WRY_MIP_TILE + WRY_MIP_MAX_TAPS - 2;

    static int2 cell_bounds(int d, int n, int cell) {
        if (cell >= 2) {
            int lo = ((2 * d) / cell) * cell;
            return int2(lo, min(lo + cell, n) - 1);
        }
        return int2(0, n - 1);
    }

    [[kernel]] void
    downsample_kernel(texture2d<float, access::read> source [[texture(0)]],
                      texture2d<float, access::write> destination [[texture(1)]],
                      constant WryMipUniforms& u [[buffer(0)]],
                      uint2 group [[threadgroup_position_in_grid]],
                      uint2 local [[thread_position_in_threadgroup]],
                      uint index [[thread_index_in_threadgroup]])
    {
        threadgroup float4 tile[SOURCE_TILE][SOURCE_TILE];
        threadgroup float4 rows[SOURCE_TILE][WRY_MIP_TILE];

        const int2 n = int2(source.get_width(), source.get_height());
        const int2 m = int2(destination.get_width(), destination.get_height());
        const int half_count = u.count / 2 - 1;
        const int2 base = int2(group) * WRY_MIP_TILE;
        // Source texel of tile[0][0]: the first tap of the block's first
        // destination texel, for the widest kernel
        const int2 origin = 2 * base - (WRY_MIP_MAX_TAPS / 2 - 1);
        const int threads = WRY_MIP_TILE * WRY_MIP_TILE;

        for (int i = (int)index; i < SOURCE_TILE * SOURCE_TILE; i += threads) {
            int2 p = clamp(origin + int2(i % SOURCE_TILE, i / SOURCE_TILE), int2(0), n - 1);
            float4 c = source.read(uint2(p));
            if (u.straight)
                c.rgb *= c.a + u.epsilon;
            tile[i / SOURCE_TILE][i % SOURCE_TILE] = c;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);

        for (int i = (int)index; i < SOURCE_TILE * WRY_MIP_TILE; i += threads) {
            int r = i / WRY_MIP_TILE;
            int x = i % WRY_MIP_TILE;
            int d = base.x + x;
            int2 b = cell_bounds(d, n.x, u.cell);
            float4 sum = 0.0f;
            for (int k = 0; k != u.count; ++k) {
                int s = clamp(2 * d - half_count + k, b.x, b.y);
                sum += u.taps[k] * tile[r][s - origin.x];
            }
            rows[r][x] = sum;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);

        const int2 d = base + int2(local);
        if (d.x >= m.x || d.y >= m.y)
            return;
        int2 b = cell_bounds(d.y, n.y, u.cell);
        float4 sum = 0.0f;
        for (int k = 0; k != u.count; ++k) {
            int s = clamp(2 * d.y - half_count + k, b.x, b.y);
            sum += u.taps[k] * rows[s - origin.y][local.x];
        }
        if (u.straight)
            sum.rgb /= sum.a + u.epsilon;
        destination.write(saturate(sum), uint2(d));
    }

} // namespace mip
