//
//  obj.cpp
//  client
//
//  Created by Antony Searle on 10/9/2023.
//

#include <iostream>

#include "obj.hpp"
#include "simd.hpp"

#include "array.hpp"
#include "parse.hpp"
#include "string.hpp"
#include "test.hpp"
#include "table.hpp"

namespace wry {
    
    auto match_comment() {
        return match_and(match_character('#'),
                         match_until(match_not_empty(),
                                     match_or(match_newline(),
                                              match_empty())));
    }
    
    auto parse_xyz_w(simd_double4& xyz_w) {
        return [&xyz_w](string_view& v) {
            double x, y, z, w = 1.0;
            return (match_and(parse_number_relaxed(x),
                              parse_number_relaxed(y),
                              parse_number_relaxed(z),
                              match_optional(parse_number_relaxed(w)))(v)
                    && ((void) (xyz_w = simd_make_double4(x, y, z, w)), true));
        };
    }
    
    auto parse_u_vw(simd_double3& u_vw) {
        return [&u_vw](string_view& sv) {
            double u, v = 0.0, w = 0.0;
            return (match_and(parse_number_relaxed(u),
                              match_optional(parse_number_relaxed(v),
                                             parse_number_relaxed(w)))(sv)
                    && ((void) (u_vw = simd_make_double3(u, v, w)), true));
        };
    }
    
    auto parse_xyz(simd_double3& xyz) {
        return [&xyz](string_view& v) {
            double x, y, z;
            return (match_and(parse_number_relaxed(x),
                              parse_number_relaxed(y),
                              parse_number_relaxed(z))(v)
                    && ((void) (xyz = simd_make_double3(x, y, z)), true));
        };
    }
    
    auto parse_comment() {
        return match_and(match_character('#'),
                         match_until(match_not_empty(),
                                     match_newline()));
    }
    
    auto parse_group() {
        return match_and(match_character('g'),
                         match_until(match_not_empty(),
                                     match_newline()));
    }
        
    auto parse_object() {
        return match_and(match_character('o'),
                         match_until(match_not_empty(),
                                     match_newline()));
    }
    
    
    struct obj {
        
        array<simd_double4> _positions;
        array<simd_double3> _coordinates;
        array<simd_double3> _normals;
        array<simd_double3> _parameters;
        
        table<long, array<array<simd_long3>>> _smooth_faces;
        long _smoothing_group = 0;
        
        string _mtllib;
        string _usemtl;
        
        auto parse_position() {
            return [this](string_view& v) {
                simd_double4 position;
                bool flag = match_and(match_character('v'),
                                       parse_xyz_w(position))(v);
                if (flag) {
                    _positions.push_back(position);
                }
                return flag;
                
            };
        }
        
        auto parse_coordinate() {
            return [this](string_view& v) {
                simd_double3 coordinate;
                return (match_and(match_string("vt"),
                                  parse_u_vw(coordinate))(v)
                        && ((void) _coordinates.push_back(coordinate),
                            true));
            };
        }
        
        auto parse_normal() {
            return [this](string_view& v) {
                simd_double3 normal;
                return (match_and(match_string("vn"),
                                  parse_xyz(normal))(v)
                        && ((void) _normals.push_back(normal),
                            true));
            };
        }
        
        auto parse_parameters() {
            return [this](string_view& v) {
                simd_double3 parameter;
                return (match_and(match_string("vp"),
                                  parse_u_vw(parameter))(v)
                        && ((void) _parameters.push_back(parameter),
                            true));
            };
        }
        
        auto parse_face_indices(array<simd_long3>& indices) {
            return [this, &indices](string_view& v) {
                long i, j = 0, k = 0;
                bool flag = match_and(parse_number_relaxed(i),
                                      match_optional(match_and(match_character('/'),
                                                               match_optional(parse_number(j)),
                                                               match_optional(match_and(match_character('/'),
                                                                                        parse_number(k))))))(v);
                if (flag) {
                    // convert relative indexes to 1-based absolute indices
                    if (i < 0)
                        i = _positions.size() + i + 1;
                    if (j < 0)
                        j = _coordinates.size() + j + 1;
                    if (k < 0)
                        k = _normals.size() + k + 1;
                    indices.push_back(simd_make_long3(i, j, k));
                }
                return flag;
            };
        }
        
