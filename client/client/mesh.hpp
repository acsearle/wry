//
//  mesh.hpp
//  client
//
//  Created by Antony Searle on 17/8/2023.
//

#ifndef mesh_hpp
#define mesh_hpp

#include "ShaderTypes.h"

#include <cmath>
#include <unordered_set>
#include <set>

#include "array.hpp"
#include "image.hpp"
#include "packed.hpp"
#include "simd.hpp"
#include "table.hpp"

       
namespace wry {
    
    namespace mesh {
        
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
        
        
    } // namespace mesh3
    
    namespace mesh4 {
        

        struct iedge { int indices[2]; };
        struct itriangle { int indices[3]; };
        struct iquad { int indices[4]; };

        struct iface {
            ContiguousDeque<int> indices;
        };

        struct mesh {
            
            // normalized-database-style storage for mesh manipulation
            
            // many of the mesh operations involve building lookup structures,
            // which we could alternatively always maintain
            
            // a way to handle faces is to store as a bag of tuples,
            // (face_id, face_vertices_count)
            // (face_id, vertex_id, ith_vertex_in_face)
            
            Table<int, wry::packed::float3> positions;
            Table<int, wry::packed::float3> normals;
            Table<int, wry::packed::float3> coordinates;

            ContiguousDeque<iedge> edges;
            ContiguousDeque<itriangle> triangles;
            ContiguousDeque<iquad> quads;
            ContiguousDeque<iface> faces;
            
            Table<std::pair<int, int>, int> vertices_to_edge;
            Table<int, int> edge_to_triangle;
            Table<int, int> edge_to_quad;
            Table<int, int> edge_to_face;

        };
        
        struct point {
            simd_double4 x;
        };
        
        struct plane {
            simd_double4 x;
        };
        
        struct directed_edge {
            simd_double2x4 x;
            float4 tangent() const;
        };
        
        struct curve {
            float4 operator()(simd_double1) const;
            float4 tangent(simd_double4) const;
        };
        
        struct parametric_surface {
            simd_double4 operator()(simd_double4) const;
            simd_double4x4 jacobian(simd_double4) const;
            simd_double4x4x4 hessian(simd_double4) const;
        };
        
        struct implicit_surface {
            simd_double1 operator()(simd_double4) const;
            simd_double4 gradient(simd_double4) const;
            simd_double4x4 hessian(simd_double4) const;
        };
        
        struct parametric_cylinder {
            
            simd_double4 operator()(simd_double4 x) const {
                return {  x.y * cos(x.x),  x.y * sin(x.x),  x.z,  x.w, };
            }
            
            simd_double4x4 jacobian(simd_double4 x) const {
                return simd_double4x4{{
                    { -x.y * sin(x.x),   x.y * cos(x.x),  0.0,  0.0, },
                    {        cos(x.x),         sin(x.x),  0.0,  0.0, },
                    {             0.0,              0.0,  1.0,  0.0, },
                    {             0.0,              0.0,  0.0,  1.0, },
                }};
            }
            
            simd_double4x4x4 hessian(simd_double4 x) const {
                return simd_double4x4x4{{
                    {{
                        { -x.y * cos(x.x), -x.y * sin(x.x) },
                        { -      sin(x.x),        cos(x.x) },
                    }},
                    {{
                        { -      sin(x.x),        cos(x.x) },
                    }},
                }};
            }
            
        };
        
        struct implicit_cylinder {
            
            simd_double1 operator()(simd_double4 x) const {
                return x.x * x.x + x.y * x.y - x.w * x.w;
            }
            
            simd_double4 gradient(simd_double4 x) const {
                return {  2.0 * x.x,  2.0 * x.y,  0.0, -2.0 * x.w};
            }
            
            simd_double4x4 hessian(simd_double4 x) const {
                return {{
                    {  2.0,  0.0,  0.0,  0.0, },
                    {  0.0,  2.0,  0.0,  0.0, },
                    {  0.0,  0.0,  0.0,  0.0, },
                    {  0.0,  0.0,  0.0, -2.0, },
                }};
            }
            
        };
        
        
        struct parametric_sphere {
            
            simd_double4 operator()(simd_double4 x) const {
                return {
                    sin(x.x) * cos(x.y) * x.z * x.w,
                    sin(x.y) * x.z * x.w,
                    cos(x.x) * cos(x.y) * x.z * x.w,
                    x.w,
                };
            }
            
            simd_double4x4 jacobian(simd_double4 x) const {
                return {{
                    {  cos(x.x) * cos(x.y) * x.z * x.w,                   0.0, -sin(x.x) * cos(x.y) * x.z * x.w,  0.0, },
                    { -sin(x.x) * sin(x.y) * x.z * x.w,  cos(x.y) * x.z * x.w, -cos(x.x) * sin(x.y) * x.z * x.w,  0.0, },
                    {  sin(x.x) * cos(x.y) *       x.w,  sin(x.y) *       x.w,  cos(x.x) * cos(x.y)       * x.w,  0.0, },
                    {  sin(x.x) * cos(x.y) * x.z      ,  sin(x.y) * x.z      ,  cos(x.x) * cos(x.y) * x.z      ,  0.0, },
                }};
            }
            
        };
        
