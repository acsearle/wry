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

            // construct quad in texture coordinate space
            a[0] = float2(gi.a.x, gi.a.y);
            a[1] = float2(gi.a.x, gi.b.y);
            a[2] = float2(gi.b.x, gi.a.y);
            a[3] = float2(gi.b.x, gi.b.y);
            
            
            
            
            float4 q[4];
            float2 b[4];
            for (int i = 0; i != 4; ++i) {
                // apply positioning in texture space
                // transform vertices to screen space
                q[i] = uniforms.transformation * float4(a[i] + ch.position, 0, 1);
                b[i] = q[i].xy / q[i].w;
            }
            
            // b is a screen-space quad
            // find the axis-aligned bounding box of b, and pad it by some
            // number of pixels
            
            // pixel_size is 2.0 / (viewport width, viewport height)
            
            // TODO: This is an AABB of a transformed AABB; supply hull instead?
            
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
    
    // TODO: Smoothstep vs erfc
    
    // TODO: We don't actually want to find the exact x and y crossings, we
    // just want to sample sufficiently in the region where erfc is changing

    
    [[fragment]] BezierFragmentOut
    bezierFragmentFunction_v0(BezierFragmentIn input [[stage_in]],
                           const device otf::CubicBezier* bez [[buffer(0)]]) {
        
        float2 coordinate = input.v.coordinate.xy;
        
        // float2 ddpx = fwidth(coordinate);
        float2x2 C = {{1.0, 0.0}, {1.0, 0.0}};
        
        {
            // Compute the coordinate system (u, v') in which a circle in x, y
            // is an becomes an axis-aligned ellipse
            
            // Transformation dxy -> duv
            float2x2 A = float2x2{dfdx(coordinate), dfdy(coordinate)} * sqrt(0.5);
            // Transformation duv -> dxy
            float2x2 B = inverse(A);
            
            // Unit vector u
            float2 u = float2{1.0, 0.0};
            // Image of u in xy
            float2 u2 = B * u;
            // Normalize it to unit circle
            float2 u3 = normalize(u2);
            // Perpendicular
            // - in general differs from the image of v
            // - is also normalized
            // - choice of sign determines handedness
            float2 v3 = float2{u3[1], -u3[0]};
            // Transform back to uv space
            float2 u4 = A * u3;
            float2 v4 = A * v3;
            // u4.y will be (approximately) zero
            float2x2 D = { u4, v4 };
            C = inverse(D);
            
            
            
            

            
            
        }
    
        BezierFragmentOut result{};
        
        float cumulant = 0.0;
        
        // Every pixel in the glyph's quad walks this same region, hopefully
        // with linear and coherent memory access, and with no divergence of
        // control flow
        for (uint j = input.p.begin; j != input.p.end; ++j) {
            CubicBezier curve;
            curve.a = C*(bez[j].a - coordinate.xy);
            curve.b = C*(bez[j].b - coordinate.xy);
            curve.c = C*(bez[j].c - coordinate.xy);
            curve.d = C*(bez[j].d - coordinate.xy);

            // The curve is now transformed into something approximating screen
            // space.  The curve xy(t) was formerly constrained to be monotonic
            // and single-valued with respect to both x and y, but the shear
            // part of the transform has broken this for x and curves like
            // x = (t-0.5)**2 are now permitted. y remains monotonic and x(y)
            // is single valued.
            
            // Note that the control points still bound the curve so it is
            // valid to early-out on the hull failing to intersect the
            // kernel.
            
            // To integrate the area to the right of the curve, weighted by
            // the 2d unit normal distribution, further transform the curve
            // by compacting x,y -> erfc((x,y)); the area to the right of
            // this curve is the desired quanitity.   This transformation is
            // itself monotonic for each coordinate.
            
            // We can now integrate \int_0^2 x(y)dy = \int_0^1 x(t) dydt(t) dt
            
            // Via the trapezoid rule, for (x,y) samples on the curve
            
            // If we sample at fixed points, our sampling becomes too coarse at
            // large magnficiations
            
            // We nominate 5 critical points for the integral:
            //     t = 0
            //     t = 1
            //     y = 0 (may not exist)
            //     x = 0 (zero, one or two solutions)
            // These are the endpoints, and the points where the derivative of
            // erfc is maximized.  A different point of interest might be the
            // closest approach.  Notably we don't need to exactly find these
            // points, we just need to get samples somewhere in the middle
            // of the x and y erfcs.
            //
            // We also sample midway (in t) between these points.  In
            // particular, this captures the inflection between the x=0
            // crossings
            
            // Finding the roots in t for x=0 and y=0 is somewhat awkward and
            // doesn't scale well to cubics.  An alternative approach would be
            // some kind of direct bisection or even Newton-Raphson inspired
            // unequal bisection, to progressively refine the trapezoids with
            // the worst error bounds.  We know, a priori, a great deal about
            // the curve and integral even after the shearing.
            
            // We could early-out aggressively on the basis of y.
            
            // Bisection and early-out may not be an optimization, however if it
            // forces the threadgroup(simdgroup?) control flow to diverge?
            
            // If we use a polynomial approximation for erfc, the integral
            // itself becomes a (piecewise)polynomial, in high powers of t.
            // However we would still need to root-find the patch boundaries,
            // equivalent to finding where the curve crosses the boundary of some
            // square region.
            /*
            
            float ts[9];
            ts[0] = 0.0;
            ts[2] = 0.25;
            ts[4] = 0.50;
            ts[6] = 0.75;
            //ts[2] = bezier_t_for_y(0.0, 0.0, 1.0, curve.a, curve.b, curve.c);
            //float2 tsx0 = bezier_ts_for_x(0.0, 0.0, 1.0, curve.a, curve.b, curve.c);
            //ts[4] = tsx0[0];
            //ts[6] = tsx0[1];
            ts[4] = bezier_t_for_y(0.0, 0.0, 1.0, curve.a, curve.b, curve.c, curve.d);
            ts[8] = 1.0;
            // clumsy sort
            if (ts[2] > ts[4]) {
                float tmp = ts[2];
                ts[2] = ts[4];
                ts[4] = tmp;
            }
            if (ts[2] > ts[6]) {
                float tmp = ts[2];
                ts[2] = ts[6];
                ts[6] = tmp;
            }
            if (ts[4] > ts[6]) {
                float tmp = ts[4];
                ts[4] = ts[6];
                ts[6] = tmp;
            }
            ts[1] = (ts[0] + ts[2]) * 0.5;
            ts[3] = (ts[2] + ts[4]) * 0.5;
            ts[5] = (ts[4] + ts[6]) * 0.5;
            ts[7] = (ts[6] + ts[8]) * 0.5;
            float2 points[9];
            
            // float scale_hack = input.v.position.z;
//
             
             */
            
            // Now we have to solve an integral
            
            // Solve by bisection
            // Ideally we would have a stack and be able to recurse into problem
            // areas.  What about amoeba to do this in a principled way?
            // Early out likely stalls the group until the worst case is done
            float t0 = 0.0;
            float t1 = 1.0;
            float2 p0 = erfc_approx(curve.a);
            float2 p1 = erfc_approx(curve.d);
            // Ironically, this hard limit will pixelate extreme zooms.
            for (int i = 0; i != 7; ++i) {
                float t2 = (t0 + t1) * 0.5;
                float2 p2 = erfc_approx(bezier_xy_for_t(t2, curve.a, curve.b, curve.c, curve.d));
                float2 dy0 = p2 - p0;
                float2 dy1 = p1 - p2;
                // Decide which interval to accept and which to recurse into
                // based on the area of the curve's bounding box.
                // Can the compiler implement the below branchfree?
                // When we greedily decide to subdivide, we are giving up any
                // chance to improve the other interval
                if (abs(dy0.x * dy0.y) < abs(dy1.x * dy1.y)) {
                    cumulant += dy0.y * (p0.x + p2.x);
                    p0 = p2;
                    t0 = t2;
                } else {
                    cumulant += dy1.y * (p2.x + p1.x);
                    p1 = p2;
                    t1 = t2;
                }
            }
            // Final section:
            cumulant += (p1.y - p0.y) * (p0.x + p1.x);

            

//            for (int i = 0; i != 9; ++i) {
//                points[i] = erfc_approx(bezier_xy_for_t(ts[i], curve.a, curve.b, curve.c, curve.d));
//            }
//            for (int i = 0; i != 8; ++i) {
//                cumulant += (points[i+1].y - points[i].y) * (points[i].x + points[i+1].x);
//            }
        }
                
        cumulant = clamp(cumulant * 0.125, -1.0, 1.0);

        result.color.r = 0.0;//C[0][0] * 0.001;
        result.color.g = 0.0;//C[1][1] * 0.001; //C[1][1] * 0.001;
        result.color.b = 1.0 - cumulant; //abs(C[1][0]) * 0.01;//0.0; //C[1][1] * 0.001;
        result.color.a = cumulant;
        return result;
    }
    
    
    [[fragment]] BezierFragmentOut
    bezierFragmentFunction(BezierFragmentIn input [[stage_in]],
                           const device otf::CubicBezier* bez [[buffer(0)]],
                           constant otf::BezierUniforms& uniforms [[buffer(3)]]) {
        
        // coordinate of the center of this pixel
        float2 coordinate = input.v.coordinate.xy;
        // page to screen transformation
        float4x4 A = uniforms.transformation;
        
        BezierFragmentOut result{};
        
        float cumulant = 0.0;
        
        float4 buffer[16][4];
        int index = 0;
        
        for (uint j = input.p.begin; j != input.p.end; ++j) {
            
            // Transform from page to screen
            float4 a = A * float4(bez[j].a + input.p.position, 0, 1);
            float4 b = A * float4(bez[j].b + input.p.position, 0, 1);
            float4 c = A * float4(bez[j].c + input.p.position, 0, 1);
            float4 d = A * float4(bez[j].d + input.p.position, 0, 1);
            // Translate from screen to pixel
            a.xy -= coordinate * a.w;
            b.xy -= coordinate * b.w;
            c.xy -= coordinate * c.w;
            d.xy -= coordinate * d.w;
//            // Scale according to pixel size
            a.xy /= uniforms.pixel_size * M_SQRT2_F;
            b.xy /= uniforms.pixel_size * M_SQRT2_F;
            c.xy /= uniforms.pixel_size * M_SQRT2_F;
            d.xy /= uniforms.pixel_size * M_SQRT2_F;
////            // We have a 4D bezier curve in homogeneous pixel coordinates

            
            // Now solve the integral
            
            // Specifically, we want the signed area between the curve and
            // x = +infinity
            
            // We can bound the error in the integral by bounding the region
            // the curve can occupy
            
            buffer[0][0] = a;
            buffer[0][1] = b;
            buffer[0][2] = c;
            buffer[0][3] = d;
            index = 1;
            
            while (index) {
                --index;
                a = buffer[index][0];
                b = buffer[index][1];
                c = buffer[index][2];
                d = buffer[index][3];
                float4 lo = min(min(a, b), min(c, d));
                float4 hi = max(max(a, b), max(c, d));
                float2 plo = erfc_approx(hi.xy / lo.w);
                float2 phi = erfc_approx(lo.xy / lo.w);
                float error = (phi.x - plo.x) * (phi.y - plo.y);
                if ((error <= 0.01)) {
                    // cumulant += (phi.y - plo.y) * (phi.x + plo.x) * sign(a.x - d.x);
                    float2 pa = erfc_approx(a.xy / a.w);
                    float2 pd = erfc_approx(d.xy / d.w);
                    cumulant += (pd.y - pa.y) * (pa.x + pd.x);
                    // cumulant += index * 0.1;
                } else if ((index >= 14)) {
                    // cumulant += error * 10;
                } else {
                    float4 ab = mix(a, b, 0.5);
                    float4 bc = mix(b, c, 0.5);
                    float4 cd = mix(c, d, 0.5);
                    float4 abbc = mix(ab, bc, 0.5);
                    float4 bccd = mix(bc, cd, 0.5);
                    float4 abbbcccd = mix(abbc, bccd, 0.5);
                    buffer[index][0] = a;
                    buffer[index][1] = ab;
                    buffer[index][2] = abbc;
                    buffer[index][3] = abbbcccd;
                    ++index;
                    buffer[index][0] = abbbcccd;
                    buffer[index][1] = bccd;
                    buffer[index][2] = cd;
                    buffer[index][3] = d;
                    ++index;
                }
                
                
            
                
                
                
                
                
//                
//               
//                // these are the control points of two new curves
//                // a, ab, abbc, abbbcccd
//                // abbbcccd, bccd, cd, d
//                float eleft, eright;
//                float cleft, cright;
//                {
//                    float4 lo = min(min(a, ab), min(abbc, abbbcccd));
//                    float4 hi = max(max(a, ab), max(abbc, abbbcccd));
//                    float2 plo = erfc_approx(hi.xy);
//                    float2 phi = erfc_approx(lo.xy);
//                    eleft = (phi.x - plo.x) * (phi.y - plo.y);
//                    cleft = (phi.y - plo.y) * (phi.x + plo.x) * sign(a.y - abbbcccd.y);
//                }
//                {
//                    float4 lo = min(min(abbbcccd, bccd), min(cd, d));
//                    float4 hi = max(max(abbbcccd, bccd), max(cd, d));
//                    float2 plo = erfc_approx(hi.xy);
//                    float2 phi = erfc_approx(lo.xy);
//                    eright = (phi.x - plo.x) * (phi.y - plo.y);
//                    cright = (phi.y - plo.y) * (phi.x + plo.x) * sign(abbbcccd.y - d.y);
//                }
//                if (eleft < eright) {
//                    cumulant += cleft;
//                    a = abbbcccd;
//                    b = bccd;
//                    c = cd;
//                } else {
//                    cumulant += cright;
//                    b = ab;
//                    c = abbc;
//                    d = abbbcccd;
//                }
//                
            }
//            float4 lo = min(min(a, b), min(c, d));
//            float4 hi = max(max(a, b), max(c, d));
//            float2 plo = erfc_approx(hi.xy);
//            float2 phi = erfc_approx(lo.xy);
//            cumulant += (phi.y - plo.y) * (phi.x + plo.x) * sign(a.y - d.y);
            continue;
            
//            // cumulant += a.y / a.w * 1; //(phi.y - plo.y) * (plo.x + phi.x);
//            result.color.x = abs(a.x) * 10;
//            result.color.y = abs(a.y) * 10;
//            result.color.z = 0.0; //a.z / a.w;
//            result.color.w = 1.0;
//            return result;

            
            
//            
//            
//            // Now we have to solve an integral
//            
//            // Solve by bisection
//            // Ideally we would have a stack and be able to recurse into problem
//            // areas.  What about amoeba to do this in a principled way?
//            // Early out likely stalls the group until the worst case is done
//            float t0 = 0.0;
//            float t1 = 1.0;
//            float2 p0 = erfc_approx(a.xy);
//            float2 p1 = erfc_approx(d.xy);
//            // Ironically, this hard limit will pixelate extreme zooms.
//            for (int i = 0; i != 7; ++i) {
//                float t2 = (t0 + t1) * 0.5;
//                float2 p2 = erfc_approx(bezier_xy_for_t(t2, a.xy, b.xy, c.xy, d.xy));
//                float2 dy0 = p2 - p0;
//                float2 dy1 = p1 - p2;
//                // Decide which interval to accept and which to recurse into
//                // based on the area of the curve's bounding box.
//                // Can the compiler implement the below branchfree?
//                // When we greedily decide to subdivide, we are giving up any
//                // chance to improve the other interval
//                if (abs(dy0.x * dy0.y) < abs(dy1.x * dy1.y)) {
//                    cumulant += dy0.y * (p0.x + p2.x);
//                    p0 = p2;
//                    t0 = t2;
//                } else {
//                    cumulant += dy1.y * (p2.x + p1.x);
//                    p1 = p2;
//                    t1 = t2;
//                }
//            }
//            // Final section:
//            cumulant += (p1.y - p0.y) * (p0.x + p1.x);
//            
            
            
            //            for (int i = 0; i != 9; ++i) {
            //                points[i] = erfc_approx(bezier_xy_for_t(ts[i], curve.a, curve.b, curve.c, curve.d));
            //            }
            //            for (int i = 0; i != 8; ++i) {
            //                cumulant += (points[i+1].y - points[i].y) * (points[i].x + points[i+1].x);
            //            }
        }
        
        cumulant = clamp(cumulant * 0.125, -1.0, 1.0);
        
        result.color.r = 0.0;//C[0][0] * 0.001;
        result.color.g = 0.0;//C[1][1] * 0.001; //C[1][1] * 0.001;
        result.color.b = 0.25 * (1 - cumulant); //abs(C[1][0]) * 0.01;//0.0; //C[1][1] * 0.001;
        result.color.a = cumulant;
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
