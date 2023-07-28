//
//  utility.hpp
//  client
//
//  Created by Antony Searle on 25/6/2023.
//

#ifndef utility_hpp
#define utility_hpp

#include <bit>
#include <memory>
#include <utility>

#include "type_traits.hpp"

namespace wry {
    
    // from C++23
    
    template<typename T, typename U>
    [[nodiscard]] constexpr auto&& forward_like(U&& value) {
        using T2 = std::remove_reference_t<T>;
        using U2 = std::remove_reference_t<U>;
        using U3 = std::conditional_t<std::is_const_v<T2>, std::add_const_t<U2>, U2>;
        using U4 = std::conditional_t<std::is_volatile_v<T2>, std::add_volatile_t<U3>, U3>;
        using U5 = std::conditional_t<std::is_lvalue_reference_v<T>,
        std::add_lvalue_reference_t<U4>, std::add_rvalue_reference_t<U4>>;
        return static_cast<U5>(value);
    }
    
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
    
    // from Rust
    
    using u8 = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;

    using i8 = std::int8_t;
    using i16 = std::int16_t;
    using i32 = std::int32_t;
    using i64 = std::int64_t;
    
    using isize = std::ptrdiff_t;
    using usize = std::size_t;
    
    using iaddr = std::intptr_t;
    using uaddr = std::uintptr_t;
    
    namespace detail {
        
        template<typename F, typename X>
        struct fold_helper;
        
        template<typename F, typename X>
        constexpr fold_helper<F, X> make_fold_helper(F& f, X&& x) {
            return fold_helper<F, X>{f, std::forward<X>(x)};
        }
        
        template<typename F, typename X>
        struct fold_helper {
            
            F& _f;
            X _x;
            
            constexpr X release() { return std::forward<X>(_x); }
            
            template<typename Y>
            constexpr auto operator*(fold_helper<F, Y> y) {
                assert(std::addressof(_f) == std::addressof(y._f));
                return make_fold_helper(_f, _f(release(), y.release()));
            }
            
        }; // struct fold_helper
        
    } // namespace detail
    
    template<typename F, typename... Args>
    constexpr decltype(auto) fold_right(F&& f, Args&&... args) {
        return (detail::make_fold_helper(f, std::forward<Args>(args)) * ...).release();
    }
    
    template<typename F, typename... Args>
    constexpr decltype(auto) fold_left(F&& f, Args&&... args) {
        return (... * detail::make_fold_helper(f, std::forward<Args>(args))).release();
    }
    
    
    // # min and max
    //
    // Extends std::min and std::max to an arbitrary number of arguments, to
    // accept mixed types and still return a reference where possible, and to
    // perform a stable comparison in the sense that when there are multiple
    // minimum (maximum) values, the leftmost (rightmost) such value in the
    // argument list is returned.
    
    template<typename... Args>
    decltype(auto) min(Args&&... args) {
        return fold_left([](auto&& a, auto&& b) -> decltype(auto) {
            return (b < a) ? std::forward<decltype(b)>(b) : std::forward<decltype(a)>(a);
        }, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    constexpr decltype(auto) max(Args&&... args) {
        return fold_right([](auto&& a, auto&& b) -> decltype(auto) {
            return (a < b) ? std::forward<decltype(b)>(b) : std::forward<decltype(a)>(a);
        }, std::forward<Args>(args)...);
    }
    
    
    // # shift_left, shift_right, rotate_left, rotate_right
    //
    // Moves the values of its arguments one place left, or right, discarding
    // the last value or moving it back to the vacated beginning.
    //
    // rotate_* is a fundamental operation in linked list manipulation
    //
    // `exchange` may be implemented in terms of `shift_left`
    
    template<typename A, typename... B>
    void shift_left(A& a, B&&... b) {
        (void) fold_left([](auto& a, auto&& b) -> auto& {
            a = std::move(b);
            return b;
        }, a, std::forward<B>(b)...);
    }
    
    template<typename A, typename... B>
    void shift_right(A&& a, B&... b) {
        (void) fold_right([](auto&& a, auto& b) -> auto& {
            b = std::move(a);
            return a;
        }, std::forward<A>(a), b...);
    }
    
    template<typename A, typename... B>
    void rotate_left(A& a, B&... b) {
        A c = std::move(a);
        shift_left(a, b..., std::move(c));
    }
    
    template<typename A, typename... B>
    void rotate_right(A& a, B&... b) {
        A c = std::move(a);
        shift_right(std::move(c), b..., a);
    }
    
    // # Exchange
    //
    // Extends std::exchange to any number of arguments
    
    template<typename A, typename... B>
    A exchange(A& a, B&&... b) {
        A c = std::move(a);
        shift_left(a, std::forward<B>(b)...);
        return c;
    }
    
    
    
    // # memswap
    //
    // Always enticing but rarely the best way of doing things, swaps the bytes
    // of two disjoint regions
    inline void memswap(void* __restrict__ lhs, void* __restrict__ rhs, std::size_t count) {
        unsigned char* first1 = static_cast<unsigned char*>(lhs);
        unsigned char* first2 = static_cast<unsigned char*>(rhs);
        unsigned char* last1 = first1 + count;
        while (first1 != last1) {
            unsigned char temporary = *first1;
            *first1 = *first2;
            *first2 = temporary;
            ++first1;
            ++first2;
        }
    }

    // # relocate
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
    
    template<typename T>
    void relocate/*_one*/(const T* source, T* destination) {
        std::memcpy(destination, source, sizeof(T));
    }
        
    template<typename T, typename N>
    T* relocate_n(const T* first, N count, T* d_first, bool disjoint = false) {
        if (disjoint)
            std::memcpy(d_first, first, sizeof(T) * count);
        else
            std::memmove(d_first, first, sizeof(T) * count);
        return d_first + count;
    }
    
    template<typename T>
    T* relocate(const T* first, const T* last, T* d_first, bool disjoint = false) {
        return relocate_n(first, last - first, d_first, disjoint);
    }
        
    template<typename T>
    T* relocate_backward(const T* first, const T* last, T* d_last, bool disjoint = false) {
        auto count = last - first;
        T* d_first = d_last - count;
        relocate_n(first, count, d_first, disjoint);
        return d_first;
    }
        
    template<typename T, typename N>
    T* relocate_backward_n(N count, const T* last, T* d_last, bool disjoint = false) {
        const T* first = last - count;
        T* d_first = d_last - count;
        relocate_n(first, count, d_first, disjoint);
        return d_first;
    }
    
    
    
    template<typename T, typename U>
    T* allocate(U& count) noexcept {
        if (count <= 0)
            return nullptr;
        count = (U) std::bit_ceil((size_t) (count | 15));
        T* ptr = (T*) operator new(sizeof(T) * count);
        assert(ptr);
        return ptr;
    }
    
    inline void deallocate(void* ptr) noexcept {
        operator delete(ptr);
    }
    
} // namespace wry

#endif /* utility_hpp */
