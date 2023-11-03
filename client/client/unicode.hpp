//
//  unicode.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef unicode_hpp
#define unicode_hpp

#include <compare>

#include "array_view.hpp"
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
    // Wide literal encoding
    //    L'c-char'          ->       wchar_t
    //    L'c-char'          -> const wchar_t[N]
    //    L'c-char sequence' ->       wchar_t
    //
    // The array length N is always in code units, including a zero terminator
    
    // We use the char8_t to indicate not only that invalid UTF-8 bytes do not
    // appear, but also that any contiguous sequence of char8_t constitutes
    // valid UTF-8 sequence, and that any char8_t pointers point to USV
    // boundaries in such a sequence.
    
    
    namespace utf32 {
        
        inline constexpr bool isvalid(u32 ch) {
            return (ch <= 0x0010FFFF) && ((ch & 0xFFFFD80) != 0x0000D80);
        }
        
    } // namespace utf32
    
    namespace utf16 {
        
        inline constexpr bool isvalid(u16 ch) {
            return true;
        }
        
        inline constexpr bool issurrogate(char16_t ch) {
            return (ch & 0xD800) == (0xD800);
        }
        
        inline constexpr bool ishighsurrogate(char16_t ch) {
            return (ch & 0xDC00) == (0xD800);
        }
        
        inline constexpr bool islowsurrogate(char16_t ch) {
            return (ch & 0xDC00) == (0xDC00);
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
        
        inline constexpr difference_type width(char16_t ch) {
            assert(!islowsurrogate(ch));
            return ishighsurrogate(ch) ? 2 : 1;
        }
        
    } // namespace utf16
    
    namespace utf8 {
        
        namespace hoehrmann {
            
            // Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
            // See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
            
            #define UTF8_ACCEPT 0
            #define UTF8_REJECT 12
            
            static const uint8_t utf8d[] = {
                // The first part of the table maps bytes to character classes that
                // to reduce the size of the transition table and create bitmasks.
                0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
                7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
                10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,
                
                // The second part is a transition table that maps a combination
                // of a state of the automaton and a character class to a state.
                0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
                12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
                12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
                12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
                12,36,12,12,12,12,12,12,12,12,12,12,
            };
            
            uint32_t inline
            decode(uint32_t* state, uint32_t* codep, uint32_t byte) {
                uint32_t type = utf8d[byte];
                
                *codep = (*state != UTF8_ACCEPT) ?
                (byte & 0x3fu) | (*codep << 6) :
                (0xff >> type) & (byte);
                
                *state = utf8d[256 + *state + type];
                return *state;
            }
            
        } // namespace hoehrmann
        
        inline constexpr char _isutf8_table[256] = {
            
            1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
            1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
            1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
            1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
            
            1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
            1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
            1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
            1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
            
            1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
            1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
            1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
            1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
            
            0,0,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
            1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
            1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
            1,1,1,1, 1,0,0,0, 0,0,0,0, 0,0,0,0,
            
        };
        
        inline constexpr bool isvalid(u8 ch) {
            return _isutf8_table[ch];
        }
        
        inline constexpr bool isleading(char8_t ch) {
            assert(isvalid(ch));
            return (ch & 0xC0) != 0x80;
        }
        
        inline constexpr bool iscontinuation(char8_t ch) {
            assert(isvalid(ch));
            return (ch & 0xC0) == 0x80;
        }
        
        inline constexpr int width(char8_t ch) {
            assert(isvalid(ch));
            assert(!iscontinuation(ch));
            return (0x4322000011111111 >> ((ch & 0xF0) >> 2)) & 0x0000000000000007;
        }
        
        inline constexpr u32 payload(char8_t ch) {
            assert(isvalid(ch));
            if (!(0x80 & ch)) return ch       ; // 0xxxxxxx
            if (!(0x40 & ch)) return ch & 0x3F; // 10xxxxxx
            if (!(0x20 & ch)) return ch & 0x1F; // 110xxxxx
            if (!(0x10 & ch)) return ch & 0x0F; // 1110xxxx
            return ch & 0x07; // 11110xxx
        }
        
        
        inline bool isvalid(auto v) {
            
            // replace me with simdutf?
            
            u32 u = 0;
            auto b = 0;
            char8_t c = 0;
            
            goto expect_boundary;
            
        expect_antipenultimate_continuation_byte:
            
            if (v.empty()) {
                // ends in middle of multibyte character
                goto error;
            }
            b = v.front();
            v.pop_front();
            
            if ((b & 0xC0) != 0x80) {
                // !10xxxxxxx, unexpected leading byte
                goto error;
            }
            
            u = (u << 6) | (b & 0x3F);
            
            if (!(u & 0xFFFFFFF0)) {
                // 00000xxxxxxxxxxxxxxxx, overlong encoding
                goto error;
            }
            
            if (u > 0x0000010F) {
                // > U+10FFFF, out of bounds
                goto error;
            }
            
        expect_penultimate_continuation_byte:
            
            if (v.empty()) {
                // ends in middle of multibyte character
                goto error;
            }
            b = v.front();
            v.pop_front();
            
            if ((b & 0xC0) != 0x80) {
                // !10xxxxxxx, unexpected leading byte
                goto error;
            }
            
            u = (u << 6) | (b & 0x3F);
            
            if (!(u & 0xFFFFFFE0)) {
                // 0000000000xxxxxxxxxxx, overlong encoding
                goto error;
            }
            
            if ((u & 0xFFFFFF60) == (0x00000360)) {
                // 0000001101xxxxxxxxxxx, UTF-16 surrogate
                goto error;
            }
            
        expect_ultimate_continuation_byte:
            
            b = v.front();
            v.pop_front();
            
            if ((b & 0xC0) != 0x80) {
                // !10xxxxxxx, unexpected leading byte
                goto error;
            }
            
            u = (u << 6) | (b & 0x3F);
            
            // safety: already checked in expect_leading_byte
            assert(u & 0xFFFFFF80); // 00000000000000xxxxxxx, overlong encoding
            
        validate_decoded:
            
            // safety: already checked in expect_antipenultimate_byte
            assert(u <= 0x10FFFF); // U+10FFFF
                                   // safety: already checked in expect_penultimate_byte
            assert((u & 0xFFFFD800) != 0xD800); // UTF16 surrogate
            
            // -- hot path ----------------------------------------------------
            
        expect_boundary:
            
            if (v.empty())
                // ends between multibyte characters
                return true;
            
        expect_leading_byte:
            
            b = v.front();
            v.pop_front();
            
            if (!(c & 0x80)) {
                // 0xxxxxxx, ASCII, don't need to extract or validate the character
                goto expect_boundary;
            }
            
            // ----------------------------------------------------------------
            
            if (!(c & 0x40)) {
                // 10xxxxxx, unexpected continuation byte
                return false;
            }
            if (!(c & 0x20)) {
                // 110xxxxx, two-byte encoded character
                if (!(c & 0x01C)) {
                    // 110000xx, overlong
                    return false;
                }
                u = c & 0x1F;
                goto expect_ultimate_continuation_byte;
            }
            if (!(c & 0x10)) {
                // 1110xxxx, three-byte encoded character
                u = c & 0x0F;
                goto expect_penultimate_continuation_byte;
            }
            if (!(c & 0x080)) {
                // 11110xxx, four-byte encoded character
                u = c & 0x03;
                goto expect_antipenultimate_continuation_byte;
            } else {
                // 11111xxx, invalid
                goto error;
            }
            
        error:
            
            // safety: we have popped at least once on all paths
            v.unsafe_unpop_front();
            return false;
            
        }
        
        
        // precondition: p points to the beginning of a character
        inline char32_t decode_one(const char8_t*& p) {
            
            // -- hot path ----------------------------------------------------
            
            char8_t c = *p++;
            if (!(c & 0x80))
                return c;
            
            // ---------------------------------------------------------------
            
            char32_t u = c;
            if (!(c & 0x20)) {
                u = c & 0x1F;
            } else {
                if (!(c & 0x10)) {
                    u = c & 0x0F;
                } else {
                    u = c & 0x07;
                    u <<= 6;
                    u |= *p++ & 0x3F;
                }
                u <<= 6;
                u |= *p++ & 0x3F;
            }
            u <<= 6;
            u |= *p++ & 0x3F;
            return u;
            
        }
        
        
        struct iterator {
            
            const char8_t* base;
            
            using difference_type = difference_type;
            using value_type = char32_t;
            using reference = char32_t;
            using pointer = void;
            using iterator_category = std::bidirectional_iterator_tag;
            
            iterator() = default;

            explicit iterator(const char8_t* ptr)
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
                // safety: relies on precondition that we can increment and that
                // the char8_t range is valid
                base += (0x4322000011111111 >> ((*base & 0xF0) >> 2)) & 0x0000000000000007;
                return *this;
            }
                        
            iterator& operator--() {
                while ((*--base & 0xC0) == 0x80)
                    ;
                return *this;
            }
            
            bool operator!() const {
                return !base;
            }
            
            explicit operator bool() const {
                return static_cast<bool>(base);
            }
            
            char32_t operator*() const {
                const char8_t* p = base;
                return decode_one(p);
            }
            
            // *--it
            char32_t next();
            
            // *it++
            char32_t prev() {
                // parse UTF-8 backwards
                char8_t b = *--base;
                if ((b & 0x80) == 0x00) { // ascii
                    return static_cast<char32_t>(b);
                }
                assert((b & 0xC0) == 0x80); // <-- else seq ended inside multibyte
                char32_t c = b & 0x0000003F;
                if ((b & 0xE) == 0xC0) { // 2leading
                    c |= (b & 0x0000001F) << 6;
                    // 0 0000 0000 0000 0111 1111
                    // 0 0000 0000 0222 2211 1111
                    // 0 0000 3333 2222 2211 1111
                    // 4 4433 3333 2222 2211 1111
                    assert(c & 0x00000780); // <-- else overlong
                    return c;
                }
                assert((b & 0xC0) == 0x80);
                c |= (b & 0x0000003F) << 6;
                if ((b & 0xF0) == 0xE0) { // 3leading
                    c |= (b & 0x0000000F) << 12;
                    assert(c & 0x0000F800); // <-- else overlong
                    assert((c & 0x0000D800) != 0x0000D800); // <-- else surrogate
                    return c;
                }
                assert((b & 0xC0) == 0x80);
                c |= (b & 0x0000003F) << 12;
                assert((b & 0xF8) == 0xF0); // 4leading
                c |= (b & 0x00000007) << 18;
                assert(c & 0x001F0000); // <-- else overlong
                assert(c <= 0x0010FFFF); // <-- else too large
                return c;
            }
        
            auto operator<=>(const iterator& other) const = default;
            bool operator==(const iterator& other) const = default;
            
        }; // utf8_iterator
        
    } // namespace utf8
    

        
    
    
    // transcoders

    inline bool utf32_to_utf8(const char32_t* first, const char32_t* last,
                              char8_t*& d_first, char8_t* d_last) {
        for (;;) {
            if (first == last)
                return true;
            char32_t ch = *first;
            assert(utf32::isvalid(ch));
            ptrdiff_t n = d_last - d_first;
            if (!(ch & 0xFFFFFF80)) {
                // a is 7 bits, encode to 0xxxxxxx
                if (n < 1) return false; // short output
                *d_first++ = ch;
            } else if (!(ch & 0xFFFFF800)) {
                // a is 11 bits, encode to 110xxxxx 10xxxxxx
                if (n < 2) return false; // short output
                *d_first++ = 0xC0 | (ch >> 6);
                *d_first++ = 0x80 | (ch & 0x3F);
            } else if (!(ch & 0xFFFF0000)) {
                // a is 16 bits, encode to 1110xxxx 10xxxxxx 10xxxxxx
                if (n < 3) return false; // short output
                *d_first++ = 0xE0 | (ch >> 12);
                *d_first++ = 0x80 | ((ch >> 6) & 0x3F);
                *d_first++ = 0x80 | (ch & 0x3F);
            } else {
                // a is 21 bits, encode to 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
                if (n < 4) return false; // short output
                *d_first++ = 0xF0 | (ch >> 18);
                *d_first++ = 0x80 | ((ch >> 12) & 0x3F);
                *d_first++ = 0x80 | ((ch >> 6) & 0x3F);
                *d_first++ = 0x80 | (ch & 0x3F);
            }
            ++first;
        }
    }
    
    inline bool utf32_to_utf16(const char32_t* first, const char32_t* last, char16_t*& d_first, char16_t* d_last) {
        for (;;) {
            if (first == last)
                return true;
            char32_t ch = *first;
            assert(utf32::isvalid(ch));
            if (!(ch & 0xFFFF0000)) {
                if (d_first == d_last) return false; // short output
                *d_first++ = ch;
            } else {
                if ((last - first) < 2) return false; // short output
                ch -= 0x00010000;
                assert(!(ch & 0xFFF0000));
                *d_first++ = 0x0000D800 | (ch >> 10);
                *d_first++ = 0x0000DC00 | (ch & 0x000003FF);
                return true;
            }
        }
    }
    
    inline bool utf16_to_utf8(const char16_t*& first, const char16_t* last, char8_t*& d_first, char8_t* d_last) {
        for (;;) {
            if (first == last)
                return true;
            char16_t a = *first;
            char32_t b = {};
            if (!utf16::ishighsurrogate(a)) {
                b = a;
                if (!utf32_to_utf8(&b, &b + 1, d_first, d_last))
                    return false;
                ++first;
            } else {
                b = (a & 0x000003FF) << 10;
                const char16_t* c = first;
                ++c;
                if (c == last)
                    return false;
                char16_t d = *c;
                b |= (d & 0x000003FF);
                b += 0x00010000;
                if (!utf32_to_utf8(&b, &b + 1, d_first, d_last))
                    return false;
                first = ++c;
            }
        }
    }
    
    inline bool utf8_to_utf32(const char8_t*& first, const char8_t* last, char32_t& ch) {

        const char8_t* p = first;
        
        if (p == last)
            return false;
        
        char b = *first;

        if ((b & 0x80) == 0x00) {
            // 0xxxxxxx
            return static_cast<uint32_t>(b);
        }
        
        if ((b & 0xE0) == 0xC0) {
            // 110xxxxx 10xxxxxx
            ch = b & 0x0000001F;
        } else if ((b & 0xF0) == 0xE0) {
            // 1110xxxx 10xxxxxx 10xxxxxx
            ch = b & 0x0000000F;
        } else if ((b & 0xF8) == 0xF0) {
            // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            ch = b & 0x00000007;
        } else {
            // not a valid header byte
            return false;
        }
        
        for (;;) {
            ++p;
            if (p == last)
                return false;
            char d = *p;
            if ((d & 0xC0) != 0x80) {
                // expected continuation byte
                return false;
            }
            ch = (ch << 6) | (d & 0x0000003F);
            b = (b << 1);
            if (!(b & 0x40))
                break;
        }

        assert(utf32::isvalid(ch));
        
        first = p;
        return true;
    }
    

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

