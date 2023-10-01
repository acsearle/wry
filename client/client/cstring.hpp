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
    
    // # memswap
    //
    // Swaps the bytes of two disjoint regions
    
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
