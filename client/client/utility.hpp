//
//  utility.hpp
//  client
//
//  Created by Antony Searle on 25/6/2023.
//

#ifndef utility_hpp
#define utility_hpp

#include <bit>
#include <cassert>
#include <compare>
#include <memory>
#include <utility>

#include "type_traits.hpp"

namespace wry {

    // this is pretty objectionable but makes life much easier
    
#define FORWARD( X ) std::forward<decltype( X )>( X )
    
    // # from C++23
    
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
   
    // # heterogenous reduce
    //
    // Performs reduce over parameter packs, enabling variadic extensions to
    //
    //    std::min
    //    std::max
    //    std::exchange
    //
    // and other operations on argument lists such as
    //
    //    wry::shift_left
    //    wry::shift_right
    //    wry::rotate_left
    //    wry::rotate_right

    namespace _detail {
        
        template<typename F, typename X>
        struct _reduce_by_fold_helper {
            
            F& _f;
            X _x;
            
            _reduce_by_fold_helper(F& f, X&& x) : _f(f), _x(std::forward<X>(x)) {}
            
            constexpr X _release() { return std::forward<X>(_x); }
            
        }; // struct _fold_helper
        
        template<typename F, typename X>
        _reduce_by_fold_helper(F&, X&&) -> _reduce_by_fold_helper<F, X>;
        
        template<typename F, typename X, typename Y>
        constexpr auto operator*(_reduce_by_fold_helper<F, X>&& x,
                                 _reduce_by_fold_helper<F, Y>&& y) {
            assert(std::addressof(x._f) == std::addressof(y._f));
            return _reduce_by_fold_helper(x._f, x._f(x._release(), y._release()));
        }
        
    }
    
    template<typename F, typename... Args>
    constexpr decltype(auto) reduce_right(F&& f, Args&&... args) {
        return (_detail::_reduce_by_fold_helper(f, std::forward<Args>(args)) * ...)._release();
    }
    
    template<typename F, typename... Args>
    constexpr decltype(auto) reduce_left(F&& f, Args&&... args) {
        return (... * _detail::_reduce_by_fold_helper(f, std::forward<Args>(args)))._release();
    }
    
    
    // # min and max
    //
    // Extends std::min and std::max to an arbitrary number of arguments, to
    // accept mixed types and still return a reference where possible, and to
    // perform a stable comparison in the sense that when there are multiple
    // minimum (maximum) values, the leftmost (rightmost) such value in the
    // argument list is returned.
    
    template<typename... Args>
    constexpr decltype(auto) min(Args&&... args) {
        return reduce_left([](auto&& a, auto&& b) -> decltype(auto) {
            return (b < a) ? std::forward<decltype(b)>(b) : std::forward<decltype(a)>(a);
        }, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    constexpr decltype(auto) max(Args&&... args) {
        return reduce_right([](auto&& a, auto&& b) -> decltype(auto) {
            return (a < b) ? std::forward<decltype(b)>(b) : std::forward<decltype(a)>(a);
        }, std::forward<Args>(args)...);
    }
    
    
    // # shift_left, shift_right, rotate_left, rotate_right
    //
    // Moves the values of its arguments one place left, or right, discarding
    // the last value or moving it back to the vacated beginning.
    //
    // `rotate_*` is a fundamental operation in linked list manipulation
    //
    // `exchange` may be implemented in terms of `shift_left`
    
    template<typename A, typename... B>
    void shift_left(A& a, B&&... b) {
        (void) reduce_left([](auto& a, auto&& b) -> decltype(auto) {
            a = std::move(b);
            return b;
        }, a, std::forward<B>(b)...);
    }
    
    template<typename A, typename... B>
    void shift_right(A&& a, B&... b) {
        (void) reduce_right([](auto&& a, auto& b) -> auto& {
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