#if 0


// State machine decoder
//
// https://bjoern.hoehrmann.de/utf-8/decoder/dfa/

// 07 7
// 11 5:6
// 16 4:6:6
// 21 3:6:6:6

// States (9)
//
// ERROR
//      xxxxxxxx -> ERROR
//
// INITIAL
//      0xxxxxxx -> INITIAL
//      10xxxxxx -> ERROR
//      1100000x -> ERROR
//      110xxxxx -> 2OF2
//      11100000 -> 2OF3OVERLONG
//      11101101 -> 2OF3SURROGATE
//      1110xxxx -> 2OF3
//      11110000 -> 2OF4OVERLONG
//      11110100 -> 2OF4OVERFLOW
//      111100xx -> 2OF4
//      111101xx -> ERROR
//      11111xxx -> ERROR

// 2OF2
//      10xxxxxx -> INITIAL
//      xxxxxxxx -> ERROR

// 2OF3OVERLONG
//      101xxxxx -> 2OF2     first bit of continuation must be set
//      xxxxxxxx -> ERROR

// 2OF3SURROGATE
//      100xxxxxx -> 2OF2
//      101xxxxxx -> ERROR ( 1101 1xxx xxxx xxxx surrogates )
//      xxxxxxxxx -> ERROR

// 2OF3
//      10xxxxxx -> 2OF2
//      xxxxxxxx -> ERROR

