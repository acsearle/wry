//
//  path_trace_bvh.cpp
//  client
//

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

#include "path_trace_bvh.hpp"
#include "test.hpp"

namespace wry {

    namespace {

        constexpr int kBins = 16;
        constexpr std::uint32_t kMaxLeaf = 4;
        // Relative cost of a node visit (two box tests) to a triangle test
        constexpr float kTraversalCost = 1.0f;

        struct Box {
            simd_float3 lo = simd_make_float3(INFINITY, INFINITY, INFINITY);
            simd_float3 hi = simd_make_float3(-INFINITY, -INFINITY, -INFINITY);
            void grow(simd_float3 p) { lo = simd_min(lo, p); hi = simd_max(hi, p); }
            void grow(const Box& b) { lo = simd_min(lo, b.lo); hi = simd_max(hi, b.hi); }
            bool empty() const { return lo.x > hi.x; }
            float area() const {
                if (empty())
                    return 0.0f;
                simd_float3 e = hi - lo;
                return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
            }
        };

        struct Item {
            Box box;
            simd_float3 centroid;
            std::uint32_t index;
        };

        struct Builder {
            std::vector<Item> items;
            std::vector<WryPathTraceNode> nodes;

            static void store(WryPathTraceNode& n, const Box& b) {
                for (int a = 0; a != 3; ++a) {
                    n.lo[a] = b.lo[a];
                    n.hi[a] = b.hi[a];
                }
            }

            // Make nodes[node] cover items[begin, end), splitting as the
            // heuristic says
            void build(std::uint32_t node, std::uint32_t begin, std::uint32_t end) {
                Box bounds, centroids;
                for (std::uint32_t i = begin; i != end; ++i) {
                    bounds.grow(items[i].box);
                    centroids.grow(items[i].centroid);
                }
                store(nodes[node], bounds);
                const std::uint32_t count = end - begin;
                auto make_leaf = [&] {
                    nodes[node].first = begin;
                    nodes[node].count = count;
                };
                if (count <= 1) {
                    make_leaf();
                    return;
                }

                // Best split over the bins of each axis' centroid extent
                float best_cost = INFINITY;
                int best_axis = -1;
                int best_split = 0;
                for (int axis = 0; axis != 3; ++axis) {
                    const float lo = centroids.lo[axis];
                    const float extent = centroids.hi[axis] - lo;
                    if (!(extent > 0.0f))
                        continue;
                    Box bin_box[kBins];
                    std::uint32_t bin_count[kBins] = {};
                    const float scale = kBins / extent;
                    for (std::uint32_t i = begin; i != end; ++i) {
                        int b = std::min(kBins - 1, (int)((items[i].centroid[axis] - lo) * scale));
                        bin_box[b].grow(items[i].box);
                        ++bin_count[b];
                    }
                    // Sweep: the cost of splitting after bin s
                    float right_area[kBins];
                    std::uint32_t right_count[kBins];
                    Box acc;
                    std::uint32_t n = 0;
                    for (int s = kBins - 1; s > 0; --s) {
                        acc.grow(bin_box[s]);
                        n += bin_count[s];
                        right_area[s] = acc.area();
                        right_count[s] = n;
                    }
                    acc = Box();
                    n = 0;
                    for (int s = 0; s != kBins - 1; ++s) {
                        acc.grow(bin_box[s]);
                        n += bin_count[s];
                        if (n == 0 || right_count[s + 1] == 0)
                            continue;
                        float cost = acc.area() * (float)n + right_area[s + 1] * (float)right_count[s + 1];
                        if (cost < best_cost) {
                            best_cost = cost;
                            best_axis = axis;
                            best_split = s;
                        }
                    }
                }

                const float leaf_cost = bounds.area() * (float)count;
                const float split_cost = bounds.area() * kTraversalCost + best_cost;
                if (best_axis < 0 || (count <= kMaxLeaf && leaf_cost <= split_cost)) {
                    if (best_axis < 0 && count > kMaxLeaf) {
                        // Coincident centroids: split the list in half
                        std::uint32_t mid = begin + count / 2;
                        split(node, begin, mid, end);
                        return;
                    }
                    make_leaf();
                    return;
                }

                const float lo = centroids.lo[best_axis];
                const float scale = kBins / (centroids.hi[best_axis] - lo);
                auto first_right = std::stable_partition(items.begin() + begin, items.begin() + end,
                                                         [&](const Item& it) {
                    int b = std::min(kBins - 1, (int)((it.centroid[best_axis] - lo) * scale));
                    return b <= best_split;
                });
                std::uint32_t mid = (std::uint32_t)(first_right - items.begin());
                if (mid == begin || mid == end)
                    mid = begin + count / 2;
                split(node, begin, mid, end);
            }

