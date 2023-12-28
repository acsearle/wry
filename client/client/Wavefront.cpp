//
//  Wavefront.cpp
//  client
//
//  Created by Antony Searle on 24/12/2023.
//

#include "Wavefront.hpp"

#include <iostream>
#include <map>

#include "array.hpp"
#include "filesystem.hpp"
#include "parse.hpp"
#include "platform.hpp"
#include "simd.hpp"
#include "string.hpp"
#include "table.hpp"

#include "test.hpp"

namespace wry::Wavefront {
    
    // Basics for each file
    
    auto match_comment() {
        return match_and(match_character(u8'#'),
                         match_until(match_not_empty(),
                                     match_or(match_newline(),
                                              match_empty())));
    }
    
    auto parse_xyz(packed::double3& xyz) {
        return [&xyz](string_view& v) {
            double x, y, z;
            return (match_and(parse_number_relaxed(x),
                              parse_number_relaxed(y),
                              parse_number_relaxed(z))(v)
                    && ((void) (xyz = packed::double3{x, y, z}), true));
        };
    }
    
    auto parse_u_vw(packed::double3& u_vw) {
        return [&u_vw](string_view& sv) {
            double u, v = 0.0, w = 0.0;
            return (match_and(parse_number_relaxed(u),
                              match_optional(parse_number_relaxed(v),
                                             parse_number_relaxed(w)))(sv)
                    && ((void) (u_vw = packed::double3{u, v, w}), true));
        };
    }
    
    auto parse_xyz_w(double4& xyz_w) {
        return [&xyz_w](string_view& v) {
            double x, y, z, w = 1.0;
            return (match_and(parse_number_relaxed(x),
                              parse_number_relaxed(y),
                              parse_number_relaxed(z),
                              match_optional(parse_number_relaxed(w)))(v)
                    && ((void) (xyz_w = simd::make<double4>(x, y, z, w)), true));
        };
    }
        
    auto parse_comment() {
        return match_and(match_character('#'),
                         match_until(match_not_empty(),
                                     match_newline()));
    }
    
    // Parses until newline or end of file, but excludes the newline from the
    // view given to the effect
    auto parse_line(auto&& effect) {
        return [effect=std::forward<decltype(effect)>(effect)](string_view& v) -> bool {
            for (auto u(v);;) {
                auto w = u;
                if (match_or(match_newline(), match_empty())(u)) {
                    effect(w / v);
                    return true;
                }
                u.pop_front();
            }
        };
    }
    
    
    struct MTLFile {
        
        // Wavefront .mtl format
        
        struct Material {
                        
            // Basic
            
            // ambient
            packed::double3 Ka;
            String map_Ka;
            // diffuse
            packed::double3 Kd;
            String map_Kd;
            // specular
            packed::double3 Ks;
            String map_Ks;
            // index of refraction
            double Ni;
            // alpha
            double d;
            double illum;
            double aniso;
            double anisor;
            
            // Physically-Based
            
            // roughness
            float Pr;
            String map_Pr;
            // metallic
            float Pm;
            String map_Pm;
            // sheen
            float Ps;
            String map_Ps;
            // clearcoat thickness
            float Pc;
            // clearcoat roughness
            float Pcr;
            // emissive
            packed::double3 Ke;
            String map_Ke;
            // normal
            String norm;
            String map_Bump; // ??? Blender's export of normal map

        };
        
        std::map<string, Material> named_materials;
        
        string current_name;
        // string _current_key;
        // std::map<string, string> _current_map;
        Material current_material;
        
        void commit() {
            // auto& a = _named_materials[_current_mtl];
            //a._map.merge(std::move(_current_map));
            named_materials[current_name] = current_material;
            // leave current_material as the default for next material?
            // or clear it?
        }
        
        auto parse_newmtl() {
            return match_and(match_string("newmtl"),
                             match_blanks(),
                             parse(match_graphs(),
                                   [this](string_view match) {
                commit();
                this->current_name = match;
            }));
        }
        
