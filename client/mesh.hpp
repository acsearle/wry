//
//  mesh.hpp
//  client
//
//  Created by Antony Searle on 17/8/2023.
//

#ifndef mesh_hpp
#define mesh_hpp

#include <cmath>

#include "ShaderTypes.h"

#include "array.hpp"
#include "image.hpp"


inline simd_float4x4 simd_matrix4x4(simd_float3x3 a) {
    return simd_matrix(simd_make_float4(a.columns[0], 0.0f),
                       simd_make_float4(a.columns[1], 0.0f),
                       simd_make_float4(a.columns[2], 0.0f),
                       simd_make_float4(0.0f, 0.0f, 0.0f, 1.0f));
}

inline simd_float3x3 simd_matrix3x3(simd_float4x4 a) {
    return simd_matrix(a.columns[0].xyz,
                       a.columns[1].xyz,
                       a.columns[2].xyz);
}

inline constexpr simd_float4x4 simd_matrix_ndc_to_tc = {{
    {  0.5f,  0.0f,  0.0f,  0.0f },
    {  0.0f, -0.5f,  0.0f,  0.0f },
    {  0.0f,  0.0f,  1.0f,  0.0f },
    {  0.5f,  0.5f,  0.0f,  1.0f },
}};

inline constexpr simd_float4x4 simd_matrix_tc_to_ndc = {{
    {  2.0f,  0.0f,  0.0f,  0.0f },
    {  0.0f, -2.0f,  0.0f,  0.0f },
    {  0.0f,  0.0f,  1.0f,  0.0f },
    { -1.0f,  1.0f,  0.0f,  1.0f },
}};

inline simd_float4x4 simd_matrix_rotation(float theta, simd_float3 u) {
    return simd_matrix4x4(simd_quaternion(theta, u));
}

inline simd_float4x4 simd_matrix_translation(simd_float3 u) {
    return simd_matrix(simd_make_float4(1.0f, 0.0f, 0.0f, 0.0f),
                       simd_make_float4(0.0f, 1.0f, 0.0f, 0.0f),
                       simd_make_float4(0.0f, 0.0f, 1.0f, 0.0f),
                       simd_make_float4(u, 1.0f));
}

inline simd_float4x4 simd_matrix_scale(simd_float3 u) {
    return simd_matrix(simd_make_float4(u.x, 0.0f, 0.0f, 0.0f),
                       simd_make_float4(0.0f, u.y, 0.0f, 0.0f),
                       simd_make_float4(0.0f, 0.0f, u.z, 0.0f),
                       simd_make_float4(0.0f, 0.0f, 0.0f, 1.0f));
}
                       
namespace wry {
    
    
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
                    simd_float2 texCoord = (theta2 / M_PI) * simd_make_float2(cos(phi), -sin(phi)) + 0.5f;
                    
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
                //     6---7---8---9--10---7  6+i 6+j
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
    
    
} // namespace wry

#endif /* mesh_hpp */
