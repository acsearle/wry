//
//  ctype.hpp
//  client
//
//  Created by Antony Searle on 28/9/2023.
//

#ifndef ctype_hpp
#define ctype_hpp

#include <cctype>

#include "assert.hpp"

namespace wry {
    
    // Additional character predicates
    
    constexpr int isascii(int ch) {
        return !(ch & 0xFFFFFF80);
    }
    
    constexpr int isuchar(int ch) {
        return !(ch & 0xFFFFFF00);
    }
        
    constexpr int isunderscore(int ch) {
        return ch == '_';
    }
    
    constexpr int isnewline(int ch) {
        return (ch == '\n') || (ch == '\r');
    }
    
    constexpr int isalnum_(int ch) {
        precondition(isuchar(ch));
        return isalnum(ch) || isunderscore(ch);
    }
    
    constexpr int isalpha_(int ch) {
        precondition(isuchar(ch));
        return isalpha(ch) || isunderscore(ch);
    }

} // namespace wry

#endif /* ctype_hpp */
