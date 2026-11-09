//
//  decal_text.cpp
//  client
//
//  Created by Antony Searle on 2026-11-08.
//

#include <algorithm>
#include <cmath>

#include "decal_text.hpp"
#include "test.hpp"

namespace wry {

    float decal_text_advance(const DecalFont& font, std::string_view text) {
        float sum = 0.0f;
        for (char ch : text)
            if (const DecalFont::Glyph* g = font.find(ch))
                sum += g->advance;
        return sum;
    }

    DecalTextRun layout_decal_text(const DecalFont& font, std::string_view text,
                                   float cap_height, float max_width,
                                   DecalAlign align) {
        DecalTextRun run;
        if (font.cap_height <= 0.0f || cap_height <= 0.0f)
            return run;
        float em = cap_height / font.cap_height;   // run units per em
        const float natural = decal_text_advance(font, text) * em;
        if (max_width > 0.0f && natural > max_width) {
            run.scale = max_width / natural;
            em *= run.scale;
        }
        run.width = natural * run.scale;
        float pen = (align == DecalAlign::centre) ? -0.5f * run.width : 0.0f;
        for (char ch : text) {
            const DecalFont::Glyph* g = font.find(ch);
            if (!g)
                continue;
            if (g->size.x > 0.0f && g->size.y > 0.0f) {
                DecalGlyphPlacement p;
                p.size = g->size * em;
                p.centre = simd_make_float2(pen + (g->offset.x + 0.5f * g->size.x) * em,
                                            (g->offset.y - 0.5f * g->size.y) * em);
                p.uv_rect = g->uv_rect;
                run.glyphs.push_back(p);
            }
            pen += g->advance * em;
        }
        return run;
    }

    TileCaption tile_caption_layout(const DecalFont& font, std::string_view name) {
        constexpr float kCap = 2.3f;
        constexpr float kBaseline = 21.4f;
        constexpr float kWrapBelow = 0.75f;
        constexpr float kWrapCap = 1.7f;
        constexpr float kWrapBaselines[2] = {19.4f, 21.5f};
        TileCaption c;
        const float em = (font.cap_height > 0.0f) ? 1.0f / font.cap_height : 1.0f;
        const float natural = decal_text_advance(font, name) * em * kCap;
        const float shrink = natural > TileCaption::kWidth ? TileCaption::kWidth / natural : 1.0f;
        const size_t middle2 = name.size();   // twice the middle, compared with 2 i
        size_t cut = std::string_view::npos;
        for (size_t i = 0; i != name.size(); ++i)
            if (name[i] == ' ') {
                // The space nearest the middle; ties to the first, as
                // Python's min does
                auto distance = [&](size_t k) { return (2 * k > middle2) ? 2 * k - middle2 : middle2 - 2 * k; };
                if (cut == std::string_view::npos || distance(i) < distance(cut))
                    cut = i;
            }
        if (shrink < kWrapBelow && cut != std::string_view::npos) {
            c.lines = {std::string(name.substr(0, cut)), std::string(name.substr(cut + 1))};
            c.baselines = {kWrapBaselines[0], kWrapBaselines[1]};
            c.cap = kWrapCap;
        } else {
            c.lines = {std::string(name)};
            c.baselines = {kBaseline};
            c.cap = kCap;
        }
        return c;
    }

    define_test("decal_text") {
        // A synthetic font: every capital and the space one half em
        // wide, capitals 0.7 em tall in a 0.6 x 1.0 em padded image
        DecalFont font;
        font.cap_height = 0.7f;
        for (char ch = 'A'; ch <= 'Z'; ++ch) {
            DecalFont::Glyph& g = font.glyphs[(unsigned char)ch];
            g.present = true;
            g.advance = 0.5f;
            g.offset = simd_make_float2(-0.05f, 0.85f);
            g.size = simd_make_float2(0.6f, 1.0f);
            g.uv_rect = simd_make_float4((float)(ch - 'A') / 32.0f, 0.0f, 1.0f / 32.0f, 1.0f / 8.0f);
        }
        font.glyphs[' '].present = true;
        font.glyphs[' '].advance = 0.5f;

        require(decal_text_advance(font, "AB C") == 2.0f);
        // Unknown characters take no room
        require(decal_text_advance(font, "A\x01" "B") == 1.0f);

        // Cap 0.7 makes the em 1: "AB" is 1 wide, centred on the anchor
        DecalTextRun run = layout_decal_text(font, "AB", 0.7f, 0.0f, DecalAlign::centre);
        require(run.glyphs.size() == 2);
        require(run.scale == 1.0f && run.width == 1.0f);
        require(std::fabs(run.glyphs[0].centre.x - (-0.5f - 0.05f + 0.3f)) < 1e-6f);
        require(std::fabs(run.glyphs[1].centre.x - (0.0f - 0.05f + 0.3f)) < 1e-6f);
        require(std::fabs(run.glyphs[0].centre.y - 0.35f) < 1e-6f);
        require(run.glyphs[1].uv_rect.x == 1.0f / 32.0f);
        // The space is advance only
        run = layout_decal_text(font, "A B", 0.7f, 0.0f, DecalAlign::left);
        require(run.glyphs.size() == 2 && run.width == 1.5f);
        require(std::fabs(run.glyphs[1].centre.x - (1.0f - 0.05f + 0.3f)) < 1e-6f);
        // Too wide: shrunk uniformly to the width, about the anchor
        run = layout_decal_text(font, "ABCD", 0.7f, 1.0f, DecalAlign::left);
        require(run.scale == 0.5f && run.width == 1.0f);
        require(std::fabs(run.glyphs[0].size.y - 0.5f) < 1e-6f);

        // The tile caption rule: short names keep one line at cap 2.3
        TileCaption c = tile_caption_layout(font, "ADD");
        require(c.lines.size() == 1 && c.cap == 2.3f && c.baselines[0] == 21.4f);
        // 24 characters at 2.3 / 0.7 * 0.5 = 1.64 frame units each is
        // 39.4 wide, a shrink below 3/4: two lines at the middle space
        c = tile_caption_layout(font, "GREATER THAN OR EQUAL TO");
        require(c.lines.size() == 2 && c.cap == 1.7f);
        require(c.lines[0] == "GREATER THAN" && c.lines[1] == "OR EQUAL TO");
        // Too wide with no space: one line, shrunk by the layout
        c = tile_caption_layout(font, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        require(c.lines.size() == 1 && c.cap == 2.3f);
        co_return;
    };

} // namespace wry
