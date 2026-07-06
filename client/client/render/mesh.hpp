//
//  mesh.hpp
//  client
//
//  Created by Antony Searle on 17/8/2023.
//

#ifndef mesh_hpp
#define mesh_hpp

#include <set>

#include "ShaderTypes.h"

#include "contiguous_deque.hpp"
#include "debug.hpp"
#include "packed.hpp"
#include "simd.hpp"
#include "table.hpp"

namespace wry::mesh {

    // Consider a mapping f(x) : R^4 -> R^4, specifically from
    // the parameterization most familiar as texture coordinates, to a
    // position in space.
    //
    // We can compute a Jacobian where the function C^1
    //
    //    J(x) = [ \frac{\partial}{\partial \x_j} f_i ]
    //
    // The ith column corresponds to the local projection of the coordinate
    // basis vectors e_i.
    //
    // The first two columns are vectors (bi)tangent to a surface defined
    // by constant x.zw.  The third points out of that surface, though not
    // nessarily perpendicular to it.
    //
    // If the mapping f is *homogeneous*, so that
    //
    //     f((x, y, z, w)) = w * f((x, y, z, 1.0))
    //
    // then we get
    //
    //     J(:, 4) = \frac{\partial}{\partial e_4}
    //
    // and the Jacobian neatly defines the tranformation from coordinates
    // to the tangent space.
    //
    // The Jacobian as-is provides the correct contravariant transformation
    // for diplacement mapping.  The normal is a dual vector, which
    // requires instead the covariant transformation, i.e. the inverse
    // transpose.

    struct vertex {

        float4 coordinate;

        union {
            struct {
                float4 tangent;
                float4 bitangent;
                float4 normal;
                float4 position;
            };
            float4x4 jacobian;
        };

        vertex() {}

        auto operator<=>(const vertex& other) const {
            float a[20], b[20];
            std::memcpy(a, this, sizeof(a));
            std::memcpy(b, &other, sizeof(b));
            return std::lexicographical_compare_three_way(std::begin(a), std::end(a),
                                                          std::begin(b), std::end(b),
                                                          [](float a, float b) {
                return a <=> b;
            });
        }


    };


    struct edge {

        size_t indices[2];

        bool operator==(const edge&) const = default;
        void flip() {
            std::swap(indices[0], indices[1]);
        }

    };

    inline uint64_t hash(edge e) {
        return hash_combine(&e, sizeof(e));
    }

    struct triangle {

        size_t indices[3];

        bool operator==(const triangle&) const = default;
        auto operator<=>(const triangle&) const = default;

        void flip() {
            std::swap(indices[1], indices[2]);
        }

        void rotate_left() {
            rotate_args_left(indices[0], indices[1], indices[2]);
        }

        bool is_standard_form() {
            return (indices[0] < indices[1]) && (indices[0] < indices[2]);
        }

        void make_standard_form() {
            while (!is_standard_form())
                rotate_left();
        }

        bool is_nondegenerate() {
            return ((indices[0] != indices[1])
                    && (indices[1] != indices[2])
                    && (indices[2] != indices[0]));
        }

    };

    inline void print(triangle t) {
        printf("triangle{{[%ld,%ld,%ld}}", t.indices[0], t.indices[1], t.indices[2]);
    }

    inline void print(edge e) {
        printf("edge{{[%ld,%ld}}", e.indices[0], e.indices[1]);
    }

    struct quad {
        size_t indices[4];
        void flip() {
            std::swap(indices[1], indices[3]);
        }
    };

    struct face {
        ContiguousDeque<size_t> indices;
        void flip() {
            std::reverse(std::begin(indices), std::end(indices));
        }
    };

    struct mesh {

        ContiguousDeque<vertex> vertices;

        ContiguousDeque<float4> positions;
        ContiguousDeque<float4> coordinates;
        ContiguousDeque<float4> tangents;
        ContiguousDeque<float4> bitangents;
        ContiguousDeque<float4> normals;

        ContiguousDeque<edge> edges;
        ContiguousDeque<quad> quads;

        ContiguousDeque<face> faces;
        ContiguousDeque<triangle> triangles;
        ContiguousDeque<size_t> triangle_strip;

        ContiguousDeque<MeshVertex> hack_MeshVertex;
        ContiguousDeque<float4> hack_lines;
        ContiguousDeque<uint> hack_triangle_strip;

        float distance(size_t i, size_t j) {
            return simd::distance(vertices[i].position.xyz, vertices[j].position.xyz);
        }

        float distance_squared(size_t i, size_t j) {
            return simd::distance_squared(vertices[i].position.xyz, vertices[j].position.xyz);
        }

