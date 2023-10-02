//
//  chartype.hpp
//  client
//
//  Created by Antony Searle on 28/9/2023.
//

#ifndef chartype_hpp
#define chartype_hpp

#include <cassert>
#include <cctype>

namespace wry {
    
    // # Related predicates
    
    inline constexpr int isascii(int ch) {
        return !(ch & 0xffffff80);
    }
    
    inline constexpr int isuchar(int ch) {
        return !(ch & 0xffffff00);
    }
    
    inline constexpr int isunicode(int ch) {
        return ((ch <= 0x0010ffff)    // bounds
                && ((ch & 0xfffff800) // UTF-16 surrogate half
                    != 0x0000d800));
    }
    
    inline constexpr int isunderscore(int ch) {
        return ch == '_';
    }
    
    inline constexpr int isnewline(int ch) {
        return (ch == '\n') || (ch == '\r');
    }
    
    inline constexpr int isalnum_(int ch) {
        assert(isuchar(ch));
        return isalnum(ch) || isunderscore(ch);
    }
    
    inline constexpr int isalpha_(int ch) {
        assert(isuchar(ch));
        return isalpha(ch) || isunderscore(ch);
    }
    
}

#endif /* chartype_hpp */