        struct implicit_sphere {
            
            simd_double1 operator()(simd_double4 x) const {
                return x.x * x.x + x.y * x.y + x.z * x.z - x.w * x.w;
            }
            
            simd_double4 gradient(simd_double4 x) const {
                return {  2.0 * x.x,  2.0 * x.y,  2.0 * x.z, -2.0 * x.w };
            }
            
            simd_double4x4 hessian(simd_double4 x) const {
                return {{
                    {  2.0,  0.0,  0.0,  0.0 },
                    {  0.0,  2.0,  0.0,  0.0 },
                    {  0.0,  0.0,  2.0,  0.0 },
                    {  0.0,  0.0,  0.0, -2.0 },
                }};
            }
            
        };
        
        using namespace ::simd;
        
        // for CSG we need to determine mesh interiors; we can use ray
        // intersection for this (when the interior is well defined)
        
        // mirror the Metal structures
        
        struct ray {
            double3 origin;
            double3 direction;
            double min_distance;
            double max_distance;
        };
        
        struct intersection_result {
            // intersetcion_type type;
            double distance;
            double2 triangle_barycentric_coord;
            bool triangle_front_facing;
        };
        
        struct triangle {
            double3x3 vertices;
            
            bool intersect(ray& r, intersection_result& s) {
                // Möller–Trumbore
                
                double3 v0 = vertices.columns[0];
                double3 v1 = vertices.columns[1];
                double3 v2 = vertices.columns[2];
                
                // r.origin + t*r.direction = (1 - u - v) v0 + u v1 + v v2
                // (r.origin-x) + t*r.direction = u*(y-x) + v*z(-x)
                
                double3 d = r.direction;
                double3 o = r.origin - v0;
                double3 e0 = v1 - v0;
                double3 e1 = v2 - v0;
                
                // o = [-d, e0, e1] * [t u v]
                
                double3 h = cross(d, e1); // directed area
                double a = dot(h, e0); // signed volume
                
                if ((-1e-7 < a) && (a < 1e-7))
                    // parallel
                    return false;
                double f = 1.0 / a;
                double2 uv;
                uv.x = f * dot(o, h);
                if (uv.x < 0.0f || uv.x > 1.0f)
                    // out of strip
                    return false;
                // inside strip
                double3 q = cross(o, e0);
                uv.y = f * dot(d, q);
                if ((uv.y < 0.0f) || (uv.x + uv.y > 1.0f))
                    // out of triangle
                    return false;
                // line intersects triangle
                double t = f * dot(e1, q);
                if ((t < r.min_distance) || (r.max_distance < t))
                    // before or after ray
                    return false;
                // ray intersects triangle
                s.triangle_front_facing = a < 0;
                s.triangle_barycentric_coord = uv;
                return true;

            }
            
        };
        
        
        
    } // namespace mesh4
    
} // namespace wry


namespace wry {
        
    auto min3(auto&& a, auto&& b, auto&& c) {
        return b < a ? c < b ? fwd(c) : fwd(b) : c < a ? fwd(c) : fwd(a);
    }

    auto median3(auto&& a, auto&& b, auto&& c) {
        if (b < a) {
            if (c < b) {
                // b < a && c < b : c < b < a
                return b;
            } else if (c < a) {
                // b < a && b <= c && c < a : b <= c < a
                return c;
            } else {
                // b < a && b <= c && a <= c : b < a <= c
                return a;
            }
        } else {
            if (c < a) {
                // a <= b && c < a : c < a <= b
                return a;
            } else if (c < b) {
                // a <= b && a <= c && c < b : a <= c < b
                return c;
            } else {
                // a <= b && a <= c && b <= c : a <= b <= c
                return b;
            }
            
        }
    }
    
    auto sort3(auto&& a, auto&& b, auto&& c) {
        if (b < a) {
            if (c < b) {
                // b < a && c < b : c < b < a
                return std::tuple(c, b, a);
            } else if (c < a) {
                // b < a && b <= c && c < a : b <= c < a
                return std::tuple(b, c, a);
            } else {
                // b < a && b <= c && a <= c : b < a <= c
                return std::tuple(b, a, c);
            }
        } else {
            if (c < a) {
                // a <= b && c < a : c < a <= b
                return std::tuple(c, a, b);
            } else if (c < b) {
                // a <= b && a <= c && c < b : a <= c < b
                return std::tuple(a, c, b);
            } else {
                // a <= b && a <= c && b <= c : a <= b <= c
                return std::tuple(a, b, c);
            }
            
        }
    }

    
    inline simd_float3 closest_axis(simd_float3 a) {
        simd_float3 b = simd_abs(a);
        if (b.x < b.y) {
            if (b.y < b.z) {
                a.xy = 0.0f;
            } else {
                a.xz = 0.0f;
            }
        } else {
            if (b.x < b.z) {
                a.xy = 0.0f;
            } else {
                a.yz = 0.0f;
            }
        }
        return a;
    }
    
