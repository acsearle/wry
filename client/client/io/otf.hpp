//
//  otf.hpp
//  client
//
//  Created by Antony Searle on 26/3/2026.
//

#ifndef otf_hpp
#define otf_hpp

// Open Type Format
//
// https://learn.microsoft.com/en-us/typography/opentype/spec/

#include <string_view>

#include "span.hpp"
#include "stddef.hpp"
#include "utility.hpp"
#include "bezier.hpp"

namespace wry::otf {

    struct Handle {

        static Handle parse(span<byte const>);

        struct Inner;
        Inner* _inner;

        void swap(Handle& other) {
            using std::swap;
            std::swap(_inner, other._inner);
        }

        Handle()
        : _inner(nullptr) {
        }

        Handle(Handle const&) = delete;

        Handle(Handle&& other)
        : _inner(take(other._inner)) {
        }

        ~Handle();

        Handle& operator=(Handle const&) = delete;

        Handle& operator=(Handle&& other) {
            Handle(std::move(other)).swap(*this);
            return *this;
        }

        struct Metrics {
            float ascender;
            float descender;
            float line_gap;
        };

        Metrics metrics_for_face() const;
        int units_per_em() const;

        // Glyph access.  Coordinates are in font design units; divide by
        // units_per_em() to normalize.
        int glyph_index_for_character(int c) const;
        std::vector<bezier4> outline_for_glyph_index(int glyph_index) const;
        std::vector<bezier4> outline_for_character(int c) const;
        float advance_for_glyph_index(int glyph_index) const;
        float advance_for_character(int c) const;

        // The 'SVG ' table document holding the glyph, or empty if the
        // font has no such table or the glyph has no SVG.  The bytes may be
        // gzip-compressed (they start 1F 8B).  svg::parse_otf_glyph turns a
        // document into Layers of the same curves outline_for_glyph_index
        // returns, in the same design-unit, y-up frame.
        std::string_view svg_document_for_glyph_index(int glyph_index) const;

    };

    // The same lookup over a raw 'SVG ' table; exposed for testing.
    std::string_view svg_document_in_table(span<byte const> svg_table, int glyph_index);

} // namespace wry::otf

#endif /* otf_hpp */
