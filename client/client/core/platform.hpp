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

    // The app bundle's Resources directory (client.app/Contents/Resources),
    // or empty if the process has none.  For a bare executable this is
    // the executable's own directory, per NSBundle.
    std::filesystem::path bundle_resource_directory();
    
}

#endif /* platform_hpp */
