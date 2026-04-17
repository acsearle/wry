//
//  type_traits.hpp
//  client
//
//  Created by Antony Searle on 25/6/2023.
//

#ifndef type_traits_hpp
#define type_traits_hpp

#include <type_traits>

namespace wry {
    
    // future std
    
    template<typename From, typename To>
    struct copy_const {
        using type = To;
    };
    
    template<typename From, typename To>
    struct copy_const<const From, To> {
        using type = std::add_const_t<To>;
    };
    
    template<typename From, typename To>
    using copy_const_t = typename copy_const<From, To>::type;
    
    // # Relocate
    //
    // A type T is concept Relocatable if
    //
    //     new (dst) T(std::move(*src));
    //     src->~T();
    //
    // is equivalent to
    //
    //     std::memcpy(dst, src, sizeof(T));
    //
    // All basic types and the great majority of standard library types,
    // including memory-owning containers and smart pointers, are Relocatable.
    // Notable exceptions are the non-Moveable std::mutex.
    
    template<typename T>
    struct is_relocatable 
    : std::is_nothrow_move_constructible<T> {
    };
    
    template<typename T>
    constexpr bool is_relocatable_v = is_relocatable<T>::value;
    
    // # Rank and extent
    //
    // It is not permitted to specialize std::rank and std::extent so we make
    // local customization points for them, and customize them for our vector,
    // Array, image, matrix containers and views
        
    template<typename T>
    struct Rank : std::rank<T> {};
    
    template<typename T>
    constexpr inline std::size_t rank_v = Rank<T>::value;
        
    template<typename T, unsigned N = 0>
    struct extent : std::extent<T, N> {};

    template<typename T, unsigned N = 0>
    constexpr inline std::size_t extent_v = extent<T, N>::value;

    /*
    using tag_scalar = std::integral_constant<std::size_t, 0>;
    using tag_vector = std::integral_constant<std::size_t, 1>;
    using tag_matrix = std::integral_constant<std::size_t, 2>;
     */
    
} // namespace wry

#endif /* type_traits_hpp */
