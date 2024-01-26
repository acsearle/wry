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
    
    std::filesystem::path path_for_resource(StringView name, StringView ext);
    std::filesystem::path path_for_resource(StringView name);
    
}

#endif /* platform_hpp */
