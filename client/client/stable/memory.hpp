//
//  memory.hpp
//  client
//
//  Created by Antony Searle on 9/10/2023.
//

#ifndef memory_hpp
#define memory_hpp

#include <memory>

#include "stddef.hpp"

namespace wry {
    
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

#endif /* memory_hpp */
