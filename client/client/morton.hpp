//
//  morton.hpp
//  client
//
//  Created by Antony Searle on 24/11/2024.
//

#ifndef morton_hpp
#define morton_hpp

#include <cstdint>

namespace wry {
    
    using std::uint32_t;
    using std::uint64_t;
    
    // https://en.wikipedia.org/wiki/Z-order_curve
    constexpr bool _less_msb(uint32_t a, uint32_t b) {
        return (a < b) && (a < (a ^ b));
    }
    
    constexpr bool _less_msb_by_clz(uint32_t a, uint32_t b) {
        return __builtin_clz(b) < __builtin_clz(a);
    }
    
    
}

#endif /* morton_hpp */
