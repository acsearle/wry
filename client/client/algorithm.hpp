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
    
    // libc++ is missing std::lexicographical_compare_three_way
    //
    // https://en.cppreference.com/w/cpp/algorithm/lexicographical_compare_three_way
    
    template<typename InputIt1, typename Sentinel1,
             typename InputIt2, typename Sentinel2,
             typename Compare = std::compare_three_way>
    constexpr auto 
    lexicographical_compare_three_way(InputIt1 first1, Sentinel1 last1,
                                      InputIt2 first2, Sentinel2 last2,
                                      Compare compare = Compare{})
    -> decltype(compare(*first1, *first2))
    {
        using return_type = decltype(compare(*first1, *first2));
        static_assert(std::disjunction_v<
                      std::is_same<return_type, std::strong_ordering>,
                      std::is_same<return_type, std::weak_ordering>,
                      std::is_same<return_type, std::partial_ordering>>,
                      "The return type must be a comparison category type.");
        for (;;) {
            bool exhaust1 = (first1 == last1);
            bool exhaust2 = (first2 == last2);
            if (exhaust1 || exhaust2) {
                return (!exhaust1
                        ? std::strong_ordering::greater
                        : (!exhaust2
                           ? std::strong_ordering::less:
                           std::strong_ordering::equal));
            }
            return_type c = compare(*first1, *first2);
            if (c != 0)
                return c;
            ++first1;
            ++first2;
        }        
    }
    
    // Extend standard algorithms to allow checking for second range exhaustion
    
    auto copy(auto first, auto last, auto d_first, auto d_last) -> decltype(d_first) {
        for (; first != last; ++first, ++d_first) {
            assert(d_first != d_last);
            *d_first = *first;
        }
        postcondition(d_first == d_last);
        return d_first;
    }
    
    auto swap_ranges(auto first1, auto last1, auto first2, auto last2) -> decltype(first2) {
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