    // compute two vectors orthogonal to v
    inline simd_float2x3 perpendicular(simd_float3 v) {
        simd_float3 u = closest_axis(v);
        simd_quatf q = simd_quaternion(u, v);
        return {{
            simd_act(q, u.yzx),
            simd_act(q, u.zxy),
        }};
    }
    
    
    struct interval {
        simd_double2 inner;
        
        interval operator+(interval other) {
            return { inner + other.inner };
        }
        
        interval operator-(interval other) {
            return { inner - other.inner.yx };
        }
        
        interval operator*(double other) {
            return (other < 0) ? interval{ inner.yx * other } : interval{ inner * other };
        }
        
    };
    
    
    struct aabb {
        simd_double4 a, b;
    };
    
    struct differential {
        simd_double4 value;
        simd_double4x4 jacobian;
        simd_double4x4x4 hessian;
    };
    
    struct rational {
        int64_t numerator;
        int32_t denominator;
        int32_t exponent2;
        
        void _assert_invariant() const {
            assert((numerator & 1) || (!numerator));
            assert(denominator & 1);
            assert(std::gcd(numerator, denominator) == 1);
        }
        
        void _repair_invariant() {
            abort();
            /*
            int a = __builtin_ctzll(numerator);
            numerator >>= a;
            exponent2 += a;
            int b = __builtin_ctzll(denominator);
            denominator >>= b;
            exponent2 -= b;
            int64_t c = std::gcd(numerator, denominator);
            a /= c;
            b /= c;
            _assert_invariant();
             */
        }

        rational operator*(rational other) {
            auto a = std::gcd(numerator, other.denominator);
            auto b = std::gcd(other.numerator, denominator);
            rational c{
                (numerator / a) * (other.numerator / b),
                denominator = (denominator / b) * (other.denominator / b),
                exponent2 + other.exponent2,
            };
            c._assert_invariant();
            return c;
        }
        
        rational operator+(rational other) {
            auto a = std::gcd(denominator, other.denominator);
            rational c{
                numerator * (other.denominator / a) + other.numerator * (denominator / a),
                denominator * (other.denominator / a),
            };
            _repair_invariant();
            return c;
        }
    };
    
}


#if 0

// the mesh is the surface of some function
//
// f(coordinate) -> position
//
// sampled at discrete coordinates and also storing the local
// derivative

// there are two possible transformations we can insert, one
// changing the coordinate before f
//
// f(g^-1(coordinate)) -> position
//
// and one changing the position after f
//
// h(f(coordinate)) -> position

// the latter is of most interest
//
// we can compute del h = Jh
//
// and we can already have del f

// The Jacobians will combine, so it is sufficient to get the
// Jacobian of the transform

// however, many transforms are better expressed as
//
// h(f(coordinate), coordinate) -> position
//
// suppose we perturb a circle out of the XY plane, then sweep
// it into a near-cylinder; if then rotate in proportion to Z, then
// the original slice appears nowhere

// consider this rotation around y
//
// { cos(phi), 0, sin(phi), 0 }
// { 0       , 1,        0, 0 } = A
// {-sin(phi), 0, cos(phi), 0 }
// { 0       , 0,        0, 1 }
//
// We have the simple expression that
//
// d/dx (A(phi)*x) = A(phi)
//
// but we actually want
//
// d/dc (A(phi(c)) * f(c))
//
// so, product rule
//
// = (d/dc A(theta(c))) * f(c0) + A(theta(c0)) * d/dc f(c)
//
// chain rule
//
// d/dc A(theta(c))
//
// = (d/dtheta A(theta0)) * dtheta / dc

// what is it in this case?
//
// { sin(phi), 0, -cos(phi), 0 }
// { 0, 0, 0, 0 }
// { cos(phi), 0, sin(phi), 0 }
// { 0, 0, 0, 0 }
//
// but it's actually a tensor with the other planes zeroed, in this case !


/*
 void extrude(vector_float4 delta = vector4(0.0f, 1.0f, 0.0f, 0.0f)) {
 // step one, copy existing vertices
 size_t offset = vertices.size();
 for (int i = 0; i != vertices.size(); ++i) {
 vertex v = vertices[i];
 target.vertices.push_back(v);
 }
 // step two, construct new vertices
 for (int i = 0; i != vertices.size(); ++i) {
 vertex v = vertices[i];
 matrix_float4x4 A = simd_matrix(v.tangent,
 v.bitangent,
 v.normal,
 v.position);
 v.position += matrix_multiply(A, delta);
 v.coordinate += delta;
 }
 // step three, link vertices
 for (int i = 0; i != edges.size(); ++i) {
 
 edge e = edges[i];
 face f;
 edge g;
 
 int j0 = e.indices[0] + offset;
 int j1 = e.indices[1] + offset;
 int j2 = e.indices[1] + offset + (int) vertices.size();
 int j3 = e.indices[0] + offset + (int) vertices.size();
 
 f.indices.push_back(j0);
 f.indices.push_back(g);
 f.indices.push_back(g);
 
 g.ivertices[0] = j3;
 g.ivertices[1] = j0;
 f.iedges.push_back(g);
 
 target.ifaces.push_back(std::move(f));
 
 }
 
 }
 */




