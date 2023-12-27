//
//  platform.hpp
//  client
//
//  Created by Antony Searle on 27/6/2023.
//

#ifndef platform_hpp
#define platform_hpp

#include <filesystem>

#include "string.hpp"
#include "string_view.hpp"

namespace wry {
    
    std::filesystem::path path_for_resource(string_view name, string_view ext);
    std::filesystem::path path_for_resource(string_view name);
    
}

#endif /* platform_hpp */