        float length(edge e) {
            return distance(e.indices[0], e.indices[1]);
        }

        float length_squared(edge e) {
            return distance_squared(e.indices[0], e.indices[1]);
        }

        float3 directed_area(triangle t) {
            return 0.5f * cross(vertices[t.indices[1]].position.xyz
                                - vertices[t.indices[0]].position.xyz,
                                vertices[t.indices[2]].position.xyz
                                - vertices[t.indices[1]].position.xyz);
        }

        float3 weighted_normal(triangle t) {

            float4 a = vertices[t.indices[0]].position;
            float4 b = vertices[t.indices[1]].position;
            float4 c = vertices[t.indices[2]].position;

            float3 d = normalize(b.xyz * a.w - a.xyz * b.w);
            float3 e = normalize(c.xyz * a.w - a.xyz * c.w);

            float3 y = cross(d, e); // normal * sin\theta

            return y;
        }

        void add_quads_box(float4 a, float4 b) {
            auto offset = vertices.size();

            // +Z face of a cube

            float4 c[] = {
                {  0.0f,  0.0f,  0.0f,  1.0f, },
                {  2.0f,  0.0f,  0.0f,  0.0f, },
                {  0.0f,  2.0f,  0.0f,  0.0f, },
                {  0.0f,  0.0f,  2.0f,  0.0f, },
                { -1.0f, -1.0f,  1.0f,  1.0f, },

                {  1.0f,  0.0f,  0.0f,  1.0f, },
                {  2.0f,  0.0f,  0.0f,  0.0f, },
                {  0.0f,  2.0f,  0.0f,  0.0f, },
                {  0.0f,  0.0f,  2.0f,  0.0f, },
                {  1.0f, -1.0f,  1.0f,  1.0f, },

                {  1.0f,  1.0f,  0.0f,  1.0f, },
                {  2.0f,  0.0f,  0.0f,  0.0f, },
                {  0.0f,  2.0f,  0.0f,  0.0f, },
                {  0.0f,  0.0f,  2.0f,  0.0f, },
                {  1.0f,  1.0f,  1.0f,  1.0f, },

                {  0.0f,  1.0f,  0.0f,  1.0f, },
                {  2.0f,  0.0f,  0.0f,  0.0f, },
                {  0.0f,  2.0f,  0.0f,  0.0f, },
                {  0.0f,  0.0f,  2.0f,  0.0f, },
                { -1.0f,  1.0f,  1.0f,  1.0f, },

            };

            vertices.resize(offset + 4);
            std::memcpy(vertices.end()-4, c, sizeof(c));
            quad q = {{offset + 0, offset + 1, offset + 2, offset + 3}};
            quads.push_back(q);

            float4x4 Rx(float4{  1.0f,  0.0f,  0.0f,  0.0f, },
                        float4{  0.0f,  0.0f, -1.0f,  0.0f, },
                        float4{  0.0f,  1.0f,  0.0f,  0.0f, },
                        float4{  0.0f,  0.0f,  0.0f,  1.0f, });

            float4x4 Ry(float4{  0.0f,  0.0f, -1.0f,  0.0f, },
                        float4{  0.0f,  1.0f,  0.0f,  0.0f, },
                        float4{  1.0f,  0.0f,  0.0f,  0.0f, },
                        float4{  0.0f,  0.0f,  0.0f,  1.0f, });

            auto foo = [&](quad q, float4x4 A) {
                for (size_t& j : q.indices) {
                    vertex v = vertices[j];
                    j = vertices.size();
                    v.jacobian = A * v.jacobian;
                    vertices.push_back(v);
                }
                quads.push_back(q);
            };

            foo(q, Rx);
            foo(q, Rx * Rx);
            foo(q, inverse(Rx));
            foo(q, Ry);
            foo(q, inverse(Ry));

            float4x4 A = simd_matrix_translate(simd_make_float3(1.0f, 1.0f, 1.0f));
            float4x4 B = simd_matrix_scale((b.xyz - a.xyz) * 0.5f);
            float4x4 C = simd_matrix_translate(a.xyz);
            float4x4 T = C * B * A;

            for (size_t i = offset; i != vertices.size(); ++i) {
                vertices[i].jacobian = T * vertices[i].jacobian;
            }

        }

