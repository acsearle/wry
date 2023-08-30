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

#include "array.hpp"
#include "image.hpp"
#include "simd.hpp"



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

struct r8Unorm_sRGB {
    unsigned char c;
};
                       
namespace wry {
    
    // todo:
    //
    // indexed primitives
    //
    
    struct mesh {
        
        static array<simd_float4> clip_space_quad() {
            array<simd_float4> v = {
                simd_make_float4(-1.0f, -1.0f, 0.0f, 1.0f),
                simd_make_float4(-1.0f, +1.0f, 0.0f, 1.0f),
                simd_make_float4(+1.0f, +1.0f, 0.0f, 1.0f),
                simd_make_float4(-1.0f, -1.0f, 0.0f, 1.0f),
                simd_make_float4(+1.0f, +1.0f, 0.0f, 1.0f),
                simd_make_float4(+1.0f, -1.0f, 0.0f, 1.0f),
            };
            return v;
        }
        
        static array<MeshVertex> add_normals(simd_float3x3* first, simd_float3x3* last) {
            array<MeshVertex> v;
            
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
                    simd_float2 texCoord = (theta2 / M_PI) * simd_make_float2(cos(phi), -sin(phi)) * 0.5f + 0.5f;
                    
                    // interestingly, the quaternion's 4 floats is enough to
                    // encode the whole tangent-bitangent-normal coordinate
                    // system, since they are the columns of a rotation matrix
                    
                    simd_quatf q = simd_quaternion(theta, simd_make_float3(-sin(phi), cos(phi), 0.0f));
                    simd_float4x4 A = simd_matrix4x4(q);
                    
                    simd_float4 normal = A.columns[2];
                    simd_float4 tangent = A.columns[0];

                    
                    v.push_back(MeshVertex{
                        simd_make_float4(position, 1.0f),
                        texCoord,
                        normal,
                        tangent,
                    });
                }
            }
            
