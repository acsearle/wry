//
//  font.hpp
//  client
//
//  Created by Antony Searle on 25/6/2023.
//

#ifndef font_hpp
#define font_hpp

#include <map> // fixme

#include "atlas.hpp"
#include "string.hpp"

namespace wry {
    
    struct font {
        
        struct glyph {
            sprite sprite_;
            float advance;
        };
        
        std::map<u32, glyph> charmap;
        
        float ascender;
        float descender;
        float height;
        
    };
    
    font build_font(atlas&);
    
}

#endif /* font_hpp */
