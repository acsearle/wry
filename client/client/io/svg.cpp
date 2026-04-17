//
//  svg.cpp
//  client
//
//  Created by Antony Searle on 8/3/2026.
//

#include "svg.hpp"

#include "match.hpp"
#include "parse.hpp"
#include "xml.hpp"
#include "test.hpp"

// SVG includes ellipse sections
//
// Ellipses cannot be exactly represented by Bezier curves, but they can be
// represented by rational Bezier curves, or equivalently, Bezier curves in
// homogeneous coordinates.  This complements the use of homogenous
// coordinates to implement perspective transformations.

namespace wry::svg {
    
    namespace {

        void put(std::vector<CubicBezier>& a,
                 double x, double y,
                 double x1, double y1,
                 double x2, double y2,
                 double x3, double y3) {
            CubicBezier b;
            b.control_points.columns[0] = make_float4(x, y, 0.0, 1.0);
            b.control_points.columns[1] = make_float4(x1, y1, 0.0, 1.0);
            b.control_points.columns[2] = make_float4(x2, y2, 0.0, 1.0);
            b.control_points.columns[3] = make_float4(x3, y3, 0.0, 1.0);
            a.push_back(b);
        }
        
        void put(std::vector<CubicBezier>& a,
                 double x, double y,
                 double x1, double y1,
                 double x2, double y2) {
            put(a,
                x, y,
                mix(x, x1, 2.0 / 3.0),
                mix(y, y1, 2.0 / 3.0),
                mix(x1, x2, 1.0 / 3.0),
                mix(y1, y2, 1.0 / 3.0),
                x2, y2);
        }
        
        void put(std::vector<CubicBezier>& a, double x, double y, double x1, double y1) {
            put(a,
                x, y,
                mix(x, x1, 1.0 / 2.0),
                mix(y, y1, 1.0 / 2.0),
                x1, y1);
        }
        
