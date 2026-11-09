//
//  bezier.cpp
//  client
//
//  Created by Antony Searle on 19/3/2026.
//

#include <cmath>

#include "bezier.hpp"
#include "test.hpp"

// Reference:
// https://pomax.github.io/bezierinfo

// Tools for Bezier curves

// Some of these will also be implemented in MSL

namespace wry {

    // Moved here from font.mm so that the SVG loader shares it.  The body
    // is unchanged except that each input curve is subdivided on its own
    // stack, rather than all curves being pushed up front (which dropped
    // every curve past the 128th).
    float bezier4_coverage(std::span<bezier4 const> curves,
                           simd_float2 pixel_center,
                           float pixel_size)
    {
        // TODO: This function is structured to avoid recursion and diverging
        // control flow on the GPU.  It could be written more isiomatically
        // on the CPU with recursion.

        // A curve subdivides ~6 levels in the worst case, holding at most
        // one pending sibling per level, so 64 is comfortable.  If we
        // overflow we drop the tail rather than crash; the coverage error is
        // bounded by the error-threshold on the dropped curves.
        constexpr int STACK_SIZE = 64;
        simd_float2 stack[STACK_SIZE][4];

        // Convert to "erf coordinates": 1 unit == sqrt(2) pixels == one sigma
        // of the unit-pixel Gaussian.  Same scale convention as the shader's
        // `pixel_size * M_SQRT2_F` divisor.
        // NOTE: Extra factor of two from somewhere is needed
        float scale = 1.0f / (pixel_size * (float)M_SQRT2) * 2.0;

        float cumulant = 0.0f;
        for (auto const& curve : curves) {
            int sp = 0;
            stack[sp][0] = (curve.columns[0] - pixel_center) * scale;
            stack[sp][1] = (curve.columns[1] - pixel_center) * scale;
            stack[sp][2] = (curve.columns[2] - pixel_center) * scale;
            stack[sp][3] = (curve.columns[3] - pixel_center) * scale;
            ++sp;
            while (sp) {
                --sp;
                simd_float2 a = stack[sp][0];
                simd_float2 b = stack[sp][1];
                simd_float2 c = stack[sp][2];
                simd_float2 d = stack[sp][3];

                // Control-point bbox: the curve stays inside this convex
                // hull.  Map bbox corners to erf coordinates so the error
                // estimate is in the integrator's natural units.
                simd_float2 lo = simd_min(simd_min(a, b), simd_min(c, d));
                simd_float2 hi = simd_max(simd_max(a, b), simd_max(c, d));
                simd_float2 plo = erf(lo);
                simd_float2 phi = erf(hi);
                float error = (phi.x - plo.x) * (phi.y - plo.y);

                if (error <= 1.0f / 64.0f) {
                    // Trapezoid rule between the two endpoints in erf
                    // coords.
                    simd_float2 pa = erf(a);
                    simd_float2 pd = erf(d);
                    cumulant += (pd.y - pa.y) * (pa.x + pd.x);
                } else if (sp + 2 <= STACK_SIZE) {
                    // de Casteljau subdivision at t=0.5: left half, right
                    // half.
                    simd_float2 ab = simd_mix(a, b, 0.5f);
                    simd_float2 bc = simd_mix(b, c, 0.5f);
                    simd_float2 cd = simd_mix(c, d, 0.5f);
                    simd_float2 abbc = simd_mix(ab, bc, 0.5f);
                    simd_float2 bccd = simd_mix(bc, cd, 0.5f);
                    simd_float2 abbbcccd = simd_mix(abbc, bccd, 0.5f);
                    stack[sp][0] = a;
                    stack[sp][1] = ab;
                    stack[sp][2] = abbc;
                    stack[sp][3] = abbbcccd;
                    ++sp;
                    stack[sp][0] = abbbcccd;
                    stack[sp][1] = bccd;
                    stack[sp][2] = cd;
                    stack[sp][3] = d;
                    ++sp;
                }
                // else stack full: drop this curve's contribution.
            }
        }

        // Factor of 0.125 matches the shader: erf range is [-1, 1] (width 2),
        // and pa.x + pd.x is twice the trapezoid mean.
        return cumulant * 0.125f;
    }

