//
//  ctype.hpp
//  client
//
//  Created by Antony Searle on 28/9/2023.
//

#ifndef ctype_hpp
#define ctype_hpp

#include <cctype>
#include <cstdio>

#include "assert.hpp"

namespace wry {
    
    // Additional character predicates

    constexpr int isascii(int ch) {
        return !(ch & 0xFFFFFF80);
    }
    
    constexpr int isuchar(int ch) {
        return !(ch & 0xFFFFFF00);
    }

    // Characters passing this test are safe to pass to <cctype> functions
    constexpr int issafe(int ch) {
        return isuchar(ch) || (ch == EOF);
    }

    constexpr int isunderscore(int ch) {
        return ch == '_';
    }
    
    constexpr int isnewline(int ch) {
        return (ch == '\n') || (ch == '\r');
    }
    
    constexpr int isalnum_(int ch) {
        return (issafe(ch) && isalnum(ch)) || isunderscore(ch);
    }
    
    constexpr int isalpha_(int ch) {
        precondition(issafe(ch));
        return (issafe(ch) && isalpha(ch)) || isunderscore(ch);
    }

} // namespace wry

#endif /* ctype_hpp */
