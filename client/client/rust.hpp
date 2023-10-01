//
//  rust.hpp
//  client
//
//  Created by Antony Searle on 24/9/2023.
//

#ifndef rust_hpp
#define rust_hpp

#include <cstddef>
#include <utility>

#include "cstdint.hpp"

typedef float f32;
typedef double f64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef int128_t i128;
typedef ptrdiff_t isize;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint128_t u128;
typedef size_t usize;

template<typename T>
struct slice {
    
    // note that "char" in Rust is a u32
    
    T* _ptr;
    usize _len;
    
    const T* as_ptr() { return _ptr; }
    T* as_mut_ptr() { return _ptr; }
    
    usize len() const { return _len; }
    
};

using str = slice<const u8>;

struct unit {};


namespace rust {
       
};

#endif /* rust_hpp */
