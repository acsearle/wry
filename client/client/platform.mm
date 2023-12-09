//
//  platform.mm
//  client
//
//  Created by Antony Searle on 27/6/2023.
//

#include <filesystem>

#include "platform.hpp"
#include "string_view.hpp"
#include "string.hpp"

#import <Foundation/Foundation.h>

namespace wry {
    
    // On macOS, this may point to a bundle
    
    std::filesystem::path path_for_resource(string_view name, string_view ext) {
        string s(u8"/Users/antony/Desktop/assets/");
        s.append(name);
        s.push_back(u8'.');
        s.append(ext);
        return std::filesystem::path(s.begin(), s.end());
    }

}