        bool parse_path_d(auto v) {
            
            std::vector<CubicBezier> a;
                        
            auto get = [&v](auto&&... xs) {
                return match_and(parse_number_relaxed(xs)...)(v);
            };
            
            double x = 0.0, y = 0.0;
            double x0 = 0.0, y0 = 0.0;
            double x1 = 0.0, y1 = 0.0;
            double x2 = 0.0, y2 = 0.0;
            double x3 = 0.0, y3 = 0.0;
            for (;;) {
                match_spaces()(v);
                if (v.empty())
                    break;
                auto ch = v.front();
                v.pop_front();
                switch (ch) {
                    case 'M':
                        get(x, y);
                        x0 = x1 = x;
                        y0 = y1 = y;
                        [[fallthrough]];
                    case 'L':
                        while (get(x1, y1)) {
                            put(a, x, y, x1, y1);
                            x = x1;
                            y = y1;
                        }
                        x2 = x;
                        y2 = y;
                        break;
                    case 'm':
                        get(x1, y1);
                        x1 = x += x1;
                        y1 = y += y1;
                        [[fallthrough]];
                    case 'l':
                        while (get(x1, y1)) {
                            x1 += x;
                            y1 += y;
                            put(a, x, y, x1, y1);
                            x = x1;
                            y = y1;
                        }
                        x2 = x;
                        y2 = x;
                        break;
                    case 'Z':
                    case 'z':
                        put(a, x, y, x0, y0);
                        x = x1 = x2 = x0;
                        y = y1 = y2 = y0;
                        break;
                    case 'H':
                        while (get(x1)) {
                            put(a, x, y, x1, y);
                            x = x1;
                        }
                        x2 = x;
                        break;
                    case 'h':
                        while (get(x1)) {
                            x1 += x;
                            put(a, x, y, x1, y);
                            x = x1;
                        }
                        x2 = x;
                        break;
                    case 'V':
                        while (get(y1)) {
                            put(a, x, y, x, y1);
                            y = y1;
                        }
                        y2 = y;
                        break;
                    case 'v':
                        while (get(y1)) {
                            y1 += y;
                            put(a, x, y, x, y1);
                            y = y1;
                        }
                        y2 = y;
                        break;
                    case 'Q':
                        while (get(x1, y1, x2, y2)) {
                            put(a, x, y, x1, y1, x2, y2);
                            x = x2;
                            y = y2;
                        }
                        x1 = 2 * x - x1;
                        y1 = 2 * y - y1;
                        break;
                    case 'q':
                        while (get(x1, y1, x2, y2)) {
                            x1 += x;
                            y1 += y;
                            x2 += x;
                            y2 += y;
                            put(a, x, y, x1, y1, x2, y2);
                            x = x2;
                            y = y2;
                        }
                        x1 = 2 * x2 - x1;
                        y1 = 2 * y2 - y1;
                        break;
                    case 'T':
                        while (get(x2, y2)) {
                            put(a, x, y, x1, y1, x2, y2);
                            x = x2;
                            y = y2;
                            x1 = 2 * x2 - x1;
                            y1 = 2 * y2 - y1;
                        }
                        break;
                    case 't':
                        while (get(x2, y2)) {
                            x2 += x;
                            y2 += x;
                            put(a, x, y, x1, y1, x2, y2);
                            x = x2;
                            y = y2;
                            x1 = 2 * x2 - x1;
                            y1 = 2 * y2 - y1;
                        }
                        x2 = x;
                        y2 = y;
                        break;
                    case 'C':
                        while (get(x1, y1, x2, y2, x3, y3)) {
                            put(a, x, y, x1, y1, x2, y2, x3, y3);
                            x = x3;
                            y = y3;
                        }
                        x1 = 2 * x3 - x2;
                        y1 = 2 * y3 - y2;
                        break;
                    case 'c':
                        while (get(x1, y1, x2, y2, x3, y3)) {
                            x1 += x;
                            y1 += y;
                            x2 += x;
                            y2 += y;
                            x3 += x;
                            y3 += y;
                            put(a, x, y, x1, y1, x2, y2, x3, y3);
                            x = x3;
                            y = y3;
                        }
                        x1 = 2 * x3 - x2;
                        y1 = 2 * y3 - y2;
                        break;
                    case 'S':
                        while (get(x2, y2, x3, y3)) {
                            put(a, x, y, x1, y1, x2, y2, x3, y3);
                            x = x3;
                            y = y3;
                            x1 = 2 * x3 - x2;
                            y1 = 2 * y3 - y2;
                        }
                        break;
                    case 's':
                        while (get(x2, y2, x3, y3)) {
                            x2 += x;
                            y2 += y;
                            x3 += x;
                            y3 += y;
                            put(a, x, y, x1, y1, x2, y2, x3, y3);
                            x = x3;
                            y = y3;
                            x1 = 2 * x3 - x2;
                            y1 = 2 * y3 - y2;
                        }
                        break;
                    case 'A': {
                        double rx = 0.0; // radius (x)
                        double ry = 0.0; // radius (y)
                        double angle = 0.0; // degrees
                        double large_arc_flag = 0.0; // small, large
                        double sweep_flag = 0.0; // counterclockwise, clockwise
                        while (get(rx, ry, angle, large_arc_flag, sweep_flag, x2, y2)) {
                            angle *= (M_PI / 180.0);
                            double c = cos(angle);
                            double s = sin(angle);
                            // transform a unit to the specified radii and angle
                            double2x2 A = simd_matrix(simd_make_double2(rx*c,rx*s),
                                                     simd_make_double2(-ry*s,rx*c));
                            // inverse of transform
                            double2x2 B = simd_inverse(A);
                            // transform of endpoints
                            double2 a_ = simd_mul(B, simd_make_double2(x, y));
                            double2 b = simd_mul(B, simd_make_double2(x2, y2));
                            // translate the endpoints so that they lie on the
                            // unit circle
                            double d = simd_distance_squared(a_, b);
                            assert(d <= 4.0); // else the radii are too small
                            double e = sqrt(1.0 - 0.25 * d);
                            double2 g = b - a_;
                            // TODO: Use sweep to set the sign of h
                            double2 h = simd_normalize(simd_make_double2(-g.y, g.x));
                            g -= h * e;
                            a_ -= g;
                            b -= g;
                            assert(abs(simd_length(a_) - 1.0) < 1e-3);
                            assert(abs(simd_length(b) - 1.0) < 1e-3);
                            // We now have two points on the unit circle
                            // Construct the midpoint
                            // TODO: Use small/large arc flag to choose p
                            double2 p = (large_arc_flag == 0) ? (a_ + b) * 0.5 : -h;
                            // Construct the midpoint of the arc
                            double2 q = simd_normalize(p);
                            // Q can be the basis of a quadratic approximation
                            // to the arc, but we can do better.
                            
                            // Use w to rescale the point so that the circular
                            // conic section becomes a parabola in 4d, while
                            // remaining the same in projected space (x/w, y/w)
                            double w1 = 1.0 - 0.5 * simd_distance(p, q);
                            q *= w1;
                            // The parabola passes through this point; we
                            // instead need the quadratic Bezier curve control
                            // point
                            q = (q - p) * 2.0;
                            w1 = (1.0 - w1) * 2.0;
                            // For large arcs, this puts the control point
                            // into negative w, but the actual curve won't
                            // cross the w=0 plane.  It does make the 2d bound
                            // tricky, and may argue for splitting "long" arcs
                            // into two "short" sections.  The parameterization
                            // for "long" arcs can highly nonlinear which might
                            // interact badly with the integrator
                            
                            // Undo translation, accounting for w
                            q += g * w1;
                            // Undo transformation
                            q = simd_mul(A, q);
                            x1 = q.x;
                            y1 = q.y;
                            // We now have the control points of a quadratic
                            // Bezier curve in 4 dimensions that projects down
                            // to an ellipse segment in 2 dimensions:
                            //
                            //   (x , y , 0, 1),
                            //   (x1, y1, 0, w),
                            //   (x2, y2, 0, 1)
                            
                            put(a, x, y, x1, y1, x2, y2);
                            x = x2;
                            y = y2;
                        }
                        x1 = x;
                        y1 = y;
                    } break;
                    default:
                        printf("did not understand %c\n", ch);
                        return false;
                }
            }
            return true;
        }
        