/*
 struct half {
 
 ushort s;
 
 explicit half(float f) {
 uint i;
 memcpy(&i, &f, 4);
 // 16: 1:5:10
 // 32: 1:8:23
 s = (((i & 0x80000000) >> 16)
 | ((i & 0x0FFFE000) >> 13));
 }
 
 explicit operator float() const {
 uint i = (((s & 0x00008000) << 16)
 | ((s & 0x00003FFF) << 13)
 | ((s & 0x00004000) * 0x0001C000));
 float f;
 memcpy(&f, &i, 4);
 return f;
 }
 
 };
 */


// todo:
//
// indexed primitives
//

struct mesh {
    
    static Array<float4> clip_space_quad() {
        Array<float4> v = {
            make<float4>(-1.0f, -1.0f, 0.0f, 1.0f),
            make<float4>(-1.0f, +1.0f, 0.0f, 1.0f),
            make<float4>(+1.0f, +1.0f, 0.0f, 1.0f),
            make<float4>(-1.0f, -1.0f, 0.0f, 1.0f),
            make<float4>(+1.0f, +1.0f, 0.0f, 1.0f),
            make<float4>(+1.0f, -1.0f, 0.0f, 1.0f),
        };
        return v;
    }
    
    static Array<MeshVertex> add_normals(simd_float3x3* first, simd_float3x3* last) {
        Array<MeshVertex> v;
        
        for (; first != last; ++first) {
            auto& t = *first;
            for (int i = 0; i != 3; ++i) {
                // simd_float3 normal = simd_normalize(simd_cross(t.columns[1] - t.columns[0], t.columns[2] - t.columns[1]));
                
                simd_float3 position = t.columns[i];
                
                // define texture coordinates and the surface coordinate
                // system by parallel transport on great circles, i.e
                // azimuthal
                
                // texCoord from position
                float r = simd_length(position.xy);
                float phi = atan2(position.y, position.x);
                float theta = atan2(r, position.z);
                float theta2 = theta;
                //if (theta > M_PI / 2)
                //    theta2 = M_PI - theta;
                float2 texCoord = (theta2 / M_PI) * simd_make_float2(cos(phi), -sin(phi)) * 0.5f + 0.5f;
                
                // interestingly, the quaternion's 4 floats is enough to
                // encode the whole tangent-bitangent-normal coordinate
                // system, since they are the columns of a rotation matrix
                
                simd_quatf q = simd_quaternion(theta, simd_make_float3(-sin(phi), cos(phi), 0.0f));
                float4x4 A = simd_matrix4x4(q);
                
                float4 normal = A.columns[2];
                float4 tangent = A.columns[0];
                
                
                /*
                 v.push_back(MeshVertex{
                 make<float4>(position, 1.0f),
                 texCoord,
                 normal,
                 tangent,
                 });
                 */
            }
        }
        
        return v;
    }
    
    static auto tesselate(Array<simd_float3x3>& x) {
        auto n = x.size();
        while (n--) {
            simd_float3x3 a = x.front();
            x.pop_front();
            simd_float3x3 b = simd_matrix(simd_normalize(a.columns[0] + a.columns[1]),
                                          simd_normalize(a.columns[1] + a.columns[2]),
                                          simd_normalize(a.columns[2] + a.columns[0]));
            x.push_back(simd_matrix(a.columns[0], b.columns[0], b.columns[2]));
            x.push_back(simd_matrix(a.columns[1], b.columns[1], b.columns[0]));
            x.push_back(simd_matrix(a.columns[2], b.columns[2], b.columns[1]));
            x.push_back(simd_matrix(b.columns[0], b.columns[1], b.columns[2]));
        }
    }
    
    static auto icosahedron() {
        
        const double b = 1 / sqrt(5);
        const double c = 2 * b;
        const double d = (1 - b) / 2;
        const double e = (1 + b) / 2;
        const double f = sqrt(d);
        const double g = sqrt(e);
        
        simd_double3 u[12] = {
            { 1, 0, 0 },
            { b, c, 0 },
            { b, d, g },
            { b, -e, f },
            { b, -e, -f },
            { b, d, -g },
            { -b, e, f },
            { -b, -d, g },
            { -b, -c, 0 },
            { -b, -d, -g },
            { -b, e, -f },
            { -1, 0, 0 },
        };
        
        simd_float3 v[12];
        for (int i = 0; i != 12; ++i)
            v[i] = simd_make_float3(u[i].z, u[i].x, u[i].y);
        
        Array<simd_float3x3> w;
        for (int i = 0, j; i != 5; ++i) {
            j = i + 1;
            if (j == 5)
                j = 0;
            //
            //     0   0   0   0   0
            //    / \ / \ / \ / \ / \
            //   1---2---3---4---5---1  1+i 1+j
            //    \ / \ / \ / \ / \ / \
            //     6---7---8---9--10---6  6+i 6+j
            //      \ / \ / \ / \ / \ /
            //      11  11  11  11  11
            //
            w.push_back(simd_matrix(v[0], v[1 + i], v[1 + j]));
            w.push_back(simd_matrix(v[1 + j], v[1 + i], v[6 + i]));
            w.push_back(simd_matrix(v[6 + i], v[6 + j], v[1 + j]));
            w.push_back(simd_matrix(v[6 + j], v[6 + i], v[11]));
        }
        
        //tesselate(w);
        tesselate(w);
        tesselate(w);
        tesselate(w);
        tesselate(w);
        
        
        return add_normals(w.begin(), w.end());
        
        
    }
    
