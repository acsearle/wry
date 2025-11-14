//
//  simd.cpp
//  client
//
//  Created by Antony Searle on 30/8/2023.
//

#include "simd.hpp"

#include "test.hpp"

namespace wry {
    
    
    inline simd_float4 interpolate_wheeled_vehicle(simd_float2 x0, simd_float2 y0,
                                                   simd_float2 x1, simd_float2 y1,
                                                   float t) {
        
        y0 = simd_normalize(y0);
        y1 = simd_normalize(y1);

        if (t <= 0.0f)
            return make<float4>(x0, -y0.y, y0.x);

        if (t >= 1.0f)
            return make<float4>(x1, -y1.y, y1.x);

        
        // interpolate trajectory from x0 to y1
        // y0 and y1 are the vectors from left to right along the fixed axle,
        // which are perpendicular to the heading
                
        // compose the trajectory from two arcs of the circles
        //
        // (x0 + r0 y0, |r0|)
        // (x1 + r1 y1, |r1|)
        //
        // This problem is underconstrained; make |r0| = |r1| so that the
        // two arcs have the same radius, and consider the four cases of
        // +/- y0, +/- y1 to handle the cases
        
        // ||(x0 + r*a0) - (x1 + r*a1|| = 2*r
        // ||x + r * y|| = 2*r
        // ||x|| + r*r*||y|| + 2*r*x.y = 4*r*r
        // solve quadratic
        
        simd_float2 x = x1 - x0;
        simd_float2 ys[] = {
            -y1 - y0,
            //             y1 + y0,
              //          -y1 - y0,
                //        -y1 + y0
        };
        
        float r = 0.01f;
        
        for (simd_float2 y : ys) {
            float a = simd_length_squared(y) - 4.0f;
            // printf("a : %g\n", a);
            if (a == 0.0f) {
                // continue;
                return make<float4>(x0, -y0.y, y0.x);
            }

            float b = 2.0f * simd_dot(x, y);
            float c = simd_length_squared(x);
            float d2 = b * b - 4.0f * a * c;
            if (d2 < 0.0f)
                // continue;
                return make<float4>(x0, -y0.y, y0.x);

            float d = sqrt(d2);
            float r0 = (- b - d) / (2.0f * a);
            float r1 = (- b + d) / (2.0f * a);
            // printf("r0 : %g, r1 : %g\n", r0, r1);
            // these two options seem to correspond to a smooth path, and a
            // one-point turn
            //if (abs(r0) < abs(r1))
            //    r = abs(r0);
            r = r0;
            break;
        }
        
        // now find the changeover point
        simd_float2 z0 = x0 + r * y0;
        simd_float2 z1 = x1 - r * y1;
        simd_float2 mid = (z0 + z1) / 2.0f;
        // printf("distance(z0, z1) : %g vs %g\n", simd_distance(z0, z1), 2.0f * r);
        
        // find the angles traversed in each arc
        // from x0 to mid around z0
        float w0 = acos(simd_dot(mid - z0, x0 - z0) / (r * r));
        float w1 = acos(simd_dot(x1 - z1, mid - z1) / (r * r));
        
        // printf("w : %g %g\n", w0, w1);
        
        float4 xdx;
        
        t = t * (w0 + w1);
        if (t < w0) {
            x = x0;
            x -= z0;
            float c = cos(t);
            float s = sin(t);
            xdx = simd_float4{
                x.x * c + x.y * s + z0.x,
                x.x *-s + x.y * c + z0.y,
                x.x *-s + x.y * c,
                x.x *-c + x.y *-s
            };
        } else {
            x = x1;
            x -= z1;
            float c = cos(w0 + w1 - t);
            float s = sin(w0 + w1 - t);
            xdx = simd_float4{
                x.x * c + x.y * s + z1.x,
                x.x *-s + x.y * c + z1.y,
                x.x *+s + x.y *-c,
                x.x *+c + x.y *+s
            };
        }
        
        // printf("xdx : {%g %g %g %g }\n", xdx.x, xdx.y, xdx.z, xdx.w);

        return xdx;
    }
    
    void interpolate_wheeled_vehicle2() {
        
        // the two arc path above places no limit on acceleration, nor does it
        // allow higher order continuity (jerk).  (but it does have the excellent
        // property of deciding to use two point turns)
        
        // vehicle has
        //
        // location x(t), y(t)
        // velocity x'(t), y'(t)
        //
        // with alternative form of velocity in terms of speed and heading
        //     s(t), w(t)
        //
        // we can accelerate and brake the vehicle along the velocity vector
        
        // we can accelerate the vehicle perpendicular to the velocity vector
        // only in proportion to the velocity vector; in particular we cannot
        // accelerate sideways at a standstill
        
        // Our steering input is dw/ds, so the gain increases
        
        // Another way of looking at the problem is to regard the truck as
        // an extended object.  Place the origin at the centre of the rear axle.
        // Then find the trajectory that minimizes the acceleration of
        // the cab, which is (x + tangent)"?
        
        // If we have x0, w0 and x1, w1, perhaps the path we want is the one
        // that minimizes the curvature?
        
        // x
        // tangent is dx ds
        // is ds dt is |x'|dt
        //
        // (x + x' / sqrt(x'^2 + y'^2))"
        //
        // note that this reflects how the cab experiences accelerations
        // perpendicular to the direction of motion in proportion to velocity
        
        // this is inevitably going to be numerically intractable, therefore
        // just smootherstep it?
        
        // to work near zero we need to further constrain the acceleration
        // direction at t=0 and t=1
        
        // what about an iterative chasing?
        
        
        
    }
    
    define_test("interpolate") {
        simd_float2 x0 = {0.0f, 0.0f};
        simd_float2 x1 = {1.0f, 0.0f};

        simd_float2 dx0 = {0.0f, 1.0f};
        simd_float2 dx1 = {1.0f, 0.0f};


        interpolate_wheeled_vehicle(x0, make<float2>(dx0.y, -dx0.x), x1, make<float2>(dx1.y, -dx1.x), 0.5f);
        co_return;
    };
    
}

