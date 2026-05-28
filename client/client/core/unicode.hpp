//
//  unicode.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef unicode_hpp
#define unicode_hpp

#include <compare>

#include "contiguous_view.hpp"
#include "assert.hpp"
#include "stddef.hpp"
#include "stdint.hpp"

namespace wry {

    // C++23 supports strings and literals of these forms:
    //
    // Ordinary literal encoding
    //     'c-char'          ->       char
    //     "s-char"          -> const char[N]
    //     'c-char sequence' ->       int
    //
    // Wide literal encoding
    //    L'c-char'          ->       wchar_t
    //    L'c-char'          -> const wchar_t[N]
    //    L'c-char sequence' ->       wchar_t
    //
    // UTF-8
    //   u8'c-char'          ->       char8_t     (0x0-0x7F only)
    //   u8"s-char"          -> const char8_t[N]
    //
    // UTF-16
    //    u'c-char'          ->       char16_t
    //    u"s-char"          -> const char16_t
    //
    // UTF-32
    //    U'c-char'          ->       char32_t
    //    U"s-char"          -> const char32_t[N]
    //
    //
    // The array length N is always in code units, including a zero terminator
    //
    // `char` is a distinct type from both `signed char` and `unsigned char`,
    // and its signedness is unspecified; typically signed on x86 and x64, and
    // unsigned on ARM and PowerPC
    //
    // `wchar_t` is a distinct integer type of unspecified width and signedness;
    // typically 16 bits and UTF-16 on Windows, and 32 bits and UTF-32
    // otherwise.
    //
    // `char8_t`, `char16_t` and `char32_t` are distinct types with underlying
    // types unsigned char, uint_least16_t and uint_least32_t.  In principle
    // they provide a type-system marker for the encoding, but UTF-8 validity
    // is a property of sequences of bytes, not of individual bytes, and so
    // cannot be enforced by an element type.  We do not use char8_t.
    //
    // Doctrine:
    //   - char is the storage type for UTF-8 bytes.
    //   - UTF-8 validity is an invariant of the *container* types String and
    //     StringView (and ContiguousView<const char> when used as a UTF-8
    //     view), not of individual char values.
    //   - char16_t is used for UTF-16 code units (JSON \uXXXX escapes,
    //     surrogate-pair handling).
    //   - char32_t is used for Unicode scalar values (code points).
    //   - std::byte is used for opaque binary data (file blobs, network
    //     buffers).  Bytes become char only after passing the UTF-8 invariant
    //     check at the boundary.
    //
    // Many text-based formats like JSON, CSV, OBJ, MTL have their delimiting
    // characters entirely within the ASCII character set and can thus be
    // parsed as bytes without reconstruction of multibyte characters; the
    // resulting substrings will be valid UTF-8 iff the source was.


    namespace utf16 {

        // UTF-16 surrogate-pair machinery.  Used by JSON's \uXXXX escape
        // decoder to recompose astral code points from two 16-bit halves.

        inline constexpr bool issurrogate(char16_t ch) {
            return (ch & 0xF800) == 0xD800;
        }

        inline constexpr bool ishighsurrogate(char16_t ch) {
            return (ch & 0xFC00) == 0xD800;
        }

        inline constexpr bool islowsurrogate(char16_t ch) {
            return (ch & 0xFC00) == 0xDC00;
        }

        inline constexpr bool surrogateishigh(char16_t ch) {
            assert(issurrogate(ch));
            return !(ch & 0x0400);
        }

        inline constexpr bool surrogateislow(char16_t ch) {
            assert(issurrogate(ch));
            return ch & 0x0400;
        }

        inline constexpr char32_t decodesurrogatepair(char16_t ch[2]) {
            assert(ishighsurrogate(ch[0]));
            assert(islowsurrogate(ch[1]));
            return ((((ch[0] & 0x000003FF) << 10)
                     | (ch[1] & 0x000003FF))
                    + 0x00010000);
        }

    } // namespace utf16


    namespace utf8 {