    static auto prism(int n) {
        
        // make a prism:
        
        // vertices of bottom and top n-gon
        Array<simd_float3> a, b;
        
        for (double i = 0.5; i < n; i += 1.0) {
            double theta = 2 * M_PI * i / n;
            float c = cos(theta);
            float s = sin(theta);
            a.push_back(simd_make_float3(c, s, 0.0f));
            theta = 2 * M_PI * (i - 0.5) / n;
            c = cos(theta);
            s = sin(theta);
            b.push_back(simd_make_float3(c, s, 1.0f));
        }
        
        Array<simd_float3x3> c;
        for (int i = 1; i != n-1; ++i) {
            c.push_back(simd_matrix(a[0], a[i+1], a[i]));
            c.push_back(simd_matrix(b[0], b[i], b[i+1]));
        }
        
        for (int i = 0;; ++i) {
            int j = i + 1;
            if (j == n)
                j = 0;
            c.push_back(simd_matrix(a[i], a[j], b[j]));
            c.push_back(simd_matrix(b[j], b[i], a[i]));
            if (j == 0)
                break;
        }
        
        return add_normals(c.begin(), c.end());
        
    }
    
};


/*
 struct mesh {
 
 Array<simd_float3> _vertices;
 Array<int> _indices;
 
 static mesh box(simd_float3 a, simd_float3 b) {
 
 Array<simd_float3> v({
 simd::select(a, b, simd_make_int3(0, 0, 0)),
 simd::select(a, b, simd_make_int3(0, 0, 1)),
 simd::select(a, b, simd_make_int3(0, 1, 0)),
 simd::select(a, b, simd_make_int3(0, 1, 1)),
 simd::select(a, b, simd_make_int3(1, 0, 0)),
 simd::select(a, b, simd_make_int3(1, 0, 1)),
 simd::select(a, b, simd_make_int3(1, 1, 0)),
 simd::select(a, b, simd_make_int3(1, 1, 1)),
 });
 
 Array<int> u({
 0, 1, 2, 2, 1, 3,
 0, 5, 1, 1, 5, 4,
 0, 6, 2, 2, 6, 4,
 1, 7, 3, 3, 7, 6,
 2, 6, 3, 3, 6, 7,
 4, 6, 5, 5, 6, 7,
 });
 
 return mesh{v, u};
 }
 
 };
 */

namespace mesh2 {
    
    //
    //     vertex
    //     edge
    //     face
    //     cell
    //
    //     peak
    //     ridge
    //     facet
    //     polytope
    
    
    using vertex = float4;
    
    using face = Array<vertex>;
    
    
    // flat-sided object in 3 dimensions
    
    // we don't assume that the vertices form a closed loop, relying on
    // the last point being a duplicate of the first if this is so
    
    struct triangulation {
        
        Array<vertex> vertices;
        
        
        static triangulation clip_space_quad() {
            Array<vertex> v = {
                make<float4>(-1.0f, -1.0f, 0.0f, 1.0f),
                make<float4>(-1.0f, +1.0f, 0.0f, 1.0f),
                make<float4>(+1.0f, +1.0f, 0.0f, 1.0f),
                make<float4>(-1.0f, -1.0f, 0.0f, 1.0f),
                make<float4>(+1.0f, +1.0f, 0.0f, 1.0f),
                make<float4>(+1.0f, -1.0f, 0.0f, 1.0f),
            };
            return triangulation{std::move(v)};
        }
        
        
        triangulation& tesselate(size_t m = 1) {
            while (m--) {
                size_t n = vertices.size();
                assert(n % 3 == 0);
                for (size_t i = 0; i != n; i += 3) {
                    
                    vertex a = vertices[0];
                    vertex b = vertices[1];
                    vertex c = vertices[2];
                    vertices.erase_n(vertices.begin(), 3);
                    vertex ab = (a + b) * 0.5f;
                    vertex bc = (b + c) * 0.5f;
                    vertex ca = (c + a) * 0.5f;
                    vertices.push_back(a);
                    vertices.push_back(ab);
                    vertices.push_back(ca);
                    vertices.push_back(b);
                    vertices.push_back(bc);
                    vertices.push_back(ab);
                    vertices.push_back(c);
                    vertices.push_back(ca);
                    vertices.push_back(bc);
                    vertices.push_back(ab);
                    vertices.push_back(bc);
                    vertices.push_back(ca);
                }
            }
            return *this;
        }
        
    };
    
