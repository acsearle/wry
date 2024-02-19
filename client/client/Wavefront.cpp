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
    
    auto match_comment() {
        return match_and(match_character(u8'#'),
                         match_line());
    }
    
    auto match_ignore() {
        return match_star(match_or(match_space(),
                                   match_comment()));
    }
    
    // match three numbers
    auto parse_xyz(auto xyz[]) {
        return [xyz](StringView& v) {
            return match_and(parse_number_relaxed(xyz[0]),
                             parse_number_relaxed(xyz[1]),
                             parse_number_relaxed(xyz[2]))(v);
        };
    }

    auto parse_xyz(packed::float3& xyz) {
        return [xyz=(float*)&xyz](StringView& v) {
            return match_and(parse_number_relaxed(xyz[0]),
                             parse_number_relaxed(xyz[1]),
                             parse_number_relaxed(xyz[2]))(v);
        };
    }

    // match one, two or three numbers
    auto parse_u_vw(auto u_vw[3]) {
        return [u_vw](StringView& sv) {
            return match_and(parse_number_relaxed(u_vw[0]),
                             match_optional(parse_number_relaxed(u_vw[1]),
                                            parse_number_relaxed(u_vw[2])))(sv);
        };
    }
    
    // match three or four numbers
    auto parse_xyz_w(auto xyz_w[4]) {
        return [xyz_w](StringView& v) {
            return match_and(parse_number_relaxed(xyz_w[0]),
                             parse_number_relaxed(xyz_w[1]),
                             parse_number_relaxed(xyz_w[2]),
                             match_optional(parse_number_relaxed(xyz_w[3])))(v);
        };
    }
    
    // parse up to newline or end of file, and consume the newline
    auto parse_line(auto&& effect) {
        return [effect=std::forward<decltype(effect)>(effect)](StringView& v) -> bool {
            if (v.empty())
                return false;
            for (auto u(v);;) {
                auto w = u;
                if (match_or(match_newline(), match_empty())(u)) {
                    effect(v / w);
                    v.reset(u);
                    return true;
                }
                u.pop_front();
            }
        };
    }
    
    auto parse_posix_portable_filename(auto& x) {
        return parse(match_posix_portable_filename(), 
                     [&](auto v) {
            x = v;
        });
    }

    auto parse_posix_portable_path(auto& x) {
        return parse(match_posix_portable_path(),
                     [&](auto v) {
            x = v;
        });
    }

    
    
    
    auto parse_Library(Library& x) {
        return [&x](auto& v) {
            for (;;) {
                match_ignore()(v);
                String s;
                packed::float3 f;
                float g;
                if (match_and(match_string("newmtl"),
                              match_hspace(),
                              parse_posix_portable_path(s))(v)) {
                    x.emplace_back();
                    x.back().name = std::move(s);
                    continue;
                }
                if (x.empty())
                    return true;
                if (match_and(match_string("Kd"),
                              match_hspace(),
                              parse_xyz(f))(v)) {
                    x.back().Kd.emplace<packed::float3>(f);
                    continue;
                }
                if (match_and(match_string("map_Kd"),
                              match_hspace(),
                              parse_posix_portable_filename(s))(v)) {
                    x.back().Kd.emplace<String>(std::move(s));
                    continue;
                }
                if (match_and(match_string("Ks"),
                              match_hspace(),
                              parse_xyz(f))(v)) {
                    x.back().Ks.emplace<packed::float3>(f);
                    continue;
                }
                if (match_and(match_string("Ke"),
                              match_hspace(),
                              parse_xyz(f))(v)) {
                    x.back().Ke.emplace<packed::float3>(f);
                    continue;
                }
                if (match_and(match_string("map_Ke"),
                              match_hspace(),
                              parse_posix_portable_filename(s))(v)) {
                    x.back().Ke.emplace<String>(std::move(s));
                    continue;
                }
                if (match_and(match_string("Ni"),
                              match_hspace(),
                              parse_number(g))(v)) {
                    x.back().Ni.emplace<float>(g);
                    continue;
                }
                if (match_and(match_string("d"),
                              match_hspace(),
                              parse_number(g))(v)) {
                    x.back().d.emplace<float>(g);
                    continue;
                }
                if (match_and(match_string("illum"),
                              match_hspace(),
                              parse_number(g))(v)) {
                    x.back().illum.emplace<float>(g);
                    continue;
                }

                if (match_and(match_string("Pr"),
                              match_hspace(),
                              parse_number(g))(v)) {
                    x.back().Pr.emplace<float>(g);
                    continue;
                }
                if (match_and(match_string("map_Pr"),
                              match_hspace(),
                              parse_posix_portable_filename(s))(v)) {
                    x.back().Pr.emplace<String>(std::move(s));
                    continue;
                }
                if (match_and(match_string("Ps"),
                              match_hspace(),
                              parse_number(g))(v)) {
                    x.back().Pr.emplace<float>(g);
                    continue;
                }
                if (match_and(match_string("map_Ps"),
                              match_hspace(),
                              parse_posix_portable_filename(s))(v)) {
                    x.back().Pr.emplace<String>(std::move(s));
                    continue;
                }

                if (match_and(match_string("Pm"),
                              match_hspace(),
                              parse_number(g))(v)) {
                    x.back().Pm.emplace<float>(g);
                    continue;
                }
                if (match_and(match_string("map_Pm"),
                              match_hspace(),
                              parse_posix_portable_filename(s))(v)) {
                    x.back().Pm.emplace<String>(std::move(s));
                    continue;
                }
                if (match_and(match_string("Pc"),
                              match_hspace(),
                              parse_number(g))(v)) {
                    x.back().Pm.emplace<float>(g);
                    continue;
                }
                if (match_and(match_string("map_Pc"),
                              match_hspace(),
                              parse_posix_portable_filename(s))(v)) {
                    x.back().Pm.emplace<String>(std::move(s));
                    continue;
                }
                if (match_and(match_string("Pcr"),
                              match_hspace(),
                              parse_number(g))(v)) {
                    x.back().Pm.emplace<float>(g);
                    continue;
                }
                if (match_and(match_string("map_Pcr"),
                              match_hspace(),
                              parse_posix_portable_filename(s))(v)) {
                    x.back().Pm.emplace<String>(std::move(s));
                    continue;
                }
                if (match_and(match_string("aniso"),
                              match_hspace(),
                              parse_number(g))(v)) {
                    x.back().aniso.emplace<float>(g);
                    continue;
                }
                if (match_and(match_string("anisor"),
                              match_hspace(),
                              parse_number(g))(v)) {
                    x.back().anisor.emplace<float>(g);
                    continue;
                }
                if (match_and(match_string("map_Bump -bm"),
                              match_hspace(),
                              parse_number(g),
                              match_hspace(),
                              parse_posix_portable_filename(s))(v)) {
                    x.back().map_Bump.emplace(g, std::move(s));
                    continue;
                }
                
                return true;
            }
        };
    }

    
        
    
    
    
    
    struct OBJFile {
        
        // Wavefront .obj format
        
        // Growing arrays of vertex fields
                
        using Index = std::uint32_t;
        
        Array<float4> positions;
        Array<packed::float3> normals;
        Array<packed::float3> coordinates;
        Array<packed::float3> parameters;
        
        // Running material library
        Table<String, Material> materials;
        
        // Current face metadata
        String _current_smoothing_group;
        String _current_object_name;
        String _current_group_name;
        Library _current_materials;
        Material _current_material;
        
        // Current faces
        Array<Index> _current_faces;
        
        struct SmoothingGroup {
            Array<Index> faces;
        };
        
        struct Group {
            // TODO: What level is material changed at; is it always at a
            // group boundary?
            Material usemtl;
            std::map<String, SmoothingGroup> smoothing_groups;
        };
        
        struct Object {
            std::map<String, Group> named_groups;
        };
        
        std::map<String, Object> named_objects;
        
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
            return [this](StringView& v) {
                float4 position = {0, 0, 0, 1};
                bool flag = match_and(match_character('v'),
                                      parse_xyz_w((float*)&position))(v);
                if (flag) {
                    this->positions.push_back(position);
                }
                return flag;
            };
        }
        
        auto parse_coordinate() {
            return [this](StringView& v) {
                packed::float3 coordinate = {0, 0, 0};
                return (match_and(match_string("vt"),
                                  parse_u_vw((float*)&coordinate))(v)
                        && ((void) this->coordinates.push_back(coordinate),
                            true));
            };
        }
        
        auto parse_normal() {
            return [this](StringView& v) {
                packed::float3 normal = {0, 0, 0};
                return (match_and(match_string("vn"),
                                  parse_xyz((float*)&normal))(v)
                        && ((void) this->normals.push_back(normal),
                            true));
            };
        }
        
        auto parse_parameters() {
            return [this](StringView& v) {
                packed::float3 parameter = { 0, 0, 0};
                return (match_and(match_string("vp"),
                                  parse_u_vw((float*)&parameter))(v)
                        && ((void) this->parameters.push_back(parameter),
                            true));
            };
        }
        
        auto parse_face_indices(Array<Index>& indices) {
            return [this, &indices](StringView& v) {
                Index i = 0, j = 0, k = 0;
                bool flag = match_and(parse_number_relaxed(i),
                                      match_optional(match_and(match_character('/'),
                                                               match_optional(parse_number_relaxed(j)),
                                                               match_optional(match_and(match_character('/'),
                                                                                        parse_number_relaxed(k))))))(v);
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
            // TODO: static sized face array?
            //       - require triangles?
            //       - subdivide on load?
            Array<Index> indices;
            return [this, indices=std::move(indices)](StringView& v) mutable -> bool {
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
            return [this](StringView& v) {
                String value;
                bool flag = match_and(match_character('s'),
                                      match_plus(match_space()),
                                      parse(match_plus(match_posix_portable_filename()),
                                            [&value](StringView match) {
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
                             match_plus(match_hspace()),
                             parse(match_posix_portable_path(),
                                   [this](StringView match) {
                std::filesystem::path name = wry::path_for_resource(match);
                auto s = string_from_file(name);
                // printf("%.*s", (int) s.chars.size(), (const char*) s.data());
                StringView v(s);
                parse_Library(this->_current_materials)(v);
                if (!v.empty()) {
                    if (v.chars.size() > 40) {
                        printf("ERROR: Parsed up to \"%.*s...\"\n", 40, (const char*) v.chars.data());
                    } else {
                        printf("ERROR: Parsed up to \"%.*s\"\n", (int) v.chars.size(), (const char*) v.chars.data());
                    }
                    abort();
                }                
            }));
        }
        
        auto parse_usemtl() {
            return match_and(match_string("usemtl"),
                             match_plus(match_hspace()),
                             parse_line([this](StringView match) {
                // printf("parse_line \"%.*s\"\n", (int) match.chars.size(), (const char*) match.chars.data());
                commit();
                for (auto&& m : _current_materials) {
                    if (m.name == match) {
                        this->_current_material = m;
                        return;
                    }
                }
            }));
        }
        
        auto parse_group() {
            return match_and(match_character('g'),
                             match_plus(match_hspace()),
                             parse(match_posix_portable_filename(),
                                   [this](StringView match) {
                commit();
                this->_current_group_name = match;
            }));
        }
        
        auto parse_object() {
            return match_and(match_character('o'),
                             match_plus(match_hspace()),
                             parse(match_posix_portable_filename(),
                                   [this](StringView match) {
                commit();
                this->_current_object_name = match;
            }));
        }
        
        
        void parse_obj(StringView& v) {
            auto description = match_or(parse_face(),
                                        parse_position(),
                                        parse_coordinate(),
                                        parse_normal(),
                                        parse_smoothing_group(),
                                        parse_group(),
                                        parse_usemtl(),
                                        parse_object(),
                                        match_comment(),
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
        String s = string_from_file(v);
        StringView u(s);
        Wavefront::OBJFile o;
        o.parse_obj(u);
        // o.print();
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
                            if (j) {
                                w.coordinate = make<float4>(o.coordinates[j-1].x,
                                                            o.coordinates[j-1].y,
                                                            o.coordinates[j-1].z,
                                                            1.0f);
                            }
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

