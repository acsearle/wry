//
//  text.mm
//  client
//
//  Created by Antony Searle on 28/9/2024.
//

#include "text.hpp"

namespace wry {

    float text_run_width(const Font* font, StringView v) {
        if (!font) return 0.0f;
        float w = 0.0f;
        while (!v.empty()) {
            auto c = v.front();
            v.pop_front();
            auto q = font->charmap.find(c);
            if (q != font->charmap.end()) {
                w += q->second.advance;
            }
        }
        return w;
    }

} // namespace wry
