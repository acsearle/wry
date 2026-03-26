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

namespace wry::cff {
    
    using byte = unsigned char;
    
    void* parse(byte const*, byte const*);
    
} // namespace wry::cff


#endif /* cff_hpp */
