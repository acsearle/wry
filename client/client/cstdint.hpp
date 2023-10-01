//
//  cstdint.hpp
//  client
//
//  Created by Antony Searle on 1/10/2023.
//

#ifndef cstdint_hpp
#define cstdint_hpp

#include <cstdint>

using int128_t = signed __int128;
using uint128_t = unsigned __int128;

namespace std {
    
    // likely illegal
    
    using ::int128_t;
    using ::uint128_t;
    
} // namespace std


#define WRY_X_OF_T_FOR_T_IN_SIGNED_FIXED_WITDH_INTEGER_TYPES \
    X(int8_t) X(int16_t) X(int32_t) X(int64_t)

#define WRY_X_OF_T_FOR_T_IN_UNSIGNED_FIXED_WITDH_INTEGER_TYPES \
    X(uint8_t) X(uint16_t) X(uint32_t) X(uint64_t)

#define WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES \
    WRY_X_OF_T_FOR_T_IN_SIGNED_FIXED_WITDH_INTEGER_TYPES \
    WRY_X_OF_T_FOR_T_IN_UNSIGNED_FIXED_WITDH_INTEGER_TYPES

#endif /* cstdint_hpp */
