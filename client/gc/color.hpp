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

namespace wry::gc {
    
    enum Color {
        /* WHITE or BLACK = 0, */
        /* BLACK or WHITE = 0 */
        COLOR_GRAY = 2,
        COLOR_RED = 3,
    }; // enum Color
    
    std::pair<Color, Color> color_white_black();
    Color color_alloc();
    
    bool color_compare_exchange_white_black(Atomic<Color>&);
    Color _color_white_to_black_color_was(Atomic<Color>&);
    
} // namespace wry::gc

#endif /* color_hpp */