// 2OF4OVERLONG
//      1000xxxx -> ERROR
//      10xxxxxx -> 2OF3

// 2OF4OVERFLOW
//      1000xxxx -> 2OF3
//      10xxxxxx -> ERROR (> UTF+10FFFF)
//      xxxxxxxx -> ERROR

// 2OF4
//      10xxxxxx -> 2OF3
//      xxxxxxxx -> ERROR


// groups of characters that lead to different transitions in at least
// one state:

// 0xxxxxxx    ASCII

// 1000xxxx    4 byte overlong, not overflow
// 100xxxxx    3 byte overlong, not surrogate
// 101xxxxx    3 byte surrogate

// 1100000x    2 byte overlong
// 110xxxxx    2 byte
// 11100000    3 byte possible overlong
// 11101101    3 byte possible surrogate
// 1110xxxx    3 byte
// 11110000    4 byte possible overlong
// 111100xx    4 byte
// 11110100    4 byte possible overflow
// 111111xx    Overflow
// 11111xxx    Overflow

// Allowed masks:

// x1111111

// 00xxxxx0 2overlong
// 00x11111 2leading

// 000xxxxx 3overlong
// 000x11x1 3surrogate
// 000x1111 3leading

// 0000xxxx 4overlong
// 0000x1xx
// 0000x111 4leading