            void split(std::uint32_t node, std::uint32_t begin, std::uint32_t mid, std::uint32_t end) {
                std::uint32_t left = (std::uint32_t)nodes.size();
                nodes.emplace_back();
                nodes.emplace_back();
                nodes[node].first = left;
                nodes[node].count = 0;
                build(left, begin, mid);
                build(left + 1, mid, end);
            }
        };

        // The shader's triangle test (PathTrace.metal triangle_intersect):
        // two-sided Moller-Trumbore
        bool triangle_intersect(const WryPathTraceTriangle& tri, simd_float3 o, simd_float3 d,
                                float t_min, float t_max, float& t) {
            simd_float3 p0 = simd_make_float3(tri.p0[0], tri.p0[1], tri.p0[2]);
            simd_float3 e1 = simd_make_float3(tri.p1[0], tri.p1[1], tri.p1[2]) - p0;
            simd_float3 e2 = simd_make_float3(tri.p2[0], tri.p2[1], tri.p2[2]) - p0;
            simd_float3 pv = simd_cross(d, e2);
            float det = simd_dot(e1, pv);
            if (std::fabs(det) < 1e-12f)
                return false;
            float inv = 1.0f / det;
            simd_float3 tv = o - p0;
            float u = simd_dot(tv, pv) * inv;
            if (u < 0.0f || u > 1.0f)
                return false;
            simd_float3 qv = simd_cross(tv, e1);
            float v = simd_dot(d, qv) * inv;
            if (v < 0.0f || u + v > 1.0f)
                return false;
            t = simd_dot(e2, qv) * inv;
            return t > t_min && t < t_max;
        }

        bool box_hit(const WryPathTraceNode& n, simd_float3 o, simd_float3 inv_d, float t_min, float t_max) {
            float t0 = t_min, t1 = t_max;
            for (int a = 0; a != 3; ++a) {
                float ta = (n.lo[a] - o[a]) * inv_d[a];
                float tb = (n.hi[a] - o[a]) * inv_d[a];
                t0 = std::max(t0, std::min(ta, tb));
                t1 = std::min(t1, std::max(ta, tb));
            }
            return t0 <= t1;
        }

    } // namespace

    PathTraceBVH build_path_trace_bvh(const simd_float4* positions, std::size_t count) {
        Builder b;
        b.items.reserve(count);
        for (std::size_t k = 0; k != count; ++k) {
            Item it;
            for (int c = 0; c != 3; ++c)
                it.box.grow(positions[3 * k + c].xyz);
            // Degenerate (zero-area) triangles can never be hit; drop them
            simd_float3 e1 = positions[3 * k + 1].xyz - positions[3 * k].xyz;
            simd_float3 e2 = positions[3 * k + 2].xyz - positions[3 * k].xyz;
            if (simd_length_squared(simd_cross(e1, e2)) == 0.0f)
                continue;
            it.centroid = (it.box.lo + it.box.hi) * 0.5f;
            it.index = (std::uint32_t)k;
            b.items.push_back(it);
        }
        PathTraceBVH result;
        if (b.items.empty())
            return result;
        b.nodes.reserve(2 * b.items.size());
        b.nodes.emplace_back();
        b.build(0, 0, (std::uint32_t)b.items.size());
        result.nodes = std::move(b.nodes);
        result.triangles.reserve(b.items.size());
        for (const Item& it : b.items) {
            WryPathTraceTriangle t = {};
            for (int a = 0; a != 3; ++a) {
                t.p0[a] = positions[3 * it.index + 0][a];
                t.p1[a] = positions[3 * it.index + 1][a];
                t.p2[a] = positions[3 * it.index + 2][a];
            }
            t.index = it.index;
            result.triangles.push_back(t);
        }
        return result;
    }

