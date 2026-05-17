//
//  cff.hpp
//  client
//
//  Created by Antony Searle on 26/3/2026.
//

#ifndef cff_hpp
#define cff_hpp

// The Compact Font Format

// https://adobe-type-tools.github.io/font-tech-notes/pdfs/5176.CFF.pdf

// Type2 Charstring Format

// https://adobe-type-tools.github.io/font-tech-notes/pdfs/5177.Type2.pdf

#include <map>
#include <vector>

#include "stddef.hpp"
#include "bezier.hpp"
#include "span.hpp"

namespace wry::cff {

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

        operator bool() const { return (bool)_inner; }

        std::vector<bezier4> outline_for_glyph_index(int glyph_index) const;

    };

} // namespace wry::cff


#endif /* cff_hpp */
