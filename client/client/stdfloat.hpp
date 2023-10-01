//
//  stdfloat.hpp
//  client
//
//  Created by Antony Searle on 1/10/2023.
//

#ifndef stdfloat_hpp
#define stdfloat_hpp

#include "cstring.hpp"

using float16_t = __fp16; // <-- _Float16 clashes with NEON
using float32_t = float;
using float64_t = double;

namespace std {
    
    // From C++23
    
    using float16_t = ::float16_t;
    using float32_t = ::float32_t;
    using float64_t = ::float64_t;
    
    // float128_t
    
    // bfloat16_t

} // namespace std

#define WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES \
    X(float16_t) X(float32_t) X(float64_t)

#endif /* stdfloat_hpp */
