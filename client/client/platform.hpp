//
//  platform.hpp
//  client
//
//  Created by Antony Searle on 27/6/2023.
//

#ifndef platform_hpp
#define platform_hpp

#include "string.hpp"
#include "string_view.hpp"

namespace wry {
    
    string path_for_resource(string_view name, string_view ext);
    
}

#endif /* platform_hpp */
