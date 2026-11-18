//
//  path_trace_grid.cpp
//  client
//

#include <algorithm>
#include <cmath>
#include <cstring>

#include "path_trace_grid.hpp"
#include "test.hpp"

namespace wry {

    PathTraceGridBuilder::PathTraceGridBuilder(simd_int2 origin, simd_int2 size)
    : _origin(origin), _size(simd_max(size, simd_make_int2(1, 1))) {}

    void PathTraceGridBuilder::add(std::uint32_t item, simd_float3 lo, simd_float3 hi) {
        // Cell i spans [i - 0.5, i + 0.5): the cells a box touches
        simd_int2 a = simd_make_int2((int)std::floor(lo.x + 0.5f), (int)std::floor(lo.y + 0.5f)) - _origin;
        simd_int2 b = simd_make_int2((int)std::floor(hi.x + 0.5f), (int)std::floor(hi.y + 0.5f)) - _origin;
        if (b.x < 0 || b.y < 0 || a.x >= _size.x || a.y >= _size.y)
            return;
        a = simd_max(a, simd_make_int2(0, 0));
        b = simd_min(b, _size - 1);
        _entries.push_back(Entry{item, a, b, lo.z, hi.z});
    }

    PathTraceGrid PathTraceGridBuilder::build() const {
        PathTraceGrid g;
        g.origin = _origin;
        g.size = _size;
        const std::size_t cells = (std::size_t)_size.x * (std::size_t)_size.y;
        g.start.assign(cells + 1, 0);
        g.z.assign(cells, simd_make_float2(INFINITY, -INFINITY));
        bool any = false;
        for (const Entry& e : _entries) {
            for (int j = e.a.y; j <= e.b.y; ++j)
                for (int i = e.a.x; i <= e.b.x; ++i)
                    ++g.start[(std::size_t)j * (std::size_t)_size.x + (std::size_t)i + 1];
            if (!any) {
                g.z_lo = e.z0;
                g.z_hi = e.z1;
                any = true;
            }
            g.z_lo = std::min(g.z_lo, e.z0);
            g.z_hi = std::max(g.z_hi, e.z1);
        }
        for (std::size_t c = 0; c != cells; ++c)
            g.start[c + 1] += g.start[c];
        g.items.resize(g.start[cells]);
        std::vector<std::uint32_t> fill(g.start.begin(), g.start.end() - 1);
        for (const Entry& e : _entries) {
            for (int j = e.a.y; j <= e.b.y; ++j) {
                for (int i = e.a.x; i <= e.b.x; ++i) {
                    std::size_t c = (std::size_t)j * (std::size_t)_size.x + (std::size_t)i;
                    g.items[fill[c]++] = e.item;
                    g.z[c].x = std::min(g.z[c].x, e.z0);
                    g.z[c].y = std::max(g.z[c].y, e.z1);
                }
            }
        }
        return g;
    }

    void transform_box(const simd_float4x4& m, simd_float3 lo, simd_float3 hi,
                       simd_float3& out_lo, simd_float3& out_hi) {
        out_lo = simd_make_float3(INFINITY, INFINITY, INFINITY);
        out_hi = -out_lo;
        for (int k = 0; k != 8; ++k) {
            simd_float4 p = simd_make_float4((k & 1) ? hi.x : lo.x,
                                             (k & 2) ? hi.y : lo.y,
                                             (k & 4) ? hi.z : lo.z, 1.0f);
            simd_float3 q = simd_mul(m, p).xyz;
            out_lo = simd_min(out_lo, q);
            out_hi = simd_max(out_hi, q);
        }
    }

    std::uint64_t path_trace_hash(std::uint64_t h, const void* data, std::size_t length) {
        const unsigned char* p = (const unsigned char*)data;
        constexpr std::uint64_t kPrime = 0x100000001B3ull;
        while (length >= 8) {
            std::uint64_t w;
            std::memcpy(&w, p, 8);
            h = (h ^ w) * kPrime;
            h ^= h >> 29;
            p += 8;
            length -= 8;
        }
        while (length--) {
            h = (h ^ *p++) * kPrime;
        }
        h ^= h >> 32;
        return h;
    }

    define_test("path_trace_grid") {
        PathTraceGridBuilder b(simd_make_int2(-2, 3), simd_make_int2(5, 4));
        // In cell (0, 4) only
        b.add(7, simd_make_float3(-0.4f, 3.6f, 0.1f), simd_make_float3(0.4f, 4.4f, 0.2f));
        // Straddling (0..1, 4..5)
        b.add(8, simd_make_float3(0.2f, 4.2f, -0.5f), simd_make_float3(0.7f, 4.6f, 1.0f));
        // Off the grid (x beyond -2.5): dropped
        b.add(9, simd_make_float3(-6.0f, 4.0f, 5.0f), simd_make_float3(-3.0f, 4.0f, 6.0f));
        // Overhanging the grid's edge: clamped to the edge cells
        b.add(10, simd_make_float3(1.8f, 6.2f, 0.0f), simd_make_float3(4.0f, 9.0f, 0.5f));
        PathTraceGrid g = b.build();
        auto cell = [&](int x, int y) { return (std::size_t)(y - 3) * 5 + (std::size_t)(x + 2); };
        auto items_of = [&](int x, int y) {
            std::size_t c = cell(x, y);
            return std::vector<std::uint32_t>(g.items.begin() + g.start[c], g.items.begin() + g.start[c + 1]);
        };
        require(g.start.size() == 21);
        require((items_of(0, 4) == std::vector<std::uint32_t>{7, 8}));
        require((items_of(1, 4) == std::vector<std::uint32_t>{8}));
        require((items_of(0, 5) == std::vector<std::uint32_t>{8}));
        require((items_of(1, 5) == std::vector<std::uint32_t>{8}));
        require((items_of(2, 6) == std::vector<std::uint32_t>{10}));
        require(items_of(-2, 3).empty());
        require(g.items.size() == 6);
        require(g.z[cell(0, 4)].x == -0.5f && g.z[cell(0, 4)].y == 1.0f);
        require(g.z[cell(-1, 3)].x > g.z[cell(-1, 3)].y);
        require(g.z_lo == -0.5f && g.z_hi == 1.0f);

        simd_float3 lo, hi;
        simd_float4x4 m = matrix_identity_float4x4;
        m.columns[0] = simd_make_float4(0.0f, 2.0f, 0.0f, 0.0f);    // x -> 2y
        m.columns[1] = simd_make_float4(-1.0f, 0.0f, 0.0f, 0.0f);   // y -> -x
        m.columns[3] = simd_make_float4(10.0f, 0.0f, 1.0f, 1.0f);
        transform_box(m, simd_make_float3(-1, -1, -1), simd_make_float3(1, 2, 3), lo, hi);
        require(lo.x == 8.0f && hi.x == 11.0f && lo.y == -2.0f && hi.y == 2.0f && lo.z == 0.0f && hi.z == 4.0f);

        const char a[] = "the frame's inputs", c[] = "the frame's inputz";
        require(path_trace_hash(1, a, sizeof a) == path_trace_hash(1, a, sizeof a));
        require(path_trace_hash(1, a, sizeof a) != path_trace_hash(1, c, sizeof c));
        require(path_trace_hash(1, a, sizeof a) != path_trace_hash(2, a, sizeof a));
        co_return;
    };

} // namespace wry