    struct polygon {
        
        Array<vertex> vertices;
        
        size_t size() const {
            return vertices.size();
        }
        
        // cyclic index
        vertex& operator[](size_t i) {
            return vertices[i % size()];
        }
        
        // cyclic erase
        void erase(size_t i) {
            vertices.erase(vertices.begin() + (i % size()));
        }
        
        // append
        void push_back(vertex x) {
            vertices.push_back(x);
        }
        
        static polygon regular(int n, float phase = 0.0f) {
            
            polygon p;
            
            for (int i = 0; i != n; ++i) {
                float theta = (i + phase) * 2 * M_PI / n;
                float x = cos(theta);
                float y = sin(theta);
                p.push_back(make<float4>(x, y, 0.0f, 1.0f));
            }
            
            return p;
            
        }
        
        polygon& reverse() {
            std::reverse(vertices.begin(),
                         vertices.end());
            return *this;
        }
        
        polygon& apply(float4x4 transform) {
            for (auto& x : vertices) {
                x = simd_mul(transform, x);
            }
            return *this;
        }
        
        simd_float3 area() {
            auto& v = *this;
            simd_float3 c = 0;
            for (size_t i = 0; i != size(); ++i)
                c += simd_cross(v[i].xyz, v[i + 1].xyz);
            return c;
        }
        
        simd_float3 normal() {
            return simd_normalize(area());
        }
        
        float4 centroid() {
            auto& v = *this;
            auto n = area();
            n /= simd_length_squared(n);
            n /= 3.0f;
            float4 centroid = 0;
            for (size_t i = 0; i != size(); ++i) {
                auto a = simd_cross(v[i].xyz, v[i + 1].xyz);
                float b = simd_dot(n, a);
                float4 c = v[i] + v[i + 1];
                centroid += b * c;
            }
            return centroid;
        }
        
        void stellate(float factor = 2.0f) {
            auto& v = *this;
            auto n = normal() * factor;
            Array<vertex> z;
            for (size_t i = 0; i != vertices.size(); ++i) {
                simd_float3 a = v[i].xyz;
                simd_float3 b = v[i + 1].xyz;
                simd_float3 c = simd_cross(b - a, n) + (b + a) * 0.5f;
                z.push_back(make<float4>(a, 1.0f));
                z.push_back(make<float4>(c, 1.0f));
            }
            vertices.swap(z);
        }
        
        void truncate(float factor = 1.0 / 3.0f) {
            auto& v = *this;
            Array<vertex> z;
            for (size_t i = 0; i != vertices.size(); ++i) {
                auto a = v[i];
                auto b = v[i + 1];
                z.push_back(simd_mix(a, b, factor));
                z.push_back(simd_mix(a, b, 1.0f - factor));
            }
            vertices.swap(z);
        }
        
        
        void triangulate(triangulation& target ) {
            auto& v = *this;
            /*
             // emit a simple triangle fan
             for (size_t i = 1; i + 1 < vertices.size(); ++i) {
             target.vertices.push_back(vertices[0]);
             target.vertices.push_back(vertices[i]);
             target.vertices.push_back(vertices[i + 1]);
             }
             */
            // ear clipping method
            auto foo = [&](size_t i) {
                simd_float3 a = v[i].xyz;
                simd_float3 b = v[i + 1].xyz;
                simd_float3 c = v[i + 2].xyz;
                return simd_cross(b - a, c - b);
            };
            simd_float3 n = area();
            while (vertices.size() >= 3) {
                float best_metric = -INFINITY;
                size_t best_i = -1;
                for (size_t i = 0; i != size(); ++i) {
                    auto m = foo(i);
                    auto metric = simd_dot(n, m);
                    if (metric > best_metric) {
                        best_metric = metric;
                        best_i = i;
                    }
                }
                printf("%g\n", best_metric);
                // emit the associated triangle
                size_t i = best_i;
                target.vertices.push_back(v[i]);
                target.vertices.push_back(v[i + 1]);
                target.vertices.push_back(v[i + 2]);
                
                v.erase(i + 1);
                
            }
            
        }
        
        
    };
    
    struct polyhedron {
        
        Array<polygon> faces;
        
        void _join_with_triangles(polygon& a,
                                  polygon& b) {
            size_t n = a.size();
            assert(b.size() == n);
            for (size_t i = 0; i != n; ++i) {
                polygon face;
                face.push_back(a[i]);
                face.push_back(a[i + 1]);
                face.push_back(b[i]);
                faces.push_back(face);
                face.vertices.clear();
                face.push_back(a[i + 1]);
                face.push_back(b[i + 1]);
                face.push_back(b[i]);
                faces.push_back(std::move(face));
            }
        }
        
        void _join_with_quads(polygon& a,
                              polygon& b) {
            size_t n = a.size();
            assert(b.size() == n);
            for (size_t i = 0; i != n; ++i) {
                polygon face;
                face.push_back(a[i]);
                face.push_back(a[i + 1]);
                face.push_back(b[i + 1]);
                face.push_back(b[i]);
                faces.push_back(face);
            }
        }
        
