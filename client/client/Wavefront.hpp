//
//  Wavefront.hpp
//  client
//
//  Created by Antony Searle on 24/12/2023.
//

#ifndef Wavefront_hpp
#define Wavefront_hpp

#include <optional>

#include "filesystem.hpp"
#include "mesh.hpp"

// TODO:
// - tinyobj_loader_c
// - cglTF ?

namespace wry {
    
    wry::mesh::mesh from_obj(const std::filesystem::path&);
        
} // namespace wry

namespace wry::Wavefront {
    
    struct Material {
        String name;
        std::variant<std::monostate, packed::float3, String> Kd;
        std::variant<std::monostate, packed::float3, String> Ks;
        std::variant<std::monostate, packed::float3, String> Ke;
        std::variant<std::monostate, float, String> Ni;
        std::variant<std::monostate, float, String> d;
        std::variant<std::monostate, float, String> illum;
        std::variant<std::monostate, float, String> Pr;
        std::variant<std::monostate, float, String> Pm;
        std::variant<std::monostate, float, String> Pc;
        std::variant<std::monostate, float, String> Pcr;
        std::variant<std::monostate, float, String> aniso;
        std::variant<std::monostate, float, String> anisor;
        std::optional<std::pair<float, String>> map_Bump;
    };
    
    using Library = Array<Material>;
    
    struct Object {
        
    };
    
} // wry::Wavefront

#endif /* Wavefront_hpp */