        /*
        auto parse_key_value() {
            return match_and(parse(match_graphs(),
                                   [this](string_view match) {
                this->_current_key = match;
            }),
                             match_blanks(),
                             parse(match_graphs(),
                                   [this](string_view match) {
                commit();
                this->_current_map[this->_current_key] = match;
            }));
        }
         */
                
#define PARSE_XYZ(X)\
        match_and(match_string(#X),\
                             parse_xyz(this->current_material. X ))

#define PARSE_FILENAME(X)\
match_and(match_string(#X),match_blanks(),\
parse(match_line(), [this](string_view match) { this->current_material. X  = match; }))

#define PARSE_NUMBER(X)\
match_and(match_string(#X),\
parse_number_relaxed(this->current_material. X ))

        
        void parse_mtl(string_view& v) {
            auto description = match_or(
                                        parse_comment(),
                                        parse_newmtl(),
                                        PARSE_XYZ(Kd),
                                        PARSE_XYZ(Ks),
                                        PARSE_XYZ(Ke),
                                        PARSE_NUMBER(Ni),
                                        PARSE_NUMBER(d),
                                        PARSE_NUMBER(illum),
                                        PARSE_NUMBER(Pr),
                                        PARSE_NUMBER(Pm),
                                        PARSE_NUMBER(Ps),
                                        PARSE_NUMBER(Pc),
                                        PARSE_NUMBER(Pcr),
                                        PARSE_NUMBER(aniso),
                                        PARSE_NUMBER(anisor),
                                        PARSE_FILENAME(map_Kd),
                                        PARSE_FILENAME(map_Pr),
                                        PARSE_FILENAME(map_Pm),
                                        PARSE_FILENAME(map_Bump)
                                        );
            while (!v.empty()
                   && match_spaces()(v)
                   && description(v))
                ;
            commit();
        }
        
        
        void print() {
            for (auto&& [k, v] : named_materials) {
                printf("newmtl %.*s\n", (int) k.chars.size(), (const char*) k.data());
#define PRINT_XYZ(X) printf(#X " %g %g %g\n", v.X.x, v.X.y, v.X.z)
#define PRINT_NUMBER(X) printf(#X " %g\n", v.X)
#define PRINT_STRING(X) printf(#X " %.*s\n", (int) v.X.chars.size(), (const char*) v.X.data())
                PRINT_XYZ(Kd);
                PRINT_XYZ(Ks);
                PRINT_XYZ(Ke);
                PRINT_NUMBER(Ni);
                PRINT_NUMBER(d);
                PRINT_NUMBER(illum);
                PRINT_NUMBER(Ni);
                PRINT_NUMBER(Pr);
                PRINT_NUMBER(Pm);
                PRINT_NUMBER(Ps);
                PRINT_NUMBER(Pc);
                PRINT_NUMBER(Pcr);
                PRINT_NUMBER(aniso);
                PRINT_NUMBER(anisor);
                PRINT_STRING(map_Ka);
                PRINT_STRING(map_Kd);
                PRINT_STRING(map_Ke);
                PRINT_STRING(map_Ks);
                PRINT_STRING(map_Pm);
                PRINT_STRING(map_Pr);
                PRINT_STRING(map_Ps);
                PRINT_STRING(map_Bump);

                /*
                for (auto&& [l, w] : v._map) {
                    printf("%.*s\n %.*s\n", (int) l.chars.size(), (const char*) l.data(),
                           (int) w.chars.size(), (const char*) w.data());
                }
                 */
            }
        }
            
    };
    
    /*
    struct OBJFile {
        
        struct SmoothingGroup {
            
            String name;
            Array<Index> triangles;
            
        };
        
        struct Group {
            
            String name;
            String material;
            Array<SmoothingGroup> smoothing_groups;
            
        };
        
        struct Object {
            
            String name;
            Array<Group> groups;
            
        };
        
        Array<Object> objects;
        
    };
     */
    
    struct OBJFile {
        
        // Wavefront .obj format
        
        // Growing arrays of vertex fields
        
        // TODO: should these be float32?
        
        using Index = std::uint32_t;
        
        Array<double4> positions;
        Array<packed::double3> normals;
        Array<packed::double3> coordinates;
        Array<packed::double3> parameters;
        
        // Material libraries
        // TODO: load them
        // TODO: vulnerability
        Array<std::filesystem::path> mtllibs;
        std::map<String, MTLFile::Material> materials;
        
        // Current face metadata
        String _current_smoothing_group;
        String _current_object_name;
        String _current_group_name;
        MTLFile _current_materials;
        MTLFile::Material _current_material;
        
        // Current faces
        Array<Index> _current_faces;
        
        struct SmoothingGroup {
            Array<Index> faces;
        };
        
        struct Group {
            // TODO: What level is material changed at; is it always at a
            // group boundary?
            MTLFile::Material usemtl;
            std::map<string, SmoothingGroup> smoothing_groups;
        };
        
        struct Object {
            std::map<string, Group> named_groups;
        };
        
        std::map<string, Object> named_objects;
        
        void commit() {
            auto& a = named_objects[_current_object_name];
            auto& b = a.named_groups[_current_group_name];
            b.usemtl = _current_material;
            auto& c = b.smoothing_groups[_current_smoothing_group];
            // DUMP(_current_faces.size());
            c.faces.append(std::move(_current_faces));
            _current_faces.clear();
            // DUMP(c.faces.size());
        }
        
        
        auto parse_position() {
            return [this](string_view& v) {
                simd_double4 position;
                bool flag = match_and(match_character('v'),
                                      parse_xyz_w(position))(v);
                if (flag) {
                    this->positions.push_back(position);
                }
                return flag;
            };
        }
        
        auto parse_coordinate() {
            return [this](string_view& v) {
                packed::double3 coordinate;
                return (match_and(match_string("vt"),
                                  parse_u_vw(coordinate))(v)
                        && ((void) this->coordinates.push_back(coordinate),
                            true));
            };
        }
        
        auto parse_normal() {
            return [this](string_view& v) {
                packed::double3 normal;
                return (match_and(match_string("vn"),
                                  parse_xyz(normal))(v)
                        && ((void) this->normals.push_back(normal),
                            true));
            };
        }
        
        auto parse_parameters() {
            return [this](string_view& v) {
                packed::double3 parameter;
                return (match_and(match_string("vp"),
                                  parse_u_vw(parameter))(v)
                        && ((void) this->parameters.push_back(parameter),
                            true));
            };
        }
        
        auto parse_face_indices(Array<Index>& indices) {
            return [this, &indices](string_view& v) {
                Index i = 0, j = 0, k = 0;
                bool flag = match_and(parse_number_relaxed(i),
                                      match_optional(match_and(match_character('/'),
                                                               match_optional(parse_number(j)),
                                                               match_optional(match_and(match_character('/'),
                                                                                        parse_number(k))))))(v);
                if (flag) {
                    // negative indices are relative to the current size of the
                    // arrays, so we must convert them immediately
                    if (i < 0)
                        i = (Index) this->positions.size() + i + 1;
                    if (j < 0)
                        j = (Index) this->coordinates.size() + j + 1;
                    if (k < 0)
                        k = (Index) this->normals.size() + k + 1;
                    indices.push_back(i);
                    indices.push_back(j);
                    indices.push_back(k);
                }
                return flag;
            };
        }
        
        auto parse_face() {
            Array<Index> indices;
            return [this, indices=std::move(indices)](string_view& v) mutable -> bool {
                indices.clear();
                bool flag = match_and(match_character('f'),
                                      match_star(parse_face_indices(indices)))(v);
                if (flag) {
                    assert(indices.size() >= 3);
                    _current_faces.push_back((Index) indices.size() / 3);
                    _current_faces.append(indices);
                }
                return flag;
            };
        }
        
        auto parse_smoothing_group() {
            return [this](string_view& v) {
                string value;
                bool flag = match_and(match_character('s'),
                                      match_plus(match_space()),
                                      parse(match_plus(match_graph()),
                                            [&value](string_view match) {
                    value = match;
                }))(v);
                if (flag) {
                    commit();
                    _current_smoothing_group = value;
                }
                return flag;
            };
        }
        
        auto parse_mtllib() {
            return match_and(match_string("mtllib"),
                             match_blanks(),
                             parse(match_filename(),
                                   [this](string_view match) {
                std::filesystem::path name = wry::path_for_resource(match);
                auto s = string_from_file(name);
                printf("%.*s", (int) s.chars.size(), (const char*) s.data());
                string_view v(s);
                this->_current_materials.parse_mtl(v);
                if (!v.empty()) {
                    if (v.chars.size() > 40) {
                        printf("ERROR: Parsed up to \"%.*s...\"\n", 40, (const char*) v.chars.data());
                    } else {
                        printf("ERROR: Parsed up to \"%.*s\"\n", (int) v.chars.size(), (const char*) v.chars.data());
                    }
                    abort();
                }
                
                // mtllibs.emplace_back(std::filesystem::path(match.begin(), match.end()));
            }));
        }
        
        auto parse_usemtl() {
            return match_and(match_string("usemtl"),
                             match_blanks(),
                             parse(match_filename(),
                                   [this](string_view match) {
                commit();
                auto it = _current_materials.named_materials.find(String(match));
                if (it == _current_materials.named_materials.end()) {
                    // Missing material is a fatal error
                    abort();
                }
                this->_current_material = it->second;
            }));
        }
        
        auto parse_group() {
            return match_and(match_character('g'),
                             match_blanks(),
                             parse(match_graphs(),
                                   [this](string_view match) {
                commit();
                this->_current_group_name = match;
            }));
        }
        
        auto parse_object() {
            return match_and(match_character('o'),
                             match_blanks(),
                             parse(match_graphs(),
                                   [this](string_view match) {
                commit();
                this->_current_object_name = match;
            }));
        }
        
        
        void parse_obj(string_view& v) {
            auto description = match_or(parse_face(),
                                        parse_position(),
                                        parse_coordinate(),
                                        parse_normal(),
                                        parse_smoothing_group(),
                                        parse_group(),
                                        parse_usemtl(),
                                        parse_object(),
                                        parse_comment(),
                                        parse_mtllib()
                                        );
            while (!v.empty()
                   && match_spaces()(v)
                   && description(v))
                ;
            commit();
        }
        
        void print() {
            for (auto& v : positions)
                printf("v %f %f %f %f\n", v.x, v.y, v.z, v.w);
            for (auto& vt : coordinates)
                printf("vt %f %f %f\n", vt.x, vt.y, vt.z);
            for (auto& vn : normals)
                printf("vn %f %f %f\n", vn.x, vn.y, vn.z);
            for (auto& vp : parameters)
                printf("vp %f %f %f\n", vp.x, vp.y, vp.z);
            
            for (auto& [name, object] : named_objects) {
                if (!name.empty())
                    printf("o %.*s\n", (int) name.chars.size(), (const char*) name.data());
                for (auto& [name, group] : object.named_groups) {
                    // if (!group.usemtl.empty())
                        // printf("usemtl %.*s\n", (int) group.usemtl.chars.size(), (const char*) group.usemtl.data());
                    if (!name.empty())
                        printf("g %.*s\n", (int) name.chars.size(), (const char*) name.data());
                    for (auto& [name, smoothing_group] : group.smoothing_groups) {
                        if (!name.empty())
                            printf("s %.*s\n", (int) name.chars.size(), (const char*) name.data());
                        auto* first = smoothing_group.faces.begin();
                        auto* last = smoothing_group.faces.end();
                        for (; first != last;) {
                            printf("f");
                            long count = *first++;
                            while (count--) {
                                printf(" %u", *first++);
                                printf("/%u", *first++);
                                printf("/%u", *first++);
                            }
                            printf("\n");
                        }
                    }
                }
            }
            
        }
        
    };
    
} // namespace wry::Wavefront

namespace wry {
    
    wry::mesh::mesh from_obj(const std::filesystem::path& v) {
        string s = string_from_file(v);
        string_view u(s);
        Wavefront::OBJFile o;
        o.parse_obj(u);
        o.print();
        if (!u.empty()) {
            if (u.chars.size() > 40) {
                printf("ERROR: Parsed up to \"%.*s...\"\n", 40, (const char*) u.chars.data());
            } else {
                printf("ERROR: Parsed up to \"%.*s\"\n", (int) u.chars.size(), (const char*) u.chars.data());
            }
            abort();
        }
        
        // validate the data for our partial implementation
        
        /*
         for (auto&& [s, faces] : o._smooth_faces) {
         for (auto&& face : faces) {
         assert(face.size() == 3);
         for (auto&& ijk : face) {
         assert(ijk.x != 0); // missing position
         assert(ijk.y != 0); // missing texture coordinate
         assert(ijk.z != 0); // missing normal
         }
         }
         }
         
         // all vertices have position/normal/coordinate
         // all faces are triangular
         
         // TODO: deduplicate vertices, optimize triangle strips
         //
         */
        wry::mesh::mesh m;
        
        for (auto&& [name, object] : o.named_objects) {
            for (auto&& [name, group] : object.named_groups) {
                for (auto&& [name, smoothing_group] : group.smoothing_groups) {
                    auto first = smoothing_group.faces.begin();
                    auto last = smoothing_group.faces.end();
                    for (; first != last;) {
                        long count = *first++;
                        assert(count == 3);
                        while (count--) {
                            long i = *first++;
                            long j = *first++;
                            long k = *first++;
                            MeshVertex w;
                            w.position = make<float4>(o.positions[i-1].x,
                                                      o.positions[i-1].y,
                                                      o.positions[i-1].z,
                                                      o.positions[i-1].w);
                            w.coordinate = make<float4>(o.coordinates[j-1].x,
                                                        o.coordinates[j-1].y,
                                                        o.coordinates[j-1].z,
                                                        1.0f);
                            w.normal = make<float4>(o.normals[k-1].x,
                                                    o.normals[k-1].y,
                                                    o.normals[k-1].z,
                                                    0.0f);
                            w.tangent = make<float4>(0.0, 0.0, 0.0, 0.0);
                            w.bitangent = make<float4>(0.0, 0.0, 0.0, 0.0);
                            m.hack_MeshVertex.push_back(w);
                        }
                    }
                }
            }
        }
        
        assert(!m.hack_MeshVertex.empty());
        
        assert(!(m.hack_MeshVertex.size() % 3));
        for (uint i = 0; i != m.hack_MeshVertex.size(); i += 3) {
            uint js[] = { i, i, i + 1, i + 2, i + 2, i + 2 };
            m.hack_triangle_strip.insert(m.hack_triangle_strip.end(), std::begin(js), std::end(js));
        }
        
        m.repair_jacobian();
        
        
        /*
         for (auto& position : o._positions) {
         mesh::vertex w;
         w.position = make<float4>(position.x, position.y, position.z, 1);
         w.coordinate = make<float4>(position.x, position.y, 0, 1);
         m.vertices.push_back(w);
         }
         for (const auto& [s, faces] : o._smooth_faces) {
         for (const auto& f : faces) {
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
         */
        return m;
    }
    
    
    /*
     define_test("obj") {
     //from_obj("/Users/antony/Desktop/assets/test.obj");
     //from_obj("/Users/antony/Desktop/assets/16747_Mining_Truck_v1.obj");
     
     };
     */
    
    
} // namespace wry