        void parse_path(xml::Content& a) {
            assert(a.name == "path");
            for (auto [k, v] : a.attributes) {
                if (k == "d") {
                    parse_path_d(v);
                } else {
                    printf("ignored ");
                    print(k);
                    printf("=\"");
                    print(v);
                    printf("\"\n");
                }
            }
        }
        
        
        
        
        void parse_fragment(xml::Content& a) {
            assert(a.name == "svg");
            for (auto [k, v] : a.attributes) {
                //                if (k == "height") {
                //                } else if (k == "width") {
                //                } else if (k == "viewBox") {
                //                } else if (k == "x") {
                //                } else if (k == "y") {
                //                } else {
                {
                    printf("ignored ");
                    print(k);
                    printf("=\"");
                    print(v);
                    printf("\"\n");
                }
            }
            for (xml::Content& b : a.content) {
                switch(b.tag) {
                    case xml::Content::TEXT:
                        break;
                    case xml::Content::ELEMENT:
                        if (b.name == "path") {
                            parse_path(b);
                        }
                }
            }
        }
        
    }
    
    std::vector<PiecewiseCurve> parse(StringView& v) {
        std::vector<PiecewiseCurve> result;
        
        auto a{xml::parse(v)};
        
        for (xml::Content& b : a) {
            switch (b.tag) {
                case xml::Content::TEXT:
                    break;
                case xml::Content::ELEMENT:
                    if (b.name == "svg") {
                        parse_fragment(b);
                    }
            }
        }
        
        return result;

    }
    
    namespace {
        
        define_test("svg") {
            char const* input_circle_svg = R"(<svg xmlns="http://www.w3.org/2000/svg" height="24px" viewBox="0 -960 960 960" width="24px" fill="#1f1f1f"><path d="M480-640 280-440l56 56 104-103v407h80v-407l104 103 56-56-200-200ZM146-260q-32-49-49-105T80-480q0-83 31.5-156T197-763q54-54 127-85.5T480-880q83 0 156 31.5T763-763q54 54 85.5 127T880-480q0 59-17 115t-49 105l-58-58q22-37 33-78t11-84q0-134-93-227t-227-93q-134 0-227 93t-93 227q0 43 11 84t33 78l-58 58Z"/></svg>)";
            
            StringView v{input_circle_svg};

            parse(v);
            
            co_return;
        };
        
        
    }
    
} // namespace wry
