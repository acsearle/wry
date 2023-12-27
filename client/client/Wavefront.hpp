//
//  Wavefront.hpp
//  client
//
//  Created by Antony Searle on 24/12/2023.
//

#ifndef Wavefront_hpp
#define Wavefront_hpp

#include "filesystem.hpp"
#include "mesh.hpp"
#include "string_view.hpp"

namespace wry {
    
    wry::mesh::mesh from_obj(const std::filesystem::path&);
        
} // namespace wry

namespace wry::Wavefront {
    
    // Wavefront .mtl files associated with different .obj files may use the
    // same names for materials, so we can't have a global set of materials?
    
    // Need to be able to compare paths well enough to only load textures once
    
    // Need async texture loading?  Since we want to generate mip-maps on the
    // GPU
    
    // Can we use coroutines for this without going crazy?  No scheduler, just
    // called back by GPU on arbitrary thread
   
    
} // wry::Wavefront

#endif /* Wavefront_hpp */