            return v;
        }
        
        static auto tesselate(array<simd_float3x3>& x) {
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

            array<simd_float3x3> w;
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
            array<simd_float3> a, b;
            
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
            
            array<simd_float3x3> c;
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
     
     array<simd_float3> _vertices;
     array<int> _indices;
     
     static mesh box(simd_float3 a, simd_float3 b) {
     
     array<simd_float3> v({
     simd_select(a, b, simd_make_int3(0, 0, 0)),
     simd_select(a, b, simd_make_int3(0, 0, 1)),
     simd_select(a, b, simd_make_int3(0, 1, 0)),
     simd_select(a, b, simd_make_int3(0, 1, 1)),
     simd_select(a, b, simd_make_int3(1, 0, 0)),
     simd_select(a, b, simd_make_int3(1, 0, 1)),
     simd_select(a, b, simd_make_int3(1, 1, 0)),
     simd_select(a, b, simd_make_int3(1, 1, 1)),
     });
     
     array<int> u({
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
        
        
        using vertex = simd_float4;
                
        using face = array<vertex>;
        

        // flat-sided object in 3 dimensions
        
        // we don't assume that the vertices form a closed loop, relying on
        // the last point being a duplicate of the first if this is so
        
        struct triangulation {
           
            array<vertex> vertices;
            
            
            static triangulation clip_space_quad() {
                array<vertex> v = {
                    simd_make_float4(-1.0f, -1.0f, 0.0f, 1.0f),
                    simd_make_float4(-1.0f, +1.0f, 0.0f, 1.0f),
                    simd_make_float4(+1.0f, +1.0f, 0.0f, 1.0f),
                    simd_make_float4(-1.0f, -1.0f, 0.0f, 1.0f),
                    simd_make_float4(+1.0f, +1.0f, 0.0f, 1.0f),
                    simd_make_float4(+1.0f, -1.0f, 0.0f, 1.0f),
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
            
            array<vertex> vertices;
            
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
            
            // apend
            void push_back(vertex x) {
                vertices.push_back(x);
            }
            
            static polygon regular(int n, float phase = 0.0f) {
                
                polygon p;
                
                for (int i = 0; i != n; ++i) {
                    float theta = (i + phase) * 2 * M_PI / n;
                    float x = cos(theta);
                    float y = sin(theta);
                    p.push_back(simd_make_float4(x, y, 0.0f, 1.0f));
                }
                
                return p;
                
            }
                        
            polygon& reverse() {
                std::reverse(vertices.begin(),
                             vertices.end());
                return *this;
            }
                                    
            polygon& apply(simd_float4x4 transform) {
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
            
            simd_float4 centroid() {
                auto& v = *this;
                auto n = area();
                n /= simd_length_squared(n);
                n /= 3.0f;
                simd_float4 centroid = 0;
                for (size_t i = 0; i != size(); ++i) {
                    auto a = simd_cross(v[i].xyz, v[i + 1].xyz);
                    float b = simd_dot(n, a);
                    simd_float4 c = v[i] + v[i + 1];
                    centroid += b * c;
                }
                return centroid;
            }
            
            void stellate(float factor = 2.0f) {
                auto& v = *this;
                auto n = normal() * factor;
                array<vertex> z;
                for (size_t i = 0; i != vertices.size(); ++i) {
                    simd_float3 a = v[i].xyz;
                    simd_float3 b = v[i + 1].xyz;
                    simd_float3 c = simd_cross(b - a, n) + (b + a) * 0.5f;
                    z.push_back(simd_make_float4(a, 1.0f));
                    z.push_back(simd_make_float4(c, 1.0f));
                }
                vertices.swap(z);
            }
            
            void truncate(float factor = 1.0 / 3.0f) {
                auto& v = *this;
                array<vertex> z;
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
            
            array<polygon> faces;
            
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
                        
            static polyhedron prismatoid(polygon base, simd_float4x4 transform) {
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
                simd_float4x4 A = simd_matrix_scale(simd_make_float3(scale, scale, scale));
                simd_float4x4 B = simd_matrix_translate(simd_make_float3(0, 0, 1));
                return prismatoid(base, simd_mul(B, A));
            }
            
            static polyhedron extrusion(polygon base, int n, simd_float4x4 transform) {
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
                
                simd_float4 u[12] = {
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
            
            void apply(simd_float4x4 transform) {
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
                    auto p = c + simd_make_float4(a * factor, 0);
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
            
            array<MeshVertex> vertices;
            
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
                    simd_float4 n;
                    n.xyz = simd_normalize(simd_cross(b - a, c - b));
                    n.w = 0.0f;
                    vertices[i + 0].normal = n;
                    vertices[i + 1].normal = n;
                    vertices[i + 2].normal = n;
                }
            }
            
            void texcoord_from_position(simd_float4x4 transform = matrix_identity_float4x4) {
                for (auto& v : vertices) {
                    v.texCoord = simd_mul(transform, v.position).xy;
                }
            }
            
            void texcoord_from_normal(simd_float4x4 transform = matrix_identity_float4x4) {
                for (auto& v : vertices) {
                    // we want to rotate +Z to normal
                    // this is basically a quaternion but i am dumb
                    simd_float3 z = simd_make_float3(0, 0, 1);
                    //simd_float3 a = simd_cross(v.normal.xyz, z);
                    //float b = simd_dot(v.normal.xyz, z);
                    auto q = simd_quaternion(z, v.normal.xyz);
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
                    
                    simd_float4 tangent = A.columns[0];
                    
                    vertices[i + 0].tangent = simd_normalize(tangent);
                    vertices[i + 1].tangent = simd_normalize(tangent);
                    vertices[i + 2].tangent = simd_normalize(tangent);

                }
            }
            
        };
        
        
        struct indexed_mesh {
            
            array<MeshVertex> vertices;
            array<int> indices; // ints are supposed to be good for metal
            
            void indices_from_iota() {
                indices.resize(vertices.size());
                std::iota(indices.begin(), indices.end(), 0);
            }
            
            
            
            
        };
        
        
        
    } // namespace mesh2
    
    
} // namespace wry

#endif /* mesh_hpp */
