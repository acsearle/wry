//
//  algorithm.hpp
//  client
//
//  Created by Antony Searle on 28/9/2023.
//

#ifndef algorithm_hpp
#define algorithm_hpp

#include <algorithm>
#include <compare>

#include "assert.hpp"
#include "stddef.hpp"

namespace wry {
    
    // Extend standard algorithms to allow checking for second range exhaustion
    
    auto copy_checked(auto first, auto last, auto d_first, auto d_last) -> decltype(d_first) {
        for (; first != last; ++first, ++d_first) {
            assert(d_first != d_last);
            *d_first = *first;
        }
        postcondition(d_first == d_last);
        return d_first;
    }
    
    auto swap_ranges_checked(auto first1, auto last1, auto first2, auto last2) -> decltype(first2) {
        for (;; ++first1, ++first2) {
            bool exhaust1 = (first1 == last1);
            bool exhaust2 = (first2 == last2);
            assert(exhaust1 == exhaust2);
            if (exhaust1)
                return first2;
            using std::iter_swap;
            iter_swap(first1, first2);
        }
    }
    
} // namespace wry

#endif /* algorithm_hpp */
