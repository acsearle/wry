//
//  charconv.hpp
//  client
//
//  Created by Antony Searle on 10/9/2023.
//

#ifndef charconv_hpp
#define charconv_hpp

#include <charconv>

namespace wry {

    // libc++ is missing the floating-point versions of from_chars
    
    std::from_chars_result _from_chars_double(const char* first, const char* last, double& value);
        
    std::from_chars_result from_chars(const char* first, const char* last, auto& value) {
        if constexpr (std::is_integral_v<decltype(value)>) {
            return std::from_chars(first, last, value);
        } else {
            double value2 = value;
            std::from_chars_result result = wry::_from_chars_double(first, last, value2);
            if (result.ptr == first)
                value = value2;
            return result;
        }
    }
   
    
}

#endif /* charconv_hpp */