        void add_edges_circle(size_t n) {
            auto offset = vertices.size();
            for (size_t i = 0;; ++i) {
                vertex v;
                float theta = 2 * M_PI * i / n;
                float c = cos(theta);
                float s = sin(theta);
                v.coordinate = vector4(theta, 0.0f, 0.0f, 1.0f);
                v.jacobian = simd_matrix(vector4(  -s,    c, 0.0f, 0.0f),
                                         vector4(0.0f, 0.0f, 1.0f, 0.0f),
                                         vector4(   c,    s, 0.0f, 0.0f),
                                         vector4(   c,    s, 0.0f, 1.0f));
                vertices.push_back(v);
                if (i == n)
                    break;
                edges.push_back(edge{{
                    offset + i,
                    offset + i + 1,
                }});
            }
        }

        void add_edges_superquadric(size_t n) {
            auto offset = vertices.size();
            for (size_t i = 0;; ++i) {
                vertex v;
                float theta = 2 * M_PI * (i + 0.5f) / n;

                float c = cos(theta);
                float s = sin(theta);

                auto foo = [](float x) {
                    return x ? sign(x) * sqrt(abs(x)) : 0.0f;
                };

                v.coordinate = vector4(theta, 0.0f, 0.0f, 1.0f);

                v.position = vector4(foo(c), foo(s), 0.0f, 1.0f);

                // sqrt(cos(x)) = sin(x)/2sqrt(cos(x))

                v.tangent = vector4(-s/sqrt(abs(c)), c/sqrt(abs(s)), 0.0f, 0.0f) * 0.5f;
                v.bitangent = vector4(0.0f, 0.0f, -1.0f, 0.0f);
                v.normal.xyz = cross(v.tangent.xyz, -v.bitangent.xyz);
                v.normal.w = 0;


                vertices.push_back(v);
                if (i == n)
                    break;
                edges.push_back(edge{{
                    offset + i,
                    offset + i + 1,
                }});
            }
        }

        void add_face_disk(size_t n) {
            auto offset = vertices.size();
            for (size_t i = 0; i != n; ++i) {
                vertex v;
                float theta = 2 * M_PI * i / n;
                float c = cos(theta);
                float s = sin(theta);
                v.coordinate = vector4(c, s, 0.0f, 1.0f);
                v.jacobian = simd_matrix(vector4(1.0f, 0.0f, 0.0f, 0.0f),
                                         vector4(0.0f, 1.0f, 0.0f, 0.0f),
                                         vector4(0.0f, 0.0f, 1.0f, 0.0f),
                                         vector4(   c,    s, 0.0f, 1.0f));
                vertices.push_back(v);
                edges.push_back(edge{{
                    offset + i,
                    offset + (i + 1) % n,
                }});
            }
        }

        void add_edges_polygon(size_t n) {
            size_t offset = vertices.size();
            float s = 0.0f;
            for (size_t i = 0; i != n; ++i) {

                vertex v0;
                vertex v1;

                float theta0 = 2 * M_PI * (i + 0) / n;
                float theta1 = 2 * M_PI * (i + 1) / n;

                v0.position = vector4(cos(theta0), sin(theta0), 0.0f, 1.0f);
                v1.position = vector4(cos(theta1), sin(theta1), 0.0f, 1.0f);

                v0.tangent = v1.position - v0.position;
                using ::simd::length;
                float t = length(v0.tangent);
                v0.tangent /= t;

                v0.coordinate = vector4(s, 0.0f, 0.0f, 1.0f);
                v0.bitangent = vector4(0.0f, 0.0f, 1.0f, 0.0f);
                v0.normal = vector4(normalize(cross(v0.tangent.xyz, v0.bitangent.xyz)), 0.0f);

                s += t;

                v1.coordinate = vector4(s, 0.0f, 0.0f, 1.0f);
                v1.tangent = v0.tangent;
                v1.bitangent = v0.bitangent;
                v1.normal = v0.normal;

                vertices.push_back(v0);
                vertices.push_back(v1);
                edges.push_back({{
                    offset + i * 2,
                    offset + i * 2 + 1,
                }});

            }
        }

        void reparameterize_with_matrix(float4x4 A) {
            float4x4 B = inverse(A);
            for (vertex& v : vertices) {
                v.coordinate = A * v.coordinate;
                v.jacobian = v.jacobian * B;
            }
        }

        void transform_with_matrix(float4x4 A) {
            for (vertex& v : vertices) {
                v.jacobian = A * v.jacobian;
            }
        }

        void transform_with_differentiable_function(auto&& h) {
            for (vertex& v : vertices) {
                auto [A, B] = h(v.position, v.coordinate);
                v.jacobian = (A * v.jacobian) + B;
            }
        }

