//
//  font.hpp
//  client
//
//  Created by Antony Searle on 25/6/2023.
//

#ifndef font_hpp
#define font_hpp

#include "SpriteAtlas.hpp"
#include "string.hpp"
#include "table.hpp"
#include "ShaderTypes.h"

namespace wry {
    
    struct Font {
        
        struct Glyph {
            Sprite sprite_;
            float advance;
        };
        
        Table<char32_t, Glyph> charmap;
        
        float ascender;
        float descender;
        float height;

        // Width in pixels of a vertical stem (measured from 'I'); the
        // natural thickness for rules and borders that sit beside text.
        float stem_width;

        // Height in pixels of a flat-topped capital (measured from 'H')
        // above the baseline.  All-caps labels center this band.
        float cap_height;
        
    };
    
    Font build_font(SpriteAtlas&);

    
    
    struct Font2 {
        
        struct Glyph {
            uint glyph_index;
            float advance;
        };
        
        Table<char32_t, Glyph> charmap;
        
        float ascender;
        float descender;
        float height;
        
        std::vector<otf::GlyphData> glyph_data;
        std::vector<otf::CubicBezier> cubic_bezier;
        
    };
    
    Font2 build_font2();

    
    std::tuple<float2, matrix_view<R8Unorm>, float2> get_glyph(char32_t);
    
    

}

#endif /* font_hpp */