        // Canonical UTF-8 range validator.  Straight-line loop; constexpr
        // and consteval-friendly.  Catches every category of invalid UTF-8:
        //
        //   - stray continuation byte (0x80..0xBF as a leader)
        //   - overlong 2-byte (0xC0, 0xC1)
        //   - leading byte that would encode cp > 0x10FFFF (0xF5..0xFF)
        //   - truncated multibyte sequence (input ends mid-character)
        //   - non-continuation where a continuation is expected
        //   - overlong sequence (cp encoded with more bytes than needed)
        //   - cp > U+10FFFF
        //   - UTF-16 surrogate code points (U+D800..U+DFFF)
        //
        // Returns whether the byte range is valid UTF-8 as a whole.
        inline constexpr bool isvalid(const char* first, const char* last) {
            while (first != last) {
                u8 b = (u8)*first++;
                int extra;
                u32 cp;
                if (b < 0x80) {
                    continue;
                } else if (b < 0xC2) {
                    // 0x80..0xBF: stray continuation byte
                    // 0xC0, 0xC1:  overlong 2-byte leaders
                    return false;
                } else if (b < 0xE0) {
                    extra = 1; cp = b & 0x1F;
                } else if (b < 0xF0) {
                    extra = 2; cp = b & 0x0F;
                } else if (b < 0xF5) {
                    extra = 3; cp = b & 0x07;
                } else {
                    // 0xF5..0xFF: would encode cp > 0x10FFFF
                    return false;
                }
                for (int i = 0; i < extra; ++i) {
                    if (first == last) return false;
                    u8 c = (u8)*first++;
                    if ((c & 0xC0) != 0x80) return false;
                    cp = (cp << 6) | (c & 0x3F);
                }
                u32 min_cp = (extra == 1) ? 0x80u
                           : (extra == 2) ? 0x800u
                                          : 0x10000u;
                if (cp < min_cp) return false;
                if (cp > 0x10FFFFu) return false;
                if (cp >= 0xD800u && cp <= 0xDFFFu) return false;
            }
            return true;
        }

        // View-shaped overload for callers that hold a ContiguousView /
        // ContiguousDeque / String / StringView.  Forwards to the
        // pointer-pair overload via the view's begin()/end().
        inline constexpr bool isvalid(const auto& view) {
            return isvalid(view.begin(), view.end());
        }


        // UTF-8 iterator: walks a const char* range and yields char32_t
        // scalars.  Bidirectional.  Assumes the underlying bytes are
        // already valid UTF-8 (which is guaranteed by the invariant on
        // String / StringView).

        struct iterator {

            const char* base;

            using difference_type = difference_type;
            using value_type = char32_t;
            using reference = char32_t;
            using pointer = void;
            using iterator_category = std::bidirectional_iterator_tag;

            iterator() = default;

            explicit iterator(const char* ptr)
            : base(ptr) {
            }

            explicit iterator(std::nullptr_t)
            : base(nullptr) {
            }

            iterator operator++(int) {
                iterator a(*this);
                operator++();
                return a;
            }

            iterator operator--(int) {
                iterator a(*this);
                operator--();
                return a;
            }

            iterator& operator++() {
                // Width-of-leading-byte lookup, packed in a u64:
                //   0xxxxxxx (b >> 4 in 0..7) -> 1
                //   110xxxxx (b >> 4 == 0xC..0xD) -> 2
                //   1110xxxx (b >> 4 == 0xE) -> 3
                //   11110xxx (b >> 4 == 0xF) -> 4
                // Nibble n of the constant gives the width for b >> 4 == n.
                base += (0x4322000011111111 >> (((u8)*base & 0xF0) >> 2))
                      & 0x7;
                return *this;
            }

            iterator& operator--() {
                while (((u8)*--base & 0xC0) == 0x80)
                    ;
                return *this;
            }

            bool operator!() const {
                return !base;
            }

            explicit operator bool() const {
                return static_cast<bool>(base);
            }

            // Decode the character at `base` to its char32_t scalar.
            // Precondition: base points at the start of a valid UTF-8
            // character.
            char32_t operator*() const {
                u8 c = (u8)*base;
                if (!(c & 0x80))
                    return c;
                char32_t u;
                const char* p = base + 1;
                if (!(c & 0x20)) {
                    u = c & 0x1F;
                } else {
                    if (!(c & 0x10)) {
                        u = c & 0x0F;
                    } else {
                        u = c & 0x07;
                        u = (u << 6) | ((u8)*p++ & 0x3F);
                    }
                    u = (u << 6) | ((u8)*p++ & 0x3F);
                }
                u = (u << 6) | ((u8)*p & 0x3F);
                return u;
            }

            auto operator<=>(const iterator& other) const = default;
            bool operator==(const iterator& other) const = default;

        }; // utf8::iterator

    } // namespace utf8


    using std::strlen;

    template<typename T>
    inline size_type strlen(const T* start) {
        const T* end = start;
        for (; *end; ++end)
            ;
        return end - start;
    }


} // namespace wry

#endif /* unicode_hpp */