        void transform_with_function(auto&& h) {
            auto d = [&h](float4 position, float4 coordinate) {
                const float epsilon = 0.00034526698f;
                const float k = 0.5f / epsilon;
                float4x4 A, B;
                for (size_t i = 0; i != 4; ++i) {
                    float4 delta = 0.0f;
                    delta[i] = epsilon;
                    A.columns[i] = (h(position + delta, coordinate) -
                                    h(position - delta, coordinate)) * k;
                    B.columns[i] = (h(position, coordinate + delta) -
                                    h(position, coordinate - delta)) * k;
                }
                return std::make_pair(A, B);
            };
            transform_with_differentiable_function(d);
        }

        void extrude(size_t n, float4 delta = vector4(0.0f, 1.0f, 0.0f, 0.0f)) {
            // extrude edges along a tangent space vector
            for (size_t i = 0; i != n; ++i) {
                for (size_t j = 0; j != edges.size(); ++j) {
                    size_t j0 = edges[j].indices[0];
                    size_t j1 = edges[j].indices[1];
                    vertex v0 = vertices[j0];
                    vertex v1 = vertices[j1];
                    vertex v2 = v0;
                    vertex v3 = v1;
                    v2.coordinate += delta;
                    v2.position += v2.jacobian * delta;
                    v3.coordinate += delta;
                    v3.position += v3.jacobian * delta;
                    size_t j2 = vertices.size();
                    vertices.push_back(v2);
                    size_t j3 = vertices.size();
                    vertices.push_back(v3);
                    edges[j].indices[0] = j2;
                    edges[j].indices[1] = j3;
                    quads.push_back(quad{{j1, j0, j2, j3 }});
                }
            }
        }

        // call f with size_t& of every piece of geometry
        void for_each_index(auto&& f) {
            for (edge& e : edges)
                for (size_t& i : e.indices)
                    f(i);
            for (triangle& t : triangles)
                for (size_t& i : t.indices)
                    f(i);
            for (quad& q : quads)
                for (size_t& i : q.indices)
                    f(i);
            for (face& g : faces)
                for (size_t& i : g.indices)
                    f(i);
        }

        void erase_unindexed_vertices() {
            size_t n = vertices.size();
            ContiguousDeque<size_t> a(n, 0);
            for_each_index([&](size_t& i) {
                ++a[i];
            });
            {
                size_t j = 0;
                for (size_t i = 0; i != n; ++i) {
                    if (a[i] > 0) {
                        if (j != i)
                            vertices[j] = std::move(vertices[i]);
                        a[i] = j;
                        ++j;
                    }
                }
                printf("erasing %g%% of vertices\n", (100.0f * (n-j)) / n);
                vertices.erase(vertices.begin() + j, vertices.end());
            }
            for_each_index([&](size_t& i) {
                i = a[i];
            });
        }

        // groups nearby vertices and returns mapping from indices to the
        // index of a representative vertex of each group
        ContiguousDeque<size_t> identify_colocated_vertices() {
            size_t n = vertices.size();
            ContiguousDeque<size_t> a(n);
            for (size_t i = 0; i != n; ++i)
                a[i] = i;
            // one dimensional sort against a pattern-defeating direction
            float4 direction = simd::normalize(make<float4>(61, 59, 53, 47));
            auto metric = [&](size_t i) -> float {
                return simd_dot(vertices[i].position, direction);
            };
            auto compare = [&](size_t i, size_t j) -> bool {
                return metric(i) < metric(j);
            };
            std::sort(a.begin(), a.end(), compare);

            // b unscrambles a
            ContiguousDeque<size_t> b(n);
            for (size_t i = 0; i != n; ++i)
                b[a[i]] = i;

            float threshold = 0.00034526698f;
            float threshold2 = 1.1920929e-7f;

            threshold *= 100;
            threshold2 *= 10000;

            size_t count = 0;
            size_t j0 = 0;
            for (size_t i = 1; i != n; ++i) {
                float4 vi = vertices[a[i]].position;
                float dt = metric(a[i]) - threshold;
                for (size_t j = j0; j != i; ++j) {
                    float4 vj = vertices[a[j]].position;
                    float dj = metric(a[j]);
                    float d2 = simd_distance_squared(vi, vj);
                    if (dj < dt) {
                        assert(d2 >= threshold2); // this might be too tight
                        ++j0;
                        continue;
                    }
                    if (d2 < threshold2) {
                        a[i] = a[j];
                        ++count;
                        break;
                    }
                }
            }
            // remove one layer of indirection
            for (size_t i = 0; i != n; ++i)
                b[i] = a[b[i]];
            printf("identify_colocated_vertices: %ld/%ld\n", count, n);
            return b;
        }

