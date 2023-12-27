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
    
    using std::from_chars_result;
    
    from_chars_result _from_chars_double(const char* first, const char* last, double& value);
    
    template<typename T>
    from_chars_result from_chars(const char* first, const char* last, T& value, int base = 10) {
        if constexpr (std::is_integral_v<T>) {
            return std::from_chars(first, last, value, base);
        } else {
            double value2 = value;
            std::from_chars_result result = wry::_from_chars_double(first, last, value2);
            if (result.ptr != first)
                value = value2;
            return result;
        }
    }
    
    
    // propagate mutability into to_chars_result
    
    struct to_chars_result {
        char* ptr;
        std::errc ec;
    };
    
    template<typename T>
    to_chars_result to_chars(char* first, char* last, T&& value) {
        std::to_chars_result a = std::to_chars(first, last, std::forward<T>(value));
        return to_chars_result{
            first += (a.ptr - first),
            a.ec
        };
    }
    
} // namespace wry

#endif /* charconv_hpp */
