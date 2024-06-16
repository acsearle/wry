//
//  color.hpp
//  client
//
//  Created by Antony Searle on 16/6/2024.
//

#ifndef color_hpp
#define color_hpp

#include <utility>

#include "atomic.hpp"

namespace wry {
    
    namespace gc {
        
        enum class Color {
            WHITE = 0,
            BLACK = 1,
            GRAY  = 2,
            RED   = 3,
        }; // enum class Color
        
        template<typename T>
        struct Encoded;
        
        template<>
        struct Encoded<Color> {
            int _encoded_color;
        };
        
    }

    template<>
    struct Atomic<gc::Encoded<gc::Color>> {
        
        Atomic<int> _encoded_color;
        
        explicit Atomic();
        gc::Color load() const;
        bool compare_exchange(gc::Color& expected, gc::Color desired);
        
    };
        
} // namespace wry::gc

#endif /* color_hpp */
