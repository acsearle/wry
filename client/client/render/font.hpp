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

        // The UI's pixels per point the glyphs were rasterized for
        // (GuiContext::pixels_per_point): every metric above is in
        // drawable pixels at that scale.
        float pixels_per_point;

        // The em, in points.  14.45 pt at the hosts' default 2 px/pt is
        // the 28.9 px em the font was tuned at (font.mm).
        static constexpr float em_points = 14.45f;

        // The largest pixels_per_point the font region of the atlas
        // holds a full rasterization at; build_font clamps to it.
        static constexpr float max_pixels_per_point = 4.0f;

    };

    // Rasterize the printable-ASCII glyphs at Font::em_points *
    // pixels_per_point pixels to the em into the atlas's font region
    // (SpriteAtlas::font_region), which is first emptied: a rebuild
    // replaces the previous font's glyphs, so at most one Font built by
    // this function is valid at a time.
    Font build_font(SpriteAtlas&, float pixels_per_point);

    
    
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
    
    

}

#endif /* font_hpp */