    int path_trace_bvh_depth(const PathTraceBVH& bvh) {
        if (bvh.nodes.empty())
            return 0;
        int deepest = 0;
        std::vector<std::pair<std::uint32_t, int>> stack{{0u, 1}};
        while (!stack.empty()) {
            auto [i, depth] = stack.back();
            stack.pop_back();
            deepest = std::max(deepest, depth);
            const WryPathTraceNode& n = bvh.nodes[i];
            if (!n.count) {
                stack.push_back({n.first, depth + 1});
                stack.push_back({n.first + 1, depth + 1});
            }
        }
        return deepest;
    }

    std::int64_t path_trace_bvh_intersect(const PathTraceBVH& bvh,
                                          simd_float3 origin, simd_float3 direction,
                                          float t_min, float& t_max) {
        if (bvh.nodes.empty())
            return -1;
        simd_float3 inv_d = 1.0f / direction;
        std::int64_t hit = -1;
        std::uint32_t stack[64];
        int top = 0;
        stack[top++] = 0;
        while (top) {
            const WryPathTraceNode& n = bvh.nodes[stack[--top]];
            if (!box_hit(n, origin, inv_d, t_min, t_max))
                continue;
            if (n.count) {
                for (std::uint32_t i = n.first; i != n.first + n.count; ++i) {
                    float t;
                    if (triangle_intersect(bvh.triangles[i], origin, direction, t_min, t_max, t)) {
                        t_max = t;
                        hit = bvh.triangles[i].index;
                    }
                }
            } else {
                stack[top++] = n.first;
                stack[top++] = n.first + 1;
            }
        }
        return hit;
    }

    // Random soups (with some coincident and degenerate triangles) against
    // brute force: the same nearest hit for every ray, and every input
    // triangle with area in exactly one leaf
    define_test("path_trace_bvh") {
        std::mt19937 rng(20261119);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        for (std::size_t count : {0, 1, 2, 3, 7, 100, 1000}) {
            std::vector<simd_float4> p(3 * count);
            for (std::size_t k = 0; k != count; ++k) {
                simd_float3 c = simd_make_float3(u(rng), u(rng), u(rng)) * 4.0f;
                if (k % 17 == 5 && k)
                    c = p[3 * (k - 1)].xyz;   // coincident centroids
                for (int j = 0; j != 3; ++j)
                    p[3 * k + j] = simd_make_float4(c + simd_make_float3(u(rng), u(rng), u(rng)) * 0.5f, 1.0f);
                if (k % 23 == 11)
                    p[3 * k + 2] = p[3 * k + 1];   // degenerate
            }
            PathTraceBVH bvh = build_path_trace_bvh(p.data(), count);
            require(path_trace_bvh_depth(bvh) <= 31);   // PathTrace.metal's stack
            std::vector<int> seen(count, 0);
            for (const WryPathTraceNode& n : bvh.nodes)
                for (std::uint32_t i = n.first; n.count && i != n.first + n.count; ++i)
                    ++seen[bvh.triangles[i].index];
            for (std::size_t k = 0; k != count; ++k) {
                bool degenerate = simd_length_squared(simd_cross(p[3 * k + 1].xyz - p[3 * k].xyz,
                                                                 p[3 * k + 2].xyz - p[3 * k].xyz)) == 0.0f;
                require(seen[k] == (degenerate ? 0 : 1));
            }
            for (int r = 0; r != 500; ++r) {
                simd_float3 o = simd_make_float3(u(rng), u(rng), u(rng)) * 6.0f;
                simd_float3 d = simd_normalize(simd_make_float3(u(rng), u(rng), u(rng)));
                if (r % 50 == 0)
                    d = simd_make_float3(0.0f, 0.0f, 1.0f);   // axis-parallel
                float t_bvh = INFINITY;
                std::int64_t h = path_trace_bvh_intersect(bvh, o, d, 1e-4f, t_bvh);
                float t_brute = INFINITY;
                std::int64_t hb = -1;
                for (const WryPathTraceTriangle& tri : bvh.triangles) {
                    float t;
                    if (triangle_intersect(tri, o, d, 1e-4f, t_brute, t)) {
                        t_brute = t;
                        hb = tri.index;
                    }
                }
                require(h == hb);
                require(t_bvh == t_brute);
            }
        }
        co_return;
    };

} // namespace wry
