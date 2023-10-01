//
//  algorithm.hpp
//  client
//
//  Created by Antony Searle on 28/9/2023.
//

#ifndef algorithm_hpp
#define algorithm_hpp

#include <algorithm>

namespace wry {
    
    // # From C++23
    
    template<class I1, class I2, class Cmp>
    constexpr auto lexicographical_compare_three_way(I1 f1, I1 l1, I2 f2, I2 l2, Cmp comp) -> decltype(comp(*f1, *f2)) {
        using ret_t = decltype(comp(*f1, *f2));
        static_assert(std::disjunction_v<
                      std::is_same<ret_t, std::strong_ordering>,
                      std::is_same<ret_t, std::weak_ordering>,
                      std::is_same<ret_t, std::partial_ordering>>,
                      "The return type must be a comparison category type.");
        for (;;) {
            bool exhaust1 = (f1 == l1);
            bool exhaust2 = (f2 == l2);
            if (exhaust1 || exhaust2) {
                return (!exhaust1
                        ? std::strong_ordering::greater
                        : (!exhaust2
                           ? std::strong_ordering::less:
                           std::strong_ordering::equal));
            }
            if (auto c = comp(*f1, *f2); c != 0)
                return c;
            ++f1;
            ++f2;
        }
        
    }
    
    template<class I1, class I2>
    constexpr auto lexicographical_compare_three_way(I1 f1, I1 l1, I2 f2, I2 l2) {
        return lexicographical_compare_three_way(f1, l1, f2, l2, std::compare_three_way());
    }
    
    // # Extend some algorithms to check for second range ending
    
    template<typename InputIterator, typename InputSentinel, typename OutputIterator, typename OutputSentinel>
    OutputIterator copy(InputIterator first, InputSentinel last, OutputIterator d_first, OutputSentinel d_last) {
        for (; first != last; ++first, ++d_first) {
            assert(d_first != d_last);
            *d_first = *first;
        }
        assert(d_first == d_last);
        return d_first;
    }
    
    template<typename ForwardIt1, typename Sentinel1, typename ForwardIt2, typename Sentinel2>
    ForwardIt2 swap_ranges(ForwardIt1 first1, Sentinel1 last1, ForwardIt2 first2, Sentinel2 last2) {
        for (; first1 != last1; ++first1, ++first2) {
            assert(first2 != last2);
            using std::iter_swap;
            iter_swap(first1, first2);
        }
        assert(first2 == last2);
        return first2;
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
    
    inline unsigned char* relocate_n(const unsigned char* first,
                                     size_t count,
                                     unsigned char* d_first,
                                     bool disjoint = false) {
        if (disjoint)
            std::memcpy(d_first, first, count);
        else
            std::memmove(d_first, first, count);
        return d_first + count;
    }
    
    inline unsigned char* relocate(const unsigned char* first,
                                   const unsigned char* last,
                                   unsigned char* d_first,
                                   bool disjoint = false) {
        return relocate_n(first, last - first, d_first, disjoint);
    }
    
    template<typename T>
    T* relocate(const T* first, const T* last, T* d_first, bool disjoint = false) {
        return (T*) relocate((const unsigned char*) first,
                             (const unsigned char*) last,
                             (unsigned char*) d_first,
                             disjoint);
    }
    
    template<typename T, typename N>
    T* relocate_n(const T* first, N count, T* d_first, bool disjoint = false) {
        return (T*) relocate_n((const unsigned char*) first,
                               count * sizeof(T),
                               (unsigned char*) d_first,
                               disjoint);
    }
    
    template<typename T>
    T* relocate_backward(const T* first, const T* last, T* d_last, bool disjoint = false) {
        const unsigned char* first2 = (const unsigned char*) first;
        const unsigned char* last2 = (const unsigned char*) last;
        unsigned char* d_last2 = (unsigned char*) d_last;
        ptrdiff_t count = last2 - first2;
        unsigned char* d_first2 = d_last2 - count;
        relocate_n(first2, count, d_first2, disjoint);
        return (T*) d_first2;
    }
    
    template<typename T, typename N>
    T* relocate_backward_n(N count, const T* last, T* d_last, bool disjoint = false) {
        const unsigned char* last2 = (const unsigned char*) last;
        unsigned char* d_last2 = (unsigned char*) d_last;
        auto count2 = count * sizeof(T);
        const unsigned char* first2 = last2 - count;
        unsigned char* d_first2 = d_last2 - count;
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
