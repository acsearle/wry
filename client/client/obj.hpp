//
//  obj.hpp
//  client
//
//  Created by Antony Searle on 10/9/2023.
//

#ifndef obj_hpp
#define obj_hpp

#include "mesh.hpp"
#include "string_view.hpp"

namespace wry {
    
    wry::mesh::mesh from_obj(string_view filename);
    
}

#endif /* obj_hpp */
