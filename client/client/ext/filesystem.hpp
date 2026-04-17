//
//  filesystem.hpp
//  client
//
//  Created by Antony Searle on 24/12/2023.
//

#ifndef filesystem_hpp
#define filesystem_hpp

#include <filesystem>

#include "string.hpp"

namespace wry {
    
    String string_from_file(const std::filesystem::path&);
    
};

#endif /* filesystem_hpp */
