//
//  text.hpp
//  client
//
//  Created by Antony Searle on 28/9/2024.
//

#ifndef text_hpp
#define text_hpp

#include "font.hpp"
#include "SpriteAtlas.hpp"

namespace wry {

    // Sum of glyph advances for `v` in `font`.  Unknown characters are
    // skipped (advance 0).  Pure measurement -- no drawing.
    float text_run_width(const Font* font, StringView v);

} // namespace wry

#endif /* text_hpp */
