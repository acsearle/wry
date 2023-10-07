//
//  stdfloat.hpp
//  client
//
//  Created by Antony Searle on 1/10/2023.
//

#ifndef stdfloat_hpp
#define stdfloat_hpp

// OpenCL

typedef _Float16 half;

namespace rust {
    
    using f32 = float;
    using f64 = double;
    
} // namespace rust

namespace wry {
    
    // C++23 stdfloat
    
    using float16_t = _Float16;
    using float32_t = float;
    using float64_t = double;
    struct float128_t;
    struct bfloat16_t;
    
    // OpenCL
    
    using half = _Float16;
    
} // namespace wry

#define WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES \
    X(float16_t) X(float32_t) X(float64_t)

#endif /* stdfloat_hpp */