// how far apart can we space these cases ?

// 00000000
// 0000
// 00001111





// allocate unique IDs to them that also work as AND masks
// The known zeros are precious, we basically use them to tag the
// group

// 11111111  ASCII

// 01001111  Continuation byte; 4overlong, 4overflow
// 01011111  Continuation byte; surrogate, 3overlong
// 01111111  Continutaion byte; surrogate,

// 00000000  ERROR
// 00111111  Leading of 2

// 00010000  Leading of 3, possible overlong
// 00011111  Leading of 3
// 00011101  Leading of 3, possible surrogate

// 00001000  Leading of 4, possible overlong
// 00001111  Leading of 4
// 00001100  Leading of 4, possible overflow



// xxxxxxxx  2 byte overlong, always an error
// 00x11111  Ordinary 2 byte

// 000xxxxx  3 byte possible overlong
// 000x1111   Ordinary 3 byte
// 000x11x1  3 byte possible surrogate

// 0000xxxx  4 byte possible overlong
// 0000xx11  4 byte ordinary
// 0000x1xx  4 byte possible overflow
// xxxxxxxx  Always errors

// take two, use as masks for next state
//
// 11111111
// 00111111
// 00011111
// 00001111



namespace {
    
    // Partition 256 into 12 character classes, the members of which
    // behave identically.
    