        void colocate_similar_vertices() {

            ContiguousDeque<size_t> a = identify_colocated_vertices();
            size_t n = vertices.size();
            assert(a.size() == n);
            size_t count = 0;
            for (size_t i = 0; i != n; ++i) {
                if (a[i] != i) {
                    vertices[i].position = vertices[a[i]].position;
                    ++count;
                }
            }
            printf("colocate_similar_vertices: %ld/%ld\n", count, n);
        }

        void combine_duplicate_vertices() {

            // initial implementation requires exact equality, which is
            // overly strict but a great simplification vs clustering

            // since a common case is parameter discontinuity on an
            // otherwise smooth surface, we should also harmonize
            // similar positions

            size_t n = vertices.size();
            ContiguousDeque<size_t> a(n);
            for (size_t i = 0; i != n; ++i)
                a[i] = i;

            // we can use std::sort here because we are going to merge
            // equivalent vertices
            std::sort(a.begin(), a.end(),
                      [&](size_t x, size_t y) {
                return vertices[x] < vertices[y];
            });

            // a[i] now holds the sorted order of vertices

            ContiguousDeque<size_t> b(n);
            for (size_t i = 0; i != n; ++i) {
                b[a[i]] = i;
            }

            // b[i] now holds the reverse sort

            // redirect duplicates to first occurrence
            size_t m = 0;
            for (size_t i = 1; i != vertices.size(); ++i) {
                if (!(vertices[a[i-1]] < vertices[a[i]])) {
                    ++m;
                    a[i] = a[i-1];
                }
            }

            printf("redirected %g%% vertices\n", (100.0f * m) / n);

            for_each_index([&](size_t& i) {
                i = a[b[i]];
            });

            erase_unindexed_vertices();

        }

        void triangulate() {
            for (face& f : faces) {
                if (f.indices.size() >= 3) {
                    for (size_t i = 1; i + 1 != f.indices.size(); ++i) {
                        triangles.push_back(triangle{{
                            f.indices[0],
                            f.indices[i],
                            f.indices[i + 1],
                        }});
                    }
                }
            }
            for (quad& q : quads) {
                float d02 = simd_distance_squared(vertices[q.indices[0]].position,
                                                  vertices[q.indices[2]].position);
                float d13 = simd_distance_squared(vertices[q.indices[1]].position,
                                                  vertices[q.indices[3]].position);
                // split by shortest diagonal
                // how does this relate to Delaunay/circumcircle?
                if (d13 <= d02) {
                    triangles.push_back(triangle{{
                        q.indices[0],
                        q.indices[1],
                        q.indices[2]
                    }});
                    triangles.push_back(triangle{{
                        q.indices[0],
                        q.indices[2],
                        q.indices[3]
                    }});
                } else {
                    triangles.push_back(triangle{{
                        q.indices[0],
                        q.indices[1],
                        q.indices[3]
                    }});
                    triangles.push_back(triangle{{
                        q.indices[1],
                        q.indices[2],
                        q.indices[3]
                    }});
                }
            }
        }

        void repair_texturing(float scale) {
            for (auto& v : vertices) {
                v.tangent = normalize(v.tangent);
                v.bitangent = normalize(v.bitangent);
                v.normal = normalize(v.normal);
                float4 a = make<float4>(v.position.xyz, 0) / scale;
                v.coordinate = v.jacobian * a;
            }
        }

        void repair_jacobian() {
            for (auto& v : vertices) {
                v.normal = 0.0f;
            }
            for (auto t : triangles) {
                for (auto i = 0; i != 3; ++i) {
                    auto a = vertices[t.indices[0]].position.xyz;
                    auto b = vertices[t.indices[1]].position.xyz;
                    auto c = vertices[t.indices[2]].position.xyz;
                    auto d = simd_normalize(b - a);
                    auto e = simd_normalize(c - a);
                    auto f = simd_cross(d, e);
                    // auto g = make<float4>(f, 0.0);
                    vertices[t.indices[0]].normal.xyz += f;
                    t.rotate_left();
                }
            }
            for (auto& v : vertices) {
                v.normal.xyz = simd_normalize(v.normal.xyz);
                v.normal.w = 0;
                if (abs(v.normal.x) < 0.577f) {
                    v.bitangent.xyz = simd_normalize(simd_cross(v.normal.xyz, simd_make_float3(1,0,0)));
                } else {
                    v.bitangent.xyz = simd_normalize(simd_cross(v.normal.xyz, simd_make_float3(0,1,0)));
                }
                v.bitangent.w = 0;
                v.tangent.xyz = simd_normalize(simd_cross(v.normal.xyz, v.bitangent.xyz));
                v.tangent.w = 0;
            }
        }

