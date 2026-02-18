//
//  text.mm
//  client
//
//  Created by Antony Searle on 28/9/2024.
//

#include "text.hpp"

namespace wry {

float2 drawOverlay_draw_text(const Font* _font,
                             SpriteAtlas* _atlas,
                             rect<float> x,
                             StringView v,
                             RGBA8Unorm_sRGB color) {
    
    auto valign = (_font->height + _font->ascender + _font->descender) / 2; // note descender is negative
    
    auto xy = x.a;
    xy.y += valign;
    while (!v.empty()) {
        auto c = v.front();
        v.pop_front();
        auto q = _font->charmap.find(c);
        if (q != _font->charmap.end()) {
            
            if (xy.x + q->second.advance > x.b.x) {
                xy.x = x.a.x;
                xy.y += _font->height;
            }
            if (xy.y - _font->descender > x.b.y) {
                return xy;
            }
            
            wry::Sprite s = q->second.sprite_;
            _atlas->push_sprite(s + xy, color);
            xy.x += q->second.advance;
            
        } else if (c == '\n') {
            xy.x = x.a.x;
            xy.y += _font->height;
        }
    }
    xy.y -= valign;
    return xy;
}


} // namespace wry
