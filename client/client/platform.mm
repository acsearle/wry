//
//  platform.mm
//  client
//
//  Created by Antony Searle on 27/6/2023.
//

#include <filesystem>

#include "platform.hpp"
#include "string.hpp"

#import <Foundation/Foundation.h>

namespace wry {
    
    // On macOS, this may point to a bundle?

    std::filesystem::path path_for_resource(StringView name) {
        String s(u8"");
        s.append(name);
        return std::filesystem::path(s.begin(), s.end());
    }

    std::filesystem::path path_for_resource(StringView name, StringView ext) {
        String s(name);
        s.push_back(u8'.');
        s.append(ext);
        return path_for_resource(s);
    }

}
