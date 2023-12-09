//
//  sdf.hpp
//  client
//
//  Created by Antony Searle on 22/10/2023.
//

#ifndef sdf_hpp
#define sdf_hpp

#include "image.hpp"

namespace wry {
    
    // signed distance field generators and transformers
    
    namespace sdf {
        
        struct Value {
            u8 inner;
            float read() const {
                return (inner - 128.0f) / 64.0f;
            }
            void write(float x) {
                float y = (x * 64.0f) + 128.0f;
                if (y < 0)
                    inner = 0;
                else if (y > 255.0f)
                    inner = 255;
                else
                    inner = intRTNE(y);
            }
        };
        
        // signed distance field combinators
        
        inline auto disk(float radius) {
            assert(radius > 0);
            return [=](float2 xy) -> float {
                return radius - simd_length(xy);
            };
        };

        inline auto rectangle(float2 wh) {
            assert(wh.x > 0 && wh.y > 0);
            return [=](float2 xy) -> float {
                return simd_reduce_min(wh - simd_abs(xy));
            };
        }
        
        inline auto capsule(float2 wh) {
            assert(wh.x > 0 && wh.y > 0);
            return [=](float2 xy) -> float {
                xy.x = abs(xy.x) - wh.x;
                if (xy.x > 0.0f) {
                    return wh.y - simd_length(xy);
                } else {
                    return wh.y - abs(xy.y);
                }
            };
        }
        
        inline auto circle(float radius, float thickness) {
            assert((radius > 0) && (thickness > 0));
            return [=](float2 xy) -> float {
                return thickness - abs(radius - simd_length(xy));
            };
        }
        
        inline auto arc(float radius, float thickness, float theta0, float theta1) {
            float2 xy0 = simd_make_float2(cos(theta0), sin(theta0)) * radius;
            float2 xy1 = simd_make_float2(cos(theta1), sin(theta1)) * radius;
            while (theta0 < 0)
                theta0 += 2 * M_PI;
            while (theta1 < theta0)
                theta1 += 2 * M_PI;
            assert(0 <= theta0);
            assert(theta0 <= theta1);
            assert(theta0 <= 2 * M_PI);
            assert(theta1 <= theta0 + 2 * M_PI);
            return [=](float2 xy) -> float {
                float theta = atan2(xy.y, xy.x);
                while (theta < theta0)
                    theta += 2 * M_PI;
                if (theta < theta1) {
                    return thickness - abs(radius - simd_length(xy));
                } else if ((theta0 + 2 * M_PI - theta) <= (theta - theta1)) {
                    return thickness - simd_length(xy - xy0);
                } else {
                    return thickness - simd_length(xy - xy1);
                }
            };
        }
        
        // closest point to a spiral is tricky
        
        // in fact, closest point to any sort of distorted thing is tricky, but
        // is that a deal-breaker?
        
        // can we permit distortions and just warp a linear arrow?
        // radial unaffected;
        // off-radial only slightly affected
        
        inline auto spiral(float radius, float dradiusdtheta, float thickness, float theta0, float theta1) {
            float2 xy0 = simd_make_float2(cos(theta0), sin(theta0)) * radius;
            float2 xy1 = simd_make_float2(cos(theta1), sin(theta1)) * (radius + dradiusdtheta * (theta1 - theta0));
            while (theta0 < 0)
                theta0 += 2 * M_PI;
            while (theta1 < theta0)
                theta1 += 2 * M_PI;
            assert(0 <= theta0);
            assert(theta0 <= theta1);
            assert(theta0 <= 2 * M_PI);
            assert(theta1 <= theta0 + 2 * M_PI);
            return [=](float2 xy) -> float {
                float theta = atan2(xy.y, xy.x);
                while (theta < theta0)
                    theta += 2 * M_PI;
                if (theta < theta1) {
                    return thickness - abs(radius + (theta - theta0) * dradiusdtheta - simd_length(xy));
                } else if ((theta0 + 2 * M_PI - theta) <= (theta - theta1)) {
                    return thickness - simd_length(xy - xy0);
                } else {
                    return thickness - simd_length(xy - xy1);
                }
            };
        }
        
        inline auto transform(simd_float3x2 A) {
            simd_float2x2 B = simd_matrix(A.columns[0], A.columns[1]);
            assert(abs(simd_determinant(B) - 1.0f) < 1e-3);
            return [A](auto&& f) {
                return [A,f=std::forward<decltype(f)>(f)](float2 xy) {
                    return f(simd_mul(A, simd_make_float3(xy, 1.0f)));
                };
            };
        }

        inline auto transform(simd_float3x3 A) {
            assert(abs(simd_determinant(A) - 1.0f) < 1e-3);
            return [A](auto&& f) {
                return [A,f=std::forward<decltype(f)>(f)](float2 xy) {
                    return f(simd_mul(A, simd_make_float3(xy, 1.0f)).xy);
                };
            };
        }

        inline auto line(float2 a,
                         float2 b,
                         float t) {
            float2 wh = simd_make_float2(simd_distance(a, b) * 0.5, t);
            
            float2 s = (a + b) * 0.5f;
            float2 cs = simd_normalize(b - a);
            simd_float3x3 A = simd_matrix(simd_make_float3(cs.x, -cs.y, 0),
                                          simd_make_float3(cs.y, cs.x, 0),
                                          simd_make_float3(0, 0, 1));
            simd_float3x3 B = simd_matrix(simd_make_float3(1, 0, 0),
                                          simd_make_float3(0, 1, 0),
                                          simd_make_float3(-s, 1));
            return transform(simd_mul(A,B))(capsule(wh));
        }
        