    CoverageBitmap rasterize_coverage(std::span<bezier4 const> curves,
                                      float pixel_size)
    {
        if (curves.empty())
            return CoverageBitmap{matrix<R8Unorm>{}, 0, 0};

        // Control-point bbox.
        simd_float2 lo = simd_float2{ INFINITY,  INFINITY};
        simd_float2 hi = simd_float2{-INFINITY, -INFINITY};
        for (auto const& curve : curves) {
            for (int i = 0; i != 4; ++i) {
                lo = simd_min(lo, curve.columns[i]);
                hi = simd_max(hi, curve.columns[i]);
            }
        }

        // Pixel-space bbox with Gaussian-spillover padding.  3 sigma
        // captures >99.7% of the Gaussian mass; beyond that the falloff is
        // invisible at 8-bit precision.
        constexpr int padding = 3;
        int x_min_px = (int)std::floor(lo.x / pixel_size) - padding;
        int x_max_px = (int)std::ceil (hi.x / pixel_size) + padding;
        int y_min_px = (int)std::floor(lo.y / pixel_size) - padding;
        int y_max_px = (int)std::ceil (hi.y / pixel_size) + padding;

        int width  = x_max_px - x_min_px;
        int height = y_max_px - y_min_px;

        matrix<R8Unorm> bitmap((size_t)height, (size_t)width);

        // y is up; bitmap rows go top-to-bottom (row 0 = top).
        for (int row = 0; row != height; ++row) {
            float y = (y_max_px - row - 0.5f) * pixel_size;
            for (int col = 0; col != width; ++col) {
                float x = (x_min_px + col + 0.5f) * pixel_size;
                float cov = bezier4_coverage(curves,
                                             simd_float2{x, y},
                                             pixel_size);
                // R8Unorm::operator=(float) clamps [0,1] internally.
                bitmap[row, col] = cov;
            }
        }

        return CoverageBitmap{std::move(bitmap), x_min_px, y_max_px};
    }

    double bezier4_signed_area(std::span<bezier4 const> curves) {
        // x(t) y'(t) is a polynomial of degree 5, so three-point
        // Gauss-Legendre quadrature on [0, 1] is exact.
        constexpr double r = 0.7745966692414834; // sqrt(3/5)
        constexpr double ts[3] = {0.5 - 0.5 * r, 0.5, 0.5 + 0.5 * r};
        constexpr double ws[3] = {5.0 / 18.0, 8.0 / 18.0, 5.0 / 18.0};
        double area = 0.0;
        for (auto const& c : curves) {
            simd_double2 p0 = simd_double(c.columns[0]);
            simd_double2 p1 = simd_double(c.columns[1]);
            simd_double2 p2 = simd_double(c.columns[2]);
            simd_double2 p3 = simd_double(c.columns[3]);
            for (int i = 0; i != 3; ++i) {
                double t = ts[i], s = 1.0 - t;
                simd_double2 p = s*s*s*p0 + 3*s*s*t*p1 + 3*s*t*t*p2 + t*t*t*p3;
                simd_double2 dp = 3*s*s*(p1 - p0) + 6*s*t*(p2 - p1) + 3*t*t*(p3 - p2);
                area += ws[i] * p.x * dp.y;
            }
        }
        return area;
    }

    namespace {

        // The unit square, counterclockwise, as four straight cubics.
        std::vector<bezier4> square(float x0, float y0, float x1, float y1) {
            return {
                make_bezier4(simd_float2x2{simd_float2{x0, y0}, simd_float2{x1, y0}}),
                make_bezier4(simd_float2x2{simd_float2{x1, y0}, simd_float2{x1, y1}}),
                make_bezier4(simd_float2x2{simd_float2{x1, y1}, simd_float2{x0, y1}}),
                make_bezier4(simd_float2x2{simd_float2{x0, y1}, simd_float2{x0, y0}}),
            };
        }

        define_test("bezier") {
            // Signed area: CCW positive, CW negative.
            {
                auto s = square(1.0f, 2.0f, 4.0f, 6.0f);
                require(std::abs(bezier4_signed_area(s) - 12.0) < 1e-9);
                for (auto& c : s) {
                    std::swap(c.columns[0], c.columns[3]);
                    std::swap(c.columns[1], c.columns[2]);
                }
                require(std::abs(bezier4_signed_area(s) + 12.0) < 1e-9);
            }

            // Coverage deep inside is one, far outside is zero, and the
            // prefilter preserves the integral: the bitmap sums to the area.
            {
                auto s = square(0.0f, 0.0f, 10.0f, 10.0f);
                require(std::abs(bezier4_coverage(s, simd_float2{5, 5}, 1.0f) - 1.0f) < 1e-3f);
                require(std::abs(bezier4_coverage(s, simd_float2{20, 5}, 1.0f)) < 1e-3f);
                auto b = rasterize_coverage(s, 0.5f);
                double sum = 0.0;
                for (size_t i = 0; i != b.coverage.minor(); ++i)
                    for (size_t j = 0; j != b.coverage.major(); ++j)
                        sum += (float)b.coverage[i, j];
                sum *= 0.25; // pixel area
                require(std::abs(sum - 100.0) < 0.5);
            }

            // More curves than the old 128-deep up-front stack held: 200
            // abutting thin squares tile a 200 x 1 strip.
            {
                std::vector<bezier4> many;
                for (int i = 0; i != 200; ++i) {
                    auto s = square((float)i, 0.0f, (float)(i + 1), 4.0f);
                    many.insert(many.end(), s.begin(), s.end());
                }
                require(many.size() == 800);
                require(std::abs(bezier4_coverage(many, simd_float2{190.5f, 2.0f}, 0.25f) - 1.0f) < 1e-3f);
            }

            co_return;
        };

    } // namespace

} // namespace wry
