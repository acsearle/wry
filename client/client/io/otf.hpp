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
        : _inner(std::exchange(other._inner, nullptr)) {
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

    };

} // namespace wry::otf

#endif /* otf_hpp */
