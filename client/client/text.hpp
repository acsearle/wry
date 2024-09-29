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

float2 drawOverlay_draw_text(const wry::Font* _font,
                             wry::SpriteAtlas* _atlas,
                             wry::rect<float> x,
                             wry::StringView v,
                             wry::RGBA8Unorm_sRGB color);

} // namespace wry

#endif /* text_hpp */