        auto parse_face() {
            return [this](string_view& v) {
                array<simd_long3> indices;
                bool flag = match_and(match_character('f'),
                                      match_star(parse_face_indices(indices)))(v);
                if (flag) {
                    assert(indices.size() >= 3);
                    _smooth_faces[_smoothing_group].push_back(std::move(indices));
                }
                return flag;
            };
        }
        
        auto parse_smoothing_group() {
            return [this](string_view& v) {
                long s = 0;
                //printf("s looks at %10s\n", v.a._ptr);
                bool flag = match_and(match_character('s'),
                                      match_spaces(),
                                      match_or(match_string("off"),
                                                  parse_number(s)))(v);
                if (flag) {
                    _smoothing_group = s;
                }
                return flag;
            };
        }
        
        auto parse_mtllib() {
            return match_and(match_string("mtllib"),
                             match_blanks(),
                             parse(match_filename(),
                                   [this](string_view match) {
                this->_mtllib = match;
            }));
        }

        auto parse_usemtl() {
            return match_and(match_string("usemtl"),
                             match_blanks(),
                             parse(match_identifier(),
                                   [this](string_view match) {
                this->_usemtl = match;
            }));
        }

                             
        void parse_obj(string_view& v) {
            auto description = match_or(
                                        parse_position(),
                                        parse_coordinate(),
                                        parse_normal(),
                                        parse_face(),
                                        parse_comment(),
                                        parse_group(),
                                        parse_object(),
                                        parse_smoothing_group(),
                                        parse_mtllib(),
                                        parse_usemtl()
                                        );
            while (!v.empty()
                   && match_spaces()(v)
                   && description(v))
                ;
        }
                
        void print() {
            for (auto& v : _positions)
                printf("v %f %f %f %f\n", v.x, v.y, v.z, v.w);
            for (auto& vt : _coordinates)
                printf("vt %f %f %f\n", vt.x, vt.y, vt.z);
            for (auto& vn : _normals)
                printf("vn %f %f %f\n", vn.x, vn.y, vn.z);
            for (auto& s : _smooth_faces) {
                printf("s");
                if (s.first) {
                    printf(" %ld\n", s.first);
                } else {
                    printf(" off\n");
                }
                for (auto& f : s.second) {
                    printf("f");
                    for (auto& g : f) {
                        printf(" %ld", g.x);
                        if (g.y || g.z)
                            printf("/");
                        if (g.y)
                            printf("%ld", g.y);
                        if (g.z)
                            printf("/%ld", g.z);
                    }
                    printf("\n");
                }
            }
                
        }
        
    };
    
    wry::mesh::mesh from_obj(string_view v) {
        string s = string_from_file(v);
        string_view u(s);
        obj o;
        o.parse_obj(u);
        if (u.end() != s.end()) {
            printf("ERROR: Parsed up to ...%.*s\n", (int) u.chars.size(), u.chars.data());
        }
        wry::mesh::mesh m;
        for (auto& position : o._positions) {
            mesh::vertex w;
            w.position = make<float4>(position.x, position.y, position.z, 1);
            w.coordinate = make<float4>(position.x, position.y, 0, 1);
            m.vertices.push_back(w);
        }
        for (auto& s : o._smooth_faces) {
            for (auto f : s.second) {
                mesh::face g;
                for (auto i : f) {
                    g.indices.push_back(i.x - 1);
                }
                m.faces.push_back(g);
            }
        }
        m.triangulate();
        m.repair_jacobian();
        m.strip();
        return m;
    }

 
    /*
    define_test("obj") {
        //from_obj("/Users/antony/Desktop/assets/test.obj");
        //from_obj("/Users/antony/Desktop/assets/16747_Mining_Truck_v1.obj");
        
    };
     */
    
    
} // namespace wry

