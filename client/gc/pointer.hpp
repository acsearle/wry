//
//  pointer.hpp
//  client
//
//  Created by Antony Searle on 2/6/2024.
//

#ifndef pointer_hpp
#define pointer_hpp

#include <cstdint>

namespace gc::pointer_tools {
    
    // Architecture-specific pointer tools
    
    
    // - 64 bit pointers
    // - 48 bit address space
    // - top 17 bits must be equal
    // - alignment by bytes

    using S = std::intptr_t;
    using U = std::uintptr_t;

    template<typename T> constexpr U LOW = alignof(T) - 1;
    template<typename T> constexpr U MID = 0x0000'8000'0000'0000 - alignof(T);
    constexpr U HIGH = 0xFFFF'8000'0000'0000;

    
    

    
    
    
} // namespace gc::pointer_tools

#endif /* pointer_hpp */
