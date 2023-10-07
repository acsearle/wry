//
//  stdint.hpp
//  client
//
//  Created by Antony Searle on 1/10/2023.
//

#ifndef stdint_hpp
#define stdint_hpp

#include <cstdint>

// OpenCL

typedef unsigned char  uchar;
typedef unsigned short ushort;
typedef unsigned int   uint;
typedef unsigned long  ulong;

namespace rust {

    using i8   = ::std::int8_t;
    using i16  = ::std::int16_t;
    using i32  = ::std::int32_t;
    using i64  = ::std::int64_t;
    using i128 = signed __int128;

    using u8   = ::std::uint8_t;
    using u16  = ::std::uint16_t;
    using u32  = ::std::uint32_t;
    using u64  = ::std::uint64_t;
    using u128 = unsigned __int128;

} // namespace rust

namespace wry {
    
    // use cstdint names
    
    using ::std::int8_t;
    using ::std::int16_t;
    using ::std::int32_t;
    using ::std::int64_t;

    using ::std::uint8_t;
    using ::std::uint16_t;
    using ::std::uint32_t;
    using ::std::uint64_t;

    // extend cstdint

    using int128_t  =   signed __int128;
    using uint128_t = unsigned __int128;

    // use rust names
    
    using ::rust::i8;
    using ::rust::i16;
    using ::rust::i32;
    using ::rust::i64;
    using ::rust::i128;

    using ::rust::u8;
    using ::rust::u16;
    using ::rust::u32;
    using ::rust::u64;
    using ::rust::u128;
    
    // use opencl names
    
    using ::uchar;
    using ::ushort;
    using ::uint;
    using ::ulong;

} // namespace wry


#define WRY_X_OF_T_FOR_T_IN_SIGNED_FIXED_WITDH_INTEGER_TYPES \
    X(int8_t) X(int16_t) X(int32_t) X(int64_t)

#define WRY_X_OF_T_FOR_T_IN_UNSIGNED_FIXED_WITDH_INTEGER_TYPES \
    X(uint8_t) X(uint16_t) X(uint32_t) X(uint64_t)

#define WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES \
    WRY_X_OF_T_FOR_T_IN_SIGNED_FIXED_WITDH_INTEGER_TYPES \
    WRY_X_OF_T_FOR_T_IN_UNSIGNED_FIXED_WITDH_INTEGER_TYPES

#endif /* stdint_hpp */
