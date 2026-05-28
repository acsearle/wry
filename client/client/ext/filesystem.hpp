//
//  filesystem.hpp
//  client
//
//  Created by Antony Searle on 24/12/2023.
//

#ifndef filesystem_hpp
#define filesystem_hpp

#include <filesystem>

#include "contiguous_deque.hpp"
#include "stddef.hpp"
#include "string.hpp"

namespace wry {

    // Read raw bytes from a file.  No interpretation, no validation -- use
    // this for binary data (font files, images, save blobs, network frames).
    ContiguousDeque<byte> bytes_from_file(const std::filesystem::path&);

    // Read a UTF-8 text file.  Validates the contents and throws
    // std::invalid_argument on invalid UTF-8.  Use this for files that are
    // documented to be UTF-8 text (JSON, CSV, OBJ/MTL, source, config).
    String string_from_file(const std::filesystem::path&);

};

#endif /* filesystem_hpp */