    // We use our freedom of mapping to choose an identifier that also
    // acts as the neccesary mask for the state
    
    enum : unsigned char {
        
        A = 0b11111111, // leading1
        
        B = 0b01001111, // continuation; overlong4, overflow4
        C = 0b01011111, // continuation; overlong4, surrogate3
        D = 0b01111111, // continuation; surrogate3
        
        E = 0b00000000, // invalid byte; overlong2, overflow4
        F = 0b00111111, // leading2
        
        G = 0b00010000, // leading3; overlong3
        H = 0b00011111, // leading3
        I = 0b00011101, // leading3; surrogate3
        
        J = 0b00001000, // leading4; overlong4
        K = 0b00001111, // leading4
        L = 0b00001101, // leading4; overflow4
        
    };
    
    inline constexpr unsigned char masks[256] = {
        
        A,A,A,A, A,A,A,A, A,A,A,A, A,A,A,A,
        A,A,A,A, A,A,A,A, A,A,A,A, A,A,A,A,
        A,A,A,A, A,A,A,A, A,A,A,A, A,A,A,A,
        A,A,A,A, A,A,A,A, A,A,A,A, A,A,A,A,
        
        A,A,A,A, A,A,A,A, A,A,A,A, A,A,A,A,
        A,A,A,A, A,A,A,A, A,A,A,A, A,A,A,A,
        A,A,A,A, A,A,A,A, A,A,A,A, A,A,A,A,
        A,A,A,A, A,A,A,A, A,A,A,A, A,A,A,A,
        
        B,B,B,B, B,B,B,B, B,B,B,B, B,B,B,B,
        C,C,C,C, C,C,C,C, C,C,C,C, C,C,C,C,
        D,D,D,D, D,D,D,D, D,D,D,D, D,D,D,D,
        D,D,D,D, D,D,D,D, D,D,D,D, D,D,D,D,
        
        E,E,F,F, F,F,F,F, F,F,F,F, F,F,F,F,
        F,F,F,F, F,F,F,F, F,F,F,F, F,F,F,F,
        G,H,H,H, H,H,H,H, H,H,H,H, H,I,H,H,
        J,K,K,K, L,E,E,E, E,E,E,E, E,E,E,E,
        
    };
    
    void foo(uint8_t b, uint32_t& z) {
        
        unsigned char m = masks[b];
        z = (z << 6) | (m & b);
        
        // A:A -> A
        // A:F -> F, Z (2OF2)
        // A:G -> G, 2OF3OVERLONG
        // A:H -> H, Y (2OF3)
        // A:I -> I, 2OF3SURROGATE
        // A:J -> J, 2OF4OVERLONG
        // A:K -> K, X (2OF4)
        // A:L -> L, 2OF4OVERFLOW
        
        // first byte spreads out everywhere
        // second byte of 2 and 3 decides solely on the basis of
        // the top 2/6
        
        // F:B -> A
        // F:C -> A
        // F:D -> A
        
        // H:B -> F
        // H:C -> F
        // H:D -> F
        
        // K:B -> H
        // K:C -> H
        // K:D -> H
        
        // G:B -> E
        // G:C -> E
        // G:D -> F
        
        // I:B -> F
        // I:C -> F
        // I:D -> E
        
        // J:B -> E
        // J:C -> H
        // J:D -> H
        
        // K:B -> H
        // K:C -> E
        // K:D -> E
        
        
    }
    
}




#endif
