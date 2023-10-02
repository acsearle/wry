//
//  algorithm.hpp
//  client
//
//  Created by Antony Searle on 28/9/2023.
//

#ifndef algorithm_hpp
#define algorithm_hpp

#include <algorithm>
#include <cassert>

#include "stddef.hpp"

namespace wry {
    
    // # From C++23
    //
    // https://en.cppreference.com/w/cpp/algorithm/lexicographical_compare_three_way
    
    template<typename I1, typename I2, typename Cmp = std::compare_three_way>
    constexpr auto lexicographical_compare_three_way(I1 first1, I1 last1,
                                                     I2 first2, I2 last2,
                                                     Cmp comp = Cmp{}) -> decltype(comp(*first1, *first2)) {
        using ret_t = decltype(comp(*first1, *first2));
        static_assert(std::disjunction_v<
                      std::is_same<ret_t, std::strong_ordering>,
                      std::is_same<ret_t, std::weak_ordering>,
                      std::is_same<ret_t, std::partial_ordering>>,
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
            if (auto c = comp(*first1, *first2); c != 0)
                return c;
            ++first1;
            ++first2;
        }
        
    }
    
    // # Extend some algorithms to check for second range ending
    
    auto copy(auto first, auto last, auto d_first, auto d_last) -> decltype(d_first) {
        for (; first != last; ++first, ++d_first) {
            assert(d_first != d_last);
            *d_first = *first;
        }
        assert(d_first == d_last);
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
    
    // # Relocate
    //
    // A type `T` is _Relocatable_ if, for `T* dest, T *src`
    // ```
    //     std::construct_at(dest, std::move(*src));
    //     std::destroy_at(src);
    // ```
    // is equivalent to
    // ```
    //     std::memcpy(dest, src, sizeof(T));
    // ```
    // _Movable_ types are typically _Relocatable_.  A type must introspect and
    // store or publish its own address to become unrelocatable; mutexes are one
    // example of this behavior.  STL containers and even smart pointers are
    // movable.  Rust types are relocatable by default (see `Pin`).
    // Is there a non-contrived example of a type that is _Movable_ but not
    // _Relocatable_?
    //
    // In particular, we can perform bulk array relocates where
    // ```
    //     std::uninitialized_move(first, last, d_first);
    //     std::destroy(first, last)
    // ```
    // becomes
    // ```
    //     std::memmove(d_first, first, first - last);
    // ```
    // which avoids writing move-from states back to the source range just to
    // communicate to their destructors that no actions are needed.
    
    // `std::memmove` will do the right thing without having to worry about
    // forward or backward copying in the sense of `std::copy_backward`, but
    // we provide backward relocate operations for when it is more convenient
    // to specify `d_last`.
    //
    // An optional argument signals if the ranges are known to be disjoint so
    // we can use `std::memcpy` rather than `std::memmove`.  Since
    // `std::memmove` (probably) performs this check internally and falls back
    // to `std::memcpy` when possible, this argument should be set only when we
    // know at compile time that the ranges are disjoint.
    //
    // Note that AddressSanitizer can detect overlapping misuses of
    // `std::memcpy`.
    
    inline byte* relocate_n(const byte* first,
                            size_t count,
                            byte* d_first,
                            bool disjoint = false) {
        if (disjoint)
            std::memcpy(d_first, first, count);
        else
            std::memmove(d_first, first, count);
        return d_first + count;
    }
    
    inline byte* relocate(const byte* first,
                          const byte* last,
                          byte* d_first,
                          bool disjoint = false) {
        return relocate_n(first, last - first, d_first, disjoint);
    }
    
    template<typename T>
    T* relocate(const T* first, const T* last, T* d_first, bool disjoint = false) {
        return (T*) relocate((const byte*) first,
                             (const byte*) last,
                             (byte*) d_first,
                             disjoint);
    }
    
    template<typename T, typename N>
    T* relocate_n(const T* first, N count, T* d_first, bool disjoint = false) {
        return (T*) relocate_n((const byte*) first,
                               count * sizeof(T),
                               (byte*) d_first,
                               disjoint);
    }
    
    template<typename T>
    T* relocate_backward(const T* first, const T* last, T* d_last, bool disjoint = false) {
        const byte* first2 = (const byte*) first;
        const byte* last2 = (const byte*) last;
        byte* d_last2 = (byte*) d_last;
        ptrdiff_t count = last2 - first2;
        byte* d_first2 = d_last2 - count;
        relocate_n(first2, count, d_first2, disjoint);
        return (T*) d_first2;
    }
    
    template<typename T, typename N>
    T* relocate_backward_n(N count, const T* last, T* d_last, bool disjoint = false) {
        const byte* last2 = (const byte*) last;
        byte* d_last2 = (byte*) d_last;
        auto count2 = count * sizeof(T);
        const byte* first2 = last2 - count;
        byte* d_first2 = d_last2 - count;
        relocate_n(first2, count2, d_first2, disjoint);
        return (T*) d_first2;
    }
    
    template<typename T>
    T* relocate(const T* src, T* dest) {
        std::memcpy(dest, src, sizeof(T));
        return dest + 1;
    }
    
} // namespace wry

#endif /* algorithm_hpp */
