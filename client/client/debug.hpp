//
//  debug.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef debug_hpp
#define debug_hpp

#include <iostream>

#include "assert.hpp"
#include "typeinfo.hpp"

namespace wry {
    
    #define DUMP(X) std::cout << "(" << name_of<decltype((X))> <<  ") " #X " = " << (X) << std::endl;
        
    struct timer {
        
        std::uint64_t _begin;
        char const* _context;
        
        explicit timer(char const* context);
        timer(timer const&) = delete;
        ~timer();
        
    };

} // namespace wry

#endif /* debug_hpp */
