//
//  symbol_sdf.cpp
//  client
//
//  Created by Antony Searle on 2026-10-29.
//

#include <algorithm>
#include <cmath>
#include <cstring>
#include <system_error>
#include <limits>

#include "symbol_sdf.hpp"
#include "test.hpp"

namespace wry {

    namespace {

        constexpr float kInf = std::numeric_limits<float>::infinity();

    } // namespace

    float edge_distance(float a, float gx, float gy) {
        gx = std::abs(gx);
        gy = std::abs(gy);
        if (gx < gy)
            std::swap(gx, gy);
        if (!(gx > 0.0f))
            return 0.5f - a;
        const float a1 = 0.5f * gy / gx;
        if (a < a1)
            return 0.5f * (gx + gy) - std::sqrt(2.0f * gx * gy * a);
        if (a < 1.0f - a1)
            return (0.5f - a) * gx;
        return -0.5f * (gx + gy) + std::sqrt(2.0f * gx * gy * (1.0f - a));
    }

    // The seeds are the texels the contour passes through: those of
    // partial coverage, and full texels that abut a full texel of the
    // other class (a hard edge, as a thresholded image has everywhere).
    // Each seed knows the point where the contour crosses it: its centre
    // pushed back along the contour's outward normal (the negated Sobel
    // gradient of coverage) by edge_distance.  The crossing points are
    // then propagated to every texel by the two-pass eight-neighbour
    // sweep (Danielsson; the 8SSEDT), each texel keeping the nearest
    // crossing point it hears of from its neighbours: not exact, since
    // the nearest point of a texel is not always the nearest point of a
    // neighbour, but within a tenth of a texel in practice, and it
    // carries off-grid points, which the exact grid transforms cannot
    // (seeding those at texel centres loses the sub-texel placement, and
    // the nearest seed centre is not the nearest crossing).
    matrix<float> signed_distance(matrix_view<const float> coverage) {
        const int rows = (int)coverage.minor();
        const int columns = (int)coverage.major();
        auto at = [&](int y, int x) -> float {
            y = std::clamp(y, 0, rows - 1);
            x = std::clamp(x, 0, columns - 1);
            return coverage[y, x];
        };
        auto inside = [](float a) { return a >= 0.5f; };
        auto full = [](float a) { return a <= 0.0f || a >= 1.0f; };

        // Per texel, the nearest crossing point known so far and its
        // squared distance from the texel's centre
        matrix<float> nearest_x((std::size_t)rows, (std::size_t)columns);
        matrix<float> nearest_y((std::size_t)rows, (std::size_t)columns);
        matrix<float> nearest_r2((std::size_t)rows, (std::size_t)columns);
        // and the contour's outward unit normal there (zero if unknown)
        matrix<float> nearest_nx((std::size_t)rows, (std::size_t)columns);
        matrix<float> nearest_ny((std::size_t)rows, (std::size_t)columns);
        bool any_seed = false;
        for (int y = 0; y != rows; ++y) {
            for (int x = 0; x != columns; ++x) {
                const float a = at(y, x);
                bool seed = !full(a);
                if (!seed) {
                    const bool c = inside(a);
                    for (auto [dy, dx] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}}) {
                        const int ny = y + dy, nx = x + dx;
                        if (ny < 0 || ny >= rows || nx < 0 || nx >= columns)
                            continue;
                        const float b = at(ny, nx);
                        if (full(b) && inside(b) != c) {
                            seed = true;
                            break;
                        }
                    }
                }
                nearest_r2[y, x] = kInf;
                if (!seed)
                    continue;
                any_seed = true;
                float px = x + 0.5f, py = y + 0.5f;
                float unx = 0.0f, uny = 0.0f;
                const float gx = (at(y - 1, x + 1) + 2.0f * at(y, x + 1) + at(y + 1, x + 1))
                               - (at(y - 1, x - 1) + 2.0f * at(y, x - 1) + at(y + 1, x - 1));
                const float gy = (at(y + 1, x - 1) + 2.0f * at(y + 1, x) + at(y + 1, x + 1))
                               - (at(y - 1, x - 1) + 2.0f * at(y - 1, x) + at(y - 1, x + 1));
                const float g = std::hypot(gx, gy);
                if (g > 0.0f) {
                    const float nx = -gx / g, ny = -gy / g;
                    const float e = edge_distance(a, nx, ny);
                    px -= nx * e;
                    py -= ny * e;
                    unx = nx;
                    uny = ny;
                }
                nearest_x[y, x] = px;
                nearest_y[y, x] = py;
                nearest_nx[y, x] = unx;
                nearest_ny[y, x] = uny;
                nearest_r2[y, x] = (px - (x + 0.5f)) * (px - (x + 0.5f))
                                 + (py - (y + 0.5f)) * (py - (y + 0.5f));
            }
        }

        matrix<float> d((std::size_t)rows, (std::size_t)columns);
        if (!any_seed) {
            for (int y = 0; y != rows; ++y)
                for (int x = 0; x != columns; ++x)
                    d[y, x] = kSymbolSDFFar;
            return d;
        }

        // Offer the neighbour's point to (y, x)
        auto relax = [&](int y, int x, int ny, int nx) {
            if (ny < 0 || ny >= rows || nx < 0 || nx >= columns)
                return;
            const float r2 = nearest_r2[ny, nx];
            if (r2 == kInf)
                return;
            const float px = nearest_x[ny, nx], py = nearest_y[ny, nx];
            const float q2 = (px - (x + 0.5f)) * (px - (x + 0.5f))
                           + (py - (y + 0.5f)) * (py - (y + 0.5f));
            if (q2 < nearest_r2[y, x]) {
                nearest_r2[y, x] = q2;
                nearest_x[y, x] = px;
                nearest_y[y, x] = py;
                nearest_nx[y, x] = nearest_nx[ny, nx];
                nearest_ny[y, x] = nearest_ny[ny, nx];
            }
        };
        for (int y = 0; y != rows; ++y) {
            for (int x = 0; x != columns; ++x) {
                relax(y, x, y - 1, x - 1);
                relax(y, x, y - 1, x);
                relax(y, x, y - 1, x + 1);
                relax(y, x, y, x - 1);
            }
            for (int x = columns - 1; x >= 0; --x)
                relax(y, x, y, x + 1);
        }
        for (int y = rows - 1; y >= 0; --y) {
            for (int x = columns - 1; x >= 0; --x) {
                relax(y, x, y + 1, x + 1);
                relax(y, x, y + 1, x);
                relax(y, x, y + 1, x - 1);
                relax(y, x, y, x + 1);
            }
            for (int x = 0; x != columns; ++x)
                relax(y, x, y, x - 1);
        }

        // Two more rounds of hearing from every neighbour, which repairs
        // the points the ordered sweeps missed
        for (int round = 0; round != 2; ++round) {
            for (int y = 0; y != rows; ++y) {
                for (int x = 0; x != columns; ++x) {
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            if (dy || dx)
                                relax(y, x, y + dy, x + dx);
                }
            }
        }

        // The crossing points sample the contour about a texel apart, so
        // a texel near the contour but between two samples is nearer to
        // the contour than to either sample.  The contour through the
        // nearest sample, taken as straight along its normal, gives the
        // distance there; far from the samples that line would extend
        // past the contour's corners, so the point distance, less the
        // half spacing of the samples, is the floor
        for (int y = 0; y != rows; ++y) {
            for (int x = 0; x != columns; ++x) {
                const float px = x + 0.5f - nearest_x[y, x];
                const float py = y + 0.5f - nearest_y[y, x];
                const float along = std::abs(px * nearest_nx[y, x] + py * nearest_ny[y, x]);
                const float r = std::max(along, std::sqrt(nearest_r2[y, x]) - 0.75f);
                d[y, x] = inside(at(y, x)) ? -r : r;
            }
        }
        return d;
    }

    SymbolSDF build_symbol_sdf(matrix_view<const RGBA8Unorm_sRGB> atlas, int cell) {
        constexpr int kCells = SymbolSDF::kCells;
        assert(atlas.minor() == atlas.major());
        assert(atlas.minor() % (std::size_t)kCells == 0);
        const int source_cell = (int)(atlas.minor() / (std::size_t)kCells);
        assert(source_cell % cell == 0);
        const int k = source_cell / cell;   // source texels per field texel

        SymbolSDF result;
        result.source_cell = source_cell;
        result.cell = cell;
        result.image = matrix<SymbolSDFTexel>((std::size_t)(kCells * cell), (std::size_t)(kCells * cell));
        result.stroke_width.assign((std::size_t)kCells * kCells, 0.0f);

        matrix<float> coverage((std::size_t)source_cell, (std::size_t)source_cell);
        for (int cy = 0; cy != kCells; ++cy) {
            for (int cx = 0; cx != kCells; ++cx) {
                // The cell's silhouette; a cell with nothing in it is all
                // far, gradient zero
                bool any = false;
                for (int y = 0; y != source_cell; ++y) {
                    for (int x = 0; x != source_cell; ++x) {
                        const float a = atlas[cy * source_cell + y, cx * source_cell + x].a;
                        coverage[y, x] = a;
                        any = any || (a >= 0.5f);
                    }
                }
                if (!any) {
                    for (int y = 0; y != cell; ++y)
                        for (int x = 0; x != cell; ++x)
                            result.image[cy * cell + y, cx * cell + x] = SymbolSDFTexel{255, 128, 128, 0};
                    continue;
                }
                matrix<float> d = signed_distance(coverage);
                float deepest = 0.0f;
                for (int y = 0; y != source_cell; ++y)
                    for (int x = 0; x != source_cell; ++x)
                        deepest = std::max(deepest, -d[y, x]);
                // Quantized as the texel channel carries it, so a cached
                // field agrees with a fresh one
                const std::uint8_t stroke_channel = (std::uint8_t)std::lround(
                    std::clamp(2.0f * deepest / SymbolSDF::kStrokeWidthScale, 0.0f, 1.0f) * 255.0f);
                result.stroke_width[(std::size_t)cy * kCells + (std::size_t)cx] =
                    stroke_channel / 255.0f * SymbolSDF::kStrokeWidthScale;

                // Box-filter distance and the central-difference gradient
                // down to the field's resolution
                auto dd = [&](int y, int x) -> float {
                    y = std::clamp(y, 0, source_cell - 1);
                    x = std::clamp(x, 0, source_cell - 1);
                    return d[y, x];
                };
                for (int y = 0; y != cell; ++y) {
                    for (int x = 0; x != cell; ++x) {
                        float sum = 0.0f, gx = 0.0f, gy = 0.0f;
                        for (int j = 0; j != k; ++j) {
                            for (int i = 0; i != k; ++i) {
                                const int sy = y * k + j, sx = x * k + i;
                                sum += dd(sy, sx);
                                // Central differences, one-sided at the
                                // cell's border (the span is 1 there)
                                const float span_x = (float)(std::min(sx + 1, source_cell - 1) - std::max(sx - 1, 0));
                                const float span_y = (float)(std::min(sy + 1, source_cell - 1) - std::max(sy - 1, 0));
                                gx += (dd(sy, sx + 1) - dd(sy, sx - 1)) / span_x;
                                gy += (dd(sy + 1, sx) - dd(sy - 1, sx)) / span_y;
                            }
                        }
                        const float mean = sum / (float)(k * k);
                        const float g = std::hypot(gx, gy);
                        float ux = 0.0f, uy = 0.0f;
                        if (g > 0.0f && std::abs(mean) < SymbolSDF::kRange) {
                            ux = gx / g;
                            uy = gy / g;
                        }
                        auto unorm = [](float v) -> std::uint8_t {
                            return (std::uint8_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
                        };
                        SymbolSDFTexel& t = result.image[cy * cell + y, cx * cell + x];
                        t.distance = unorm(mean / (2.0f * SymbolSDF::kRange) + 0.5f);
                        t.gradient_x = unorm(ux * 0.5f + 0.5f);
                        t.gradient_y = unorm(uy * 0.5f + 0.5f);
                        t.stroke_width = stroke_channel;
                    }
                }
            }
        }
        return result;
    }

    // The cache is the texel array as an RGBA PNG; libpng's simplified
    // API moves the bytes untouched either way (the file's sRGB tag is
    // only an interpretation).  Rebuilt when missing, older than the
    // atlas, or not the shape expected.
    SymbolSDF symbol_sdf_for_atlas(std::filesystem::path const& atlas_path) {
        constexpr int kCells = SymbolSDF::kCells;
        std::filesystem::path cache_path = atlas_path;
        cache_path.replace_filename(atlas_path.stem().string() + "_field_v1.png");
        std::error_code ec;
        const bool fresh = std::filesystem::exists(cache_path, ec)
            && std::filesystem::last_write_time(cache_path, ec) >= std::filesystem::last_write_time(atlas_path, ec)
            && !ec;
        if (fresh) {
            matrix<RGBA8Unorm_sRGB> png = from_png(StringView(cache_path.string().c_str()));
            if (png.minor() == png.major() && png.minor() % (std::size_t)kCells == 0 && png.minor() > 0) {
                SymbolSDF result;
                result.cell = (int)(png.minor() / (std::size_t)kCells);
                result.source_cell = result.cell * 2;
                result.image = matrix<SymbolSDFTexel>(png.minor(), png.major());
                for (std::size_t y = 0; y != png.minor(); ++y)
                    std::memcpy(&result.image[(ptrdiff_t)y, 0], &png[(ptrdiff_t)y, 0], png.major() * sizeof(SymbolSDFTexel));
                result.stroke_width.resize((std::size_t)kCells * kCells);
                for (int cy = 0; cy != kCells; ++cy)
                    for (int cx = 0; cx != kCells; ++cx)
                        result.stroke_width[(std::size_t)cy * kCells + (std::size_t)cx] =
                            result.image[cy * result.cell, cx * result.cell].stroke_width / 255.0f
                            * SymbolSDF::kStrokeWidthScale;
                return result;
            }
        }
        matrix<RGBA8Unorm_sRGB> atlas = from_png(StringView(atlas_path.string().c_str()));
        const int cell = (int)(atlas.minor() / (std::size_t)kCells) / 2;
        SymbolSDF result = build_symbol_sdf(atlas, cell);
        matrix<RGBA8Unorm_sRGB> png(result.image.minor(), result.image.major());
        for (std::size_t y = 0; y != png.minor(); ++y)
            std::memcpy(&png[(ptrdiff_t)y, 0], &result.image[(ptrdiff_t)y, 0], png.major() * sizeof(SymbolSDFTexel));
        to_png(png, StringView(cache_path.string().c_str()));
        return result;
    }

    // ----------------------------------------------------------------
    // Tests.

    define_test("symbol_sdf_edge_distance") {
        // Axis-aligned edges are linear in coverage; a diagonal edge
        // through the centre is at distance zero, and at zero coverage
        // the centre is a half diagonal away
        assert(edge_distance(0.5f, 1.0f, 0.0f) == 0.0f);
        assert(edge_distance(0.2f, 0.0f, 1.0f) == 0.3f);
        assert(std::abs(edge_distance(0.5f, 0.7071f, 0.7071f)) < 1e-4f);
        assert(std::abs(edge_distance(0.0f, 0.7071f, 0.7071f) - 0.7071f) < 1e-4f);
        assert(std::abs(edge_distance(1.0f, 0.7071f, 0.7071f) + 0.7071f) < 1e-4f);
        // A corner sliver: the line at distance t cuts area t^2 * ... --
        // for the diagonal, area = (0.7071 - t)^2, so t = 0.7071 - sqrt(a)
        assert(std::abs(edge_distance(0.01f, 0.7071f, 0.7071f) - (0.7071f - 0.1f)) < 1e-3f);
        co_return;
    };

    define_test("symbol_sdf_disc") {
        // An antialiased disc: the signed distance is radial to within a
        // fraction of a texel, the gradient radial, the stroke width the
        // diameter, and the downsampled field agrees at its centres.
        const int n = 64;
        const float cx = 31.3f, cy = 32.6f, radius = 20.0f;
        matrix<float> coverage((std::size_t)n, (std::size_t)n);
        for (int y = 0; y != n; ++y) {
            for (int x = 0; x != n; ++x) {
                // 8x8 supersampled coverage
                int in = 0;
                for (int j = 0; j != 8; ++j)
                    for (int i = 0; i != 8; ++i)
                        in += std::hypot(x + (i + 0.5f) / 8.0f - cx, y + (j + 0.5f) / 8.0f - cy) < radius;
                coverage[y, x] = in / 64.0f;
            }
        }
        matrix<float> d = signed_distance(coverage);
        float worst = 0.0f;
        for (int y = 0; y != n; ++y) {
            for (int x = 0; x != n; ++x) {
                const float truth = std::hypot(x + 0.5f - cx, y + 0.5f - cy) - radius;
                if (std::abs(truth) < 12.0f)
                    worst = std::max(worst, std::abs(d[y, x] - truth));
            }
        }
        // Crossing points model the contour as straight within a texel,
        // the supersampled coverage is quantized to 1/64, and the sweep
        // is not exact; measured 0.08
        assert(worst < 0.15f);

        // The same disc as the only occupied cell of a tiny atlas, built at
        // half resolution
        constexpr int kCells = SymbolSDF::kCells;
        matrix<RGBA8Unorm_sRGB> atlas((std::size_t)(kCells * n), (std::size_t)(kCells * n));
        for (int y = 0; y != n; ++y)
            for (int x = 0; x != n; ++x)
                atlas[3 * n + y, 5 * n + x] = RGBA8Unorm_sRGB(1.0f, 1.0f, 1.0f, coverage[y, x]);
        SymbolSDF sdf = build_symbol_sdf(atlas, n / 2);
        assert(sdf.cell == n / 2 && sdf.source_cell == n);
        assert((std::abs(sdf.stroke_width_at(5, 3) - 2.0f * radius) < 0.75f));   // texel centres miss the disc centre, and 1/4 texel quantization
        assert((sdf.stroke_width_at(0, 0) == 0.0f));
        // An empty cell decodes as far outside, gradient zero, no stroke;
        // the disc's texels carry its diameter
        {
            SymbolSDFTexel t = sdf.image[0, 0];
            assert(t.distance == 255 && t.gradient_x == 128 && t.gradient_y == 128 && t.stroke_width == 0);
            SymbolSDFTexel u = sdf.image[3 * (n / 2), 5 * (n / 2)];
            assert(std::abs(u.stroke_width / 255.0f * SymbolSDF::kStrokeWidthScale - 2.0f * radius) < 0.75f);
        }
        float worst_distance = 0.0f, worst_gradient = 0.0f;
        for (int y = 0; y != n / 2; ++y) {
            for (int x = 0; x != n / 2; ++x) {
                SymbolSDFTexel t = sdf.image[3 * (n / 2) + y, 5 * (n / 2) + x];
                // The field texel's centre in source texels
                const float sx = 2.0f * x + 1.0f, sy = 2.0f * y + 1.0f;
                const float truth = std::hypot(sx - cx, sy - cy) - radius;
                if (std::abs(truth) >= SymbolSDF::kRange - 1.0f)
                    continue;
                const float decoded = (t.distance / 255.0f - 0.5f) * 2.0f * SymbolSDF::kRange;
                worst_distance = std::max(worst_distance, std::abs(decoded - truth));
                const float gx = t.gradient_x / 255.0f * 2.0f - 1.0f;
                const float gy = t.gradient_y / 255.0f * 2.0f - 1.0f;
                const float r = std::hypot(sx - cx, sy - cy);
                if (r > 4.0f)
                    worst_gradient = std::max(worst_gradient,
                                              std::hypot(gx - (sx - cx) / r, gy - (sy - cy) / r));
            }
        }
        // Box filtering adds curvature error near the centre (the
        // gradient is discontinuous there, as it is along every stroke's
        // medial axis), and the 8-bit encodings quantize to 1/8 texel and
        // 1/128 of a unit vector; measured 0.11 and 0.05
        assert(worst_distance < 0.2f);
        assert(worst_gradient < 0.06f);
        co_return;
    };

} // namespace wry
