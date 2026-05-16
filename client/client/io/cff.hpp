//
//  cff.hpp
//  client
//
//  Created by Antony Searle on 26/3/2026.
//

#ifndef cff_hpp
#define cff_hpp

// The Compact Font Format

// https://adobe-type-tools.github.io/font-tech-notes/pdfs/5176.CFF.pdf

// Type2 Charstring Format

// https://adobe-type-tools.github.io/font-tech-notes/pdfs/5177.Type2.pdf

#include <map>
#include <vector>

#include "stddef.hpp"
#include "bezier.hpp"
#include "span.hpp"

namespace wry::cff {
    
    std::map<int, std::vector<simd_float4x2>> parse(span<byte const>);

    struct Handle;

    Handle const* parse_CFF(span<byte const>);

    std::vector<bezier4> path_for_glyph_index(Handle const*, int glyph_index);

} // namespace wry::cff


#endif /* cff_hpp */
