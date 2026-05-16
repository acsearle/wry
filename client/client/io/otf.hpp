//
//  otf.hpp
//  client
//
//  Created by Antony Searle on 26/3/2026.
//

#ifndef otf_hpp
#define otf_hpp

// Open Type Format
//
// https://learn.microsoft.com/en-us/typography/opentype/spec/

#include "span.hpp"
#include "stddef.hpp"

namespace wry::otf {

    struct Handle;

    Handle const* parse_Handle(span<byte const>);
    
} // namespace wry::otf

#endif /* otf_hpp */