        static polyhedron antiprismatoid(polygon bottom, polygon top) {
            polyhedron result;
            size_t n = top.vertices.size();
            assert(bottom.vertices.size() == n);
            result.faces.push_back(bottom);
            bottom.reverse();
            result._join_with_triangles(bottom, top);
            result.faces.push_back(top);
            return result;
        }
        
        static polyhedron prismatoid(polygon bottom, polygon top) {
            polyhedron result;
            size_t n = top.vertices.size();
            assert(bottom.vertices.size() == n);
            result.faces.push_back(bottom);
            bottom.reverse();
            result._join_with_quads(bottom, top);
            result.faces.push_back(top);
            return result;
        }
        
        static polyhedron prismatoid(polygon base, float4x4 transform) {
            polygon top = base;
            top.apply(transform);
            polygon bottom = base;
            bottom.reverse();
            return prismatoid(std::move(bottom), std::move(top));
        }
        
        static polyhedron prism(polygon base) {
            return prismatoid(std::move(base), simd_matrix_translate(simd_make_float3(0, 0, 1)));
        }
        
        static polyhedron prism(int n, float phase = 0.0) {
            return prism(polygon::regular(n, phase));
        }
        
        static polyhedron frustum(polygon base, float scale) {
            float4x4 A = simd_matrix_scale(simd_make_float3(scale, scale, scale));
            float4x4 B = simd_matrix_translate(simd_make_float3(0, 0, 1));
            return prismatoid(base, simd_mul(B, A));
        }
        
        static polyhedron extrusion(polygon base, int n, float4x4 transform) {
            // fixme: repeated application of the transform will lead to
            // accumulation of numerical error
            polyhedron result;
            polygon a;
            
            // bottom cap
            a = base;
            result.faces.push_back(a.reverse());
            
            // bands joining slices of cumulative transform
            for (int i = 0; i != n; ++i) {
                a = base;
                a.apply(transform);
                result._join_with_quads(base, a);
                base = a;
            }
            result.faces.push_back(a);
            return result;
        }
        
        static polyhedron icosahedron() {
            
            //
            //     0   0   0   0   0
            //    / \ / \ / \ / \ / \
            //   1---2---3---4---5---1  1+i 1+j
            //    \ / \ / \ / \ / \ / \
            //     6---7---8---9--10---6  6+i 6+j
            //      \ / \ / \ / \ / \ /
            //      11  11  11  11  11
            //
            
            const float b = 1 / sqrt(5);
            const float c = 2 * b;
            const float d = (1 - b) / 2;
            const float e = (1 + b) / 2;
            const float f = sqrt(d);
            const float g = sqrt(e);
            
            float4 u[12] = {
                { 1, 0, 0, 1 },
                { b, c, 0, 1 },
                { b, d, g, 1 },
                { b, -e, f, 1 },
                { b, -e, -f, 1 },
                { b, d, -g, 1 },
                { -b, e, f, 1 },
                { -b, -d, g, 1 },
                { -b, -c, 0, 1 },
                { -b, -d, -g, 1 },
                { -b, e, -f, 1 },
                { -1, 0, 0, 1 },
            };
            
            polyhedron result;
            
            auto foo = [&](ptrdiff_t i, ptrdiff_t j, ptrdiff_t k) {
                polygon face;
                face.vertices.push_back(u[i]);
                face.vertices.push_back(u[k]);
                face.vertices.push_back(u[j]);
                result.faces.push_back(std::move(face));
            };
            
            foo(0, 2, 1);
            foo(0, 3, 2);
            foo(0, 4, 3);
            foo(0, 5, 4);
            foo(0, 1, 5);
            
            foo(1, 2, 6);
            foo(2, 3, 7);
            foo(3, 4, 8);
            foo(4, 5, 9);
            foo(5, 1, 10);
            
            foo(2, 7, 6);
            foo(3, 8, 7);
            foo(4, 9, 8);
            foo(5, 10, 9);
            foo(1, 6, 10);
            
            foo(6, 7, 11);
            foo(7, 8, 11);
            foo(8, 9, 11);
            foo(9, 10, 11);
            foo(10, 6, 11);
            
            return result;
            
        }
        
        void triangulate(triangulation& target) {
            // destructively convert to a triangulation
            for (polygon& face : faces)
                face.triangulate(target);
        }
        
        void apply(float4x4 transform) {
            for (polygon& face : faces)
                face.apply(transform);
        }
        
        void stellate(float factor) {
            polyhedron result;
            for (auto& face : faces) {
                // this is actually the pyramid construction, hmm
                auto a = face.area();
                auto b = simd_length(a);
                a /= sqrt(b);
                auto c = face.centroid();
                auto p = c + make<float4>(a * factor, 0);
                for (size_t i = 0; i != face.size(); ++i) {
                    polygon g;
                    g.push_back(p);
                    g.push_back(face[i]);
                    g.push_back(face[i+1]);
                    result.faces.push_back(std::move(g));
                }
            }
            std::swap(faces, result.faces);
        }
        
    };
    
    
    struct mesh {
        
