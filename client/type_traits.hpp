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
    struct is_relocatable : std::is_nothrow_move_constructible<T> {};
    
    template<typename T>
    constexpr bool is_relocatable_v = is_relocatable<T>::value;
    
}

#endif /* type_traits_hpp */