        // Tangent frames for baked normal maps, for hack_MeshVertex as
        // produced by from_obj: three unshared vertices per triangle,
        // authored normals preserved.  Computes per-triangle dP/du and
        // dP/dv from the texture coordinates and assigns the
        // Gram-Schmidt-orthonormalized frame to each corner.  A normal
        // map baked against the mesh's UV layout (Blender et al.) shades
        // correctly only in this basis; a frame invented from the normal
        // alone (repair_jacobian above) rotates or mirrors the baked
        // detail on the surface.  Degenerate or missing UVs fall back to
        // that arbitrary perpendicular basis.
        void repair_tangents() {
            assert(!(hack_MeshVertex.size() % 3));
            for (size_t i = 0; i + 2 < hack_MeshVertex.size(); i += 3) {
                auto e1 = hack_MeshVertex[i + 1].position.xyz - hack_MeshVertex[i].position.xyz;
                auto e2 = hack_MeshVertex[i + 2].position.xyz - hack_MeshVertex[i].position.xyz;
                auto d1 = hack_MeshVertex[i + 1].coordinate.xy - hack_MeshVertex[i].coordinate.xy;
                auto d2 = hack_MeshVertex[i + 2].coordinate.xy - hack_MeshVertex[i].coordinate.xy;
                float det = d1.x * d2.y - d2.x * d1.y;
                simd_float3 T = {}, B = {};
                bool mapped = abs(det) > 1e-12f;
                if (mapped) {
                    float r = 1.0f / det;
                    T = (e1 * d2.y - e2 * d1.y) * r;
                    // stored t runs opposite to the authored bottom-left-
                    // origin v that normal maps are baked against (from_obj
                    // flips V for top-left-origin sampling), so negate to
                    // recover the baker's bitangent: dP/dv = -dP/dt
                    B = -(e2 * d1.x - e1 * d2.x) * r;
                }
                for (size_t j = 0; j != 3; ++j) {
                    MeshVertex& v = hack_MeshVertex[i + j];
                    auto n = simd_normalize(v.normal.xyz);
                    auto t = T - n * simd_dot(n, T);
                    if (mapped && simd_length_squared(t) > 1e-12f * simd_length_squared(T)) {
                        t = simd_normalize(t);
                        auto b = B - n * simd_dot(n, B) - t * simd_dot(t, B);
                        if (simd_length_squared(b) > 1e-12f * simd_length_squared(B)) {
                            b = simd_normalize(b);
                        } else {
                            b = simd_cross(n, t);
                        }
                        v.tangent.xyz = t;
                        v.bitangent.xyz = b;
                    } else {
                        if (abs(n.x) < 0.577f) {
                            v.bitangent.xyz = simd_normalize(simd_cross(n, simd_make_float3(1,0,0)));
                        } else {
                            v.bitangent.xyz = simd_normalize(simd_cross(n, simd_make_float3(0,1,0)));
                        }
                        v.tangent.xyz = simd_normalize(simd_cross(n, v.bitangent.xyz));
                    }
                    v.normal.xyz = n;
                    v.tangent.w = 0;
                    v.bitangent.w = 0;
                    v.normal.w = 0;
                }
            }
        }