        inline auto rotate(float theta) {
            float c = cos(theta);
            float s = sin(theta);
            simd_float2x2 A = simd_matrix(simd_make_float2(c, s),
                                          simd_make_float2(-s, c));
            return [A](auto&& f) {
                return [A, f=std::forward<decltype(f)>(f)](float2 xy) {
                    return f(simd_mul(A, xy));
                };
            };
        }
        
        inline auto translate(float2 xy) {
            return [xy](auto&& f) {
                return [xy, f=FWD(f)](float2 xy_) {
                    return f(xy_ + xy);
                };
            };
        }
        
        inline auto scale(float s) {
            assert(s > 0);
            return [s](auto&& f) {
                return [s, f=std::forward<decltype(f)>(f)](float2 xy) {
                    return s * f(xy / s);
                };
            };
        }
        
        inline auto polar(auto&& f) {
            return [f=std::forward<decltype(f)>(f)](float2 xy) {
                float r = simd_length(xy);
                float theta = atan2(xy.y, xy.x);
                if (theta < 0)
                    theta += 2 * M_PI;
                return f(simd_make_float2(r, theta));
            };
        }
        
        inline auto and_(auto&&... f) {
            return [...f=std::forward<decltype(f)>(f)](float2 xy) {
                return min(f(xy)...);
            };
        }

        inline auto or_(auto&&... f) {
            return [...f=std::forward<decltype(f)>(f)](float2 xy) {
                return max(f(xy)...);
            };
        }

        inline auto negate_(auto&& f) {
            return [f=FWD(f)](float2 xy) {
                return -xy;
            };
        }
        
        inline auto arrow(float2 a, float2 b, float t) {
            float2 c = simd_normalize(a - b) * (t * 3);
            float2 d = b + c * 2;
            c = simd_make_float2(c.y, -c.x);
            float2 f = d + c;
            float2 g = d - c;
            return or_(line(a, d, t),
                       line(b, f, t),
                       line(b, g, t));
        }
        
        inline void render(auto&& f, matrix_view<float> v) {
            for (difference_type i = 0; i != v.minor(); ++i) {
                float x = (i + 0.5f) / v.minor() - 0.5f;
                for (difference_type j = 0; j != v.major(); ++j) {
                    float y = (j + 0.5f) / v.major() - 0.5f;
                    float d = f(simd_make_float2(x, y));
                    v[i, j] = d * 4.0f + 0.5f;
                    // v[i, j] = d * 64.0f + 0.5f;
                }
            }
        }
        
        inline void render_arrow(matrix_view<float> v) {
            auto r = sqrt(2.0) / 4.0f;
            auto t = 1.0 / 32.0f;
            auto f = arrow(simd_make_float2(0.0f, -r),
                           simd_make_float2(0.0f, +r),
                           t);
            render(std::move(f), v);
        }
        
        inline void render_octagon(matrix_view<float> v) {
            auto r = sqrt(2.0) / 4.0f;
            auto f = and_(rectangle(simd_make_float2(r,r)),
                          rotate(M_PI/4)(rectangle(simd_make_float2(r,r))));
            render(std::move(f), v);
        }
        
    };
    
    
    
    //float2 x1 = cs(theta1);
    // float theta2 = theta1 - t * 6.0f / r;
    //float theta2 = theta0 + t * 6.0f / r;
    //float2 x2 = cs(theta2);
    // make iso triangle
    //float2 x3 = simd_make_float2(x1.y - x2.y, x2.x - x1.x) / 2.0f;
    // make back radial
    // float2 x3 = cs(theta2) / r * t * 3.0f;
    // make whole thing spiral?
    
    //auto f = or_(arc(r, t, theta0, theta1),
    /*line(simd_make_float2(0.25f, 0.25f),
     simd_make_float2(
     0.25f*2/3 + (0.5f-r)*1/3
     , r), t),
     line(simd_make_float2(0.25f, 0.25f),
     simd_make_float2(0.5f-r, 0.25f), t)*/
    // line(x1, x2 + x3, t),
    // line(x1, x2 - x3, t)
    //             spiral(r, r * 0.5f, t, theta0, theta2),
    //             spiral(r, r * -0.5f, t, theta0, theta2)
    //            );
    
    /*
     float theta0 = M_PI / 4.0;
     float theta1 = M_PI * 7.0 / 4.0;
     float r = sqrt(2.0f) / 4.0f;
     float t = 2.0 / 64.0f;
     
     auto cs = [=](float theta) {
     return r * simd_make_float2(cos(theta), sin(theta));
     };
     
     float r2 = 1.0f;
     float t2 = t * r2 / r;
     float theta2 = theta0 + t2 * 6.0f / r2;
     
     
     auto f = scale(r/r2)(polar(or_(line(simd_make_float2(r2, theta0),
     simd_make_float2(r2, theta1),
     t2),
     line(simd_make_float2(r2, theta0),
     simd_make_float2(r2+3*t2, theta2),
     t2),
     line(simd_make_float2(r2, theta0),
     simd_make_float2(r2-3*t2, theta2),
     t2))));
     for (difference_type i = 0; i != v.minor(); ++i) {
     float x = (i + 0.5f) / v.minor() - 0.5f;
     for (difference_type j = 0; j != v.major(); ++j) {
     float y = (j + 0.5f) / v.major() - 0.5f;
     float d = f(simd_make_float2(x, y));
     v[i, j] = d * 4.0f + 0.5f;
     // v[i, j] = d * 64.0f;
     }
     }
     */
    
} // namespace wry

#endif /* sdf_hpp */