        Array<MeshVertex> vertices;
        
        void position_from(triangulation& victim) {
            
            size_t n = victim.vertices.size();
            vertices.resize(n);
            for (size_t i = 0; i != n; ++i) {
                vertices[i].position = victim.vertices[i];
            }
            
        }
        
        void normal_from_position() {
            for (auto& v : vertices) {
                v.normal.xyz = simd_normalize(v.position.xyz);
                v.normal.w = 0.0f;
            }
        }
        
        void normal_from_triangle() {
            for (size_t i = 0; i < vertices.size(); i += 3) {
                simd_float3 a = vertices[i + 0].position.xyz;
                simd_float3 b = vertices[i + 1].position.xyz;
                simd_float3 c = vertices[i + 2].position.xyz;
                float4 n;
                n.xyz = simd_normalize(simd_cross(b - a, c - b));
                n.w = 0.0f;
                vertices[i + 0].normal = n;
                vertices[i + 1].normal = n;
                vertices[i + 2].normal = n;
            }
        }
        
        void texcoord_from_position(float4x4 transform = matrix_identity_float4x4) {
            for (auto& v : vertices) {
                v.texCoord = simd_mul(transform, v.position).xy;
            }
        }
        
        void texcoord_from_normal(float4x4 transform = matrix_identity_float4x4) {
            // correctly maps flat surfaces of any orientation, but not
            // across non-flat edges; curves will be weird
            for (auto& v : vertices) {
                // find the quaterion that rotates normal to +z
                auto q = simd_quaternion(v.normal.xyz, simd_make_float3(0, 0, 1));
                v.texCoord = simd_act(q, v.position.xyz).xy;
            }
        }
        
        void tangent_from_texcoord() {
            for (size_t i = 0; i < vertices.size(); i += 3) {
                
                // define local texture coordinate transform A as
                //
                //    P = A * C
                //    A = P * C^-1
                //
                // where C =
                
                simd_float2x2 C = matrix_identity_float2x2;
                C.columns[0] = vertices[i + 1].texCoord - vertices[i].texCoord;
                C.columns[1] = vertices[i + 2].texCoord - vertices[i].texCoord;
                
                simd_float2x4 P;
                P.columns[0] = vertices[i + 1].position - vertices[i].position;
                P.columns[1] = vertices[i + 2].position - vertices[i].position;
                
                simd_float2x4 A = simd_mul(P, simd_inverse(C));
                
                float4 tangent = A.columns[0];
                
                vertices[i + 0].tangent = simd_normalize(tangent);
                vertices[i + 1].tangent = simd_normalize(tangent);
                vertices[i + 2].tangent = simd_normalize(tangent);
                
            }
        }
        
        void normals_from_average(float distance2, float sinTheta) {
            // step one, replace with angle-weighted normals
            auto foo = [n = (int) vertices.size()](int i) {
                int m = i % 3;
                int r = i - m;
                int j = r + (m + 1) % 3;
                int k = r + (m + 2) % 3;
                return std::make_pair(j, k);
            };
            for (int i = 0; i != vertices.size(); ++i) {
                auto [j, k] = foo(i);
                simd_float3 a = vertices[i].position.xyz;
                simd_float3 b = vertices[j].position.xyz;
                simd_float3 c = vertices[k].position.xyz;
                simd_float3 ba = b - a;
                simd_float3 ac = c - a;
                ba = simd_normalize(ba);
                ac = simd_normalize(ac);
                simd_float3 n = simd_cross(ba, ac); // proportional to subtended angle
                vertices[i].normal.xyz = n;
            }
            
            
            
            Array<int> family;
            table<int, char> done;
            for (int i = 0; i != vertices.size(); ++i) {
                if (done.contains(i))
                    continue;
                done[i];
                family.clear();
                family.push_back(i);
                float4 a = vertices[i].position;
                simd_float3 n = vertices[i].normal.xyz;
                simd_float3 ni = simd_normalize(n);
                for (int j = i + 1; j != vertices.size(); ++j) {
                    if (done.contains(j))
                        continue;
                    float4 b = vertices[j].position;
                    if (simd_distance_squared(a, b) > distance2)
                        continue;
                    simd_float3 m = vertices[j].normal.xyz;
                    simd_float3 nj = simd_normalize(m);
                    if (simd_dot(ni, nj) < sinTheta)
                        continue;
                    done[j];
                    family.push_back(j);
                    n += m;
                }
                n = simd_normalize(n);
                for (int j : family) {
                    vertices[j].normal.xyz = n;
                }
            }
        }
        
    };
    
    
    struct indexed_mesh {
        
        Array<MeshVertex> vertices;
        Array<int> indices; // ints are supposed to be good for metal
        
        void indices_from_iota() {
            indices.resize(vertices.size());
            std::iota(indices.begin(), indices.end(), 0);
        }
        
        
        
        
    };
    
    
    
} // namespace mesh2


#endif

#endif /* mesh_hpp */
