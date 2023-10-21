//
//  font.hpp
//  client
//
//  Created by Antony Searle on 25/6/2023.
//

#ifndef font_hpp
#define font_hpp

#include "atlas.hpp"
#include "string.hpp"
#include "table.hpp"

namespace wry {
    
    struct font {
        
        struct glyph {
            sprite sprite_;
            float advance;
        };
        
        table<uint, glyph> charmap;
        
        float ascender;
        float descender;
        float height;
        
    };
    
    font build_font(atlas&);
    
    

    // source of alpha glyphs
    
    struct Face {
        
        virtual ~Face() = 0;
                
        std::unique_ptr<Face> with(string_view filename, float height);
        
        matrix_view<R8Unorm> operator[](char32_t);
        
    };
    
}

#endif /* font_hpp */
