//
//  cstring.hpp
//  client
//
//  Created by Antony Searle on 28/9/2023.
//

#ifndef cstring_hpp
#define cstring_hpp

#include <cstring>

namespace wry {
    
    // # strlt
    //
    // Two-way compares two null-terminated byte strings lexicographically.
    //
    // strcmp's three-way comparison is more informative but not directly
    // compatible with STL conventions
    
    inline bool strlt(const char* s1, const char* s2) {
        for (; *s1 && (*s1 == *s2); ++s1, ++s2)
            ;
        return ((unsigned char) *s1) < ((unsigned char) *s2);
    }

    
    // # memswap
    //
    // Swaps the bytes of two disjoint regions.  Compilers seem to do a decent
    // job of optimizing the inner loop.
    //
    // TODO: Memswap is a repeated temptation but never ends up being the right
    // solution in the long term
    
    inline void memswap(void* __restrict__ lhs,
                        void* __restrict__ rhs,
                        size_t count) {
        unsigned char* first1 = (unsigned char*) lhs;
        unsigned char* first2 = (unsigned char*) rhs;
        unsigned char* last1 = first1 + count;
        // rely on the compiler to optimize this into larger blocks
        while (first1 != last1) {
            unsigned char temporary = *first1;
            *first1 = *first2;
            *first2 = temporary;
            ++first1;
            ++first2;
        }
    }
    
} // namespace wry

#endif /* cstring_hpp */