        void strip() {
            // In O(N) time, build a table to O(1) lookup triangles by
            // directed edge; each directed edge should appear only once
            // else the mesh is bad
            Table<edge, size_t> tbl;
            std::set<triangle> st;
            size_t n = triangles.size();
            for (size_t i = 0; i != n; ++i) {
                //printf("%ld: ", i);
                triangle t = triangles[i];
                //print(t); printf(" -> ");
                t.make_standard_form();
                //print(t);
                //printf("\n");
                {
                    assert(!st.contains(t));
                    auto [q, did_insert] = st.insert(t);
                    assert(did_insert);
                    assert(st.contains(t));
                    auto p = st.find(t);
                    assert(p != st.end());
                    assert(*p == t);
                    assert(st.size() == i + 1);
                    //printf("   *st.find(");  print(t); printf(" == "); print(*p); printf("\n");
                }
                for (size_t j = 0; j != 3; ++j) {
                    edge e = {{ t.indices[0], t.indices[1] }};
                    assert(!tbl.contains(e));
                    std::pair<edge, size_t> peu = { e, t.indices[2] };
                    auto [dummy, did_insert] = tbl.insert(peu);
                    assert(did_insert);
                    assert(tbl.contains(e));
                    auto p = tbl.find(e);
                    assert(p != tbl.end());
                    assert(p->first == e);
                    assert(p->second == t.indices[2]);
                    assert(tbl.size() == (i * 3) + j + 1);
                    //printf("   *tbl.find({");
                    //print(e);
                    //printf(",%ld} == {", t.indices[2]);
                    //print(p->first);
                    //printf("},%ld}\n", p->second);
                    t.rotate_left();
                }
            }


            auto erase_everywhere = [&](triangle t) {
                for (size_t i = 0; i != 3; ++i) {
                    if (t.is_standard_form()) {
                        assert(st.contains(t));
                        auto p = st.find(t);
                        assert(*p == t);
                        auto count = st.erase(t);
                        assert(count == 1);
                        assert(!st.contains(t));
                    }
                    {
                        edge e = {{t.indices[0], t.indices[1]}};
                        assert(tbl.contains(e));
                        auto p = tbl.find(e);
                        assert(p->first == e);
                        assert(p->second == t.indices[2]);
                        auto count = tbl.erase(e);
                        assert(count == 1);
                        assert(!tbl.contains(e));
                    }
                    t.rotate_left();
                }
            };

            size_t restarts = 0;

            while (!st.empty()) {
                if (triangle_strip.size() >= 2) {
                    //printf("pairing with triangle_strip.size()==%ld\n", triangle_strip.size());
                    auto p = triangle_strip.end();
                    size_t b = *--p;
                    size_t a = *--p;
                    edge e{{a, b}};
                    if (((triangle_strip.size() & 1)))
                        e.flip();
                    //printf("        expecting" ); print(e); printf("\n");
                    auto q = tbl.find(e);
                    if (q != tbl.end()) {
                        //printf("    found " ); print(e); printf(",%ld\n", q->second);
                        //printf("    push_back(%ld)\n", q->second);
                        triangle_strip.push_back(q->second);
                        erase_everywhere(triangle{{
                            q->first.indices[0],
                            q->first.indices[1],
                            q->second
                        }});
                        continue;
                    }
                    // failed to find the edge, check for the other
                    // direction in case the parity is borked
                    e.flip();
                    q = tbl.find(e);
                    assert(q == tbl.end());
                }
                ++restarts;
                auto i = st.begin();
                triangle t = *i;
                assert(st.size()*3 == tbl.size());
                erase_everywhere(t);
                assert(st.size()*3 == tbl.size());
                //printf("restarting with triangle_strip.size()=%ld\n", triangle_strip.size());
                //printf("       restart triangle: "); print(t); printf("\n");
                for (size_t j = 0; j != 3; ++j) {
                    edge e = {{t.indices[2], t.indices[1]}};
                    auto p = tbl.find(e);
                    if (p != tbl.end()) {
                        //printf("        successor triangle: {"); print(p->first); printf(",%ld}\n", p->second);
                        break;
                    }
                    t.rotate_left();
                }
                if (!triangle_strip.empty()) {
                    size_t j = triangle_strip.back();
                    //printf("    push_back(%ld)\n", j);
                    triangle_strip.push_back(j);
                    //printf("    push_back(%ld)\n", t.indices[0]);
                    triangle_strip.push_back(t.indices[0]);
                }
                //printf("    push_back(%ld)\n", t.indices[0]);
                triangle_strip.push_back(t.indices[0]);
                if (!(triangle_strip.size() & 1)) {
                    t.flip();
                }
                //printf("    push_back(%ld)\n", t.indices[1]);
                triangle_strip.push_back(t.indices[1]);
                //printf("    push_back(%ld)\n", t.indices[2]);
                triangle_strip.push_back(t.indices[2]);

            }

            assert(tbl.empty());


            printf("strip is %g%% of raw\n", (100.0f * triangle_strip.size()) / (3.0f * triangles.size()));

            printf("triangle_strip: %ld (%ld restarts)\n", triangle_strip.size(), restarts);
            printf("triangles: %ld (%ld indices)\n", triangles.size(), triangles.size() * 3);
            printf("vertices: %ld\n", vertices.size());

        }

        void reindex_for_strip() {

            size_t n = vertices.size();
            size_t m = triangle_strip.size();
            // we start with:
            // vertex[triangle_strip[i]]

            // add a layer of indirection
            ContiguousDeque<size_t> a(n), b(n);
            for (size_t i = 0; i != n; ++i) {
                a[i] = i; // forward
                b[i] = i; // backward
            }

            // we now have vertex[a[triangle_strip[i]]]
            size_t j = 0;
            for (size_t i = 0; i != m; ++i) {
                // vertex[a[triangle_strip[i]] is now the highest priority
                // we want to swap it into the earliest non-finalized
                // location, triangle_strip[j]
                size_t k = triangle_strip[i];
                size_t l = a[k];
                assert(b[l] == k); // check the backwards link is consistent
                if (l >= j) {
                    if (l > j) {
                        assert(l >= j);
                        // we need to know which element of a points to j
                        size_t o = b[j];
                        // check the forwards link is consistent
                        assert(a[o] == j);
                        // swap everything
                        std::swap(vertices[l], vertices[j]);
                        std::swap(a[k], a[o]);
                        std::swap(b[l], b[j]);
                    } else {
                        // we don't need to swap anything, it was already
                        // in the right place
                    }
                    // either way, vertex[j] is now finalized, bump j
                    ++j;
                } else {
                    // the vertex is already in the prefix thanks to
                    // an earlier triangle_strip element
                }
            }

            // unscramble the indirection so we index directly into the vertices
            for (size_t i = 0; i != m; ++i) {
                triangle_strip[i] = a[triangle_strip[i]];
                // printf("triangle_strip[%ld] == %ld\n", i, triangle_strip[i]);
            }

        }

        void MeshVertexify() {

            for (vertex v : vertices) {
                MeshVertex u = {};
                u.position = v.position;
                DUMP(u.position.x);
                u.coordinate = v.coordinate;
                u.tangent = v.tangent;
                u.bitangent = v.bitangent;
                u.normal = v.normal;
                //u.normal.xyz = normalize(cross(v.tangent.xyz, -v.bitangent.xyz));
                //u.tangent.xyz = normalize(v.tangent.xyz);
                hack_MeshVertex.push_back(u);
                for (size_t j = 0; j != 3; ++j) {
                    hack_lines.push_back(v.position);
                    hack_lines.push_back(v.position + v.jacobian.columns[j]);
                }
            }

            assert(triangle_strip.size() <= INT32_MAX);
            for (size_t i : triangle_strip) {
                hack_triangle_strip.push_back((uint) i);
            }
        }

        void copy_under_transform(float4x4 A) {
            bool mirror = determinant(A) < 0.0f;
            size_t offset = vertices.size();
            for (size_t i = 0; i != offset; ++i) {
                vertex v = vertices[i];
                v.jacobian = A * v.jacobian;
                vertices.push_back(v);
            }
            size_t n = triangles.size();
            for (size_t i = 0; i != n; ++i) {
                triangle t = triangles[i];
                for (size_t& j: t.indices)
                    j += offset;
                if (mirror)
                    t.flip();
                triangles.push_back(t);
            }
        }

        void uv_unwrap_triangles() {

            // to unwrap across sharp edges, we must consider not the exact
            // complement edge, but any edge between vertexes very close
            // to the vertices of the original edge

            // If we assume that the clustering scale is << smalles edge
            // lenth, reasonable in a mesh with variation due to numerical
            // error, we can do this by mapping indices to representatives

            ContiguousDeque<size_t> r = identify_colocated_vertices();

            // The following conditions should now hold
            //
            //     v[i].p \approxeq v[r[i]].p
            //
            //     r[i] == r[j] <==> v[i].p \approxeq v[j].p
            //
            // Though \approxeq does heavy lifting and may not be
            // transitive if clusters cross our threshold scale
            //
            // [ ] what if we compute the minimum edge length and then
            //     use this to set the length scale?

            // Using representative indices, set up the edge lookup table.
            // The directed edges are still unique.

            std::set<triangle> st;
            Table<edge, triangle> tbl;
            size_t n = triangles.size();
            for (size_t i = 0; i != n; ++i) {
                triangle t = triangles[i];
                for (size_t j = 0; j != 3; ++j) {
                    edge e = {{ r[t.indices[0]], r[t.indices[1]] }};
                    std::pair<edge, triangle> peu = { e, t };
                    auto [dummy, did_insert] = tbl.insert(peu);
                    assert(did_insert);
                    if (t.is_standard_form()) {

                    }
                    t.rotate_left();
                }
            }


            ContiguousDeque<ContiguousDeque<triangle>> groups;
            ContiguousDeque<size_t> perimeter;
            while (!st.empty()) {
                groups.emplace_back();
                triangle t = *st.begin();
                st.erase(st.begin());
                perimeter.insert(std::begin(t.indices), std::end(t.indices), perimeter.end());

            }


        }

    };

} // namespace wry::mesh

#endif /* mesh_hpp */
