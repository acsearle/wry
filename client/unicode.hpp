//
//  unicode.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef unicode_hpp
#define unicode_hpp

#include <iterator>

#include "common.hpp"

namespace wry {
    
    inline const u8* utf8advance(const u8* p) {
        if (((p[0] & 0b10000000) == 0b00000000))
            return p + 1;
        if (((p[0] & 0b11100000) == 0b11000000) &&
            ((p[1] & 0b11000000) == 0b10000000))
            return p + 2;
        if (((p[0] & 0b11110000) == 0b11100000) &&
            ((p[1] & 0b11000000) == 0b10000000) &&
            ((p[2] & 0b11000000) == 0b10000000))
            return p + 3;
        if (((p[0] & 0b11111000) == 0b11110000) &&
            ((p[1] & 0b11000000) == 0b10000000) &&
            ((p[2] & 0b11000000) == 0b10000000) &&
            ((p[3] & 0b11000000) == 0b10000000))
            return p + 4;
        abort();
    }
    
    inline bool utf8validatez(u8* p) {
        for (;;) {
            if (p[0] == 0) {
                return true;
            } else if (((p[0] & 0b10000000) == 0b00000000)) {
                ++p;
            } else if (((p[0] & 0b11100000) == 0b11000000) &&
                       ((p[1] & 0b11000000) == 0b10000000)) {
                p += 2;
            } else if (((p[0] & 0b11110000) == 0b11100000) &&
                       ((p[1] & 0b11000000) == 0b10000000) &&
                       ((p[2] & 0b11000000) == 0b10000000)) {
                p += 3;
            } else if (((p[0] & 0b11111000) == 0b11110000) &&
                       ((p[1] & 0b11000000) == 0b10000000) &&
                       ((p[2] & 0b11000000) == 0b10000000) &&
                       ((p[3] & 0b11000000) == 0b10000000)) {
                p += 4;
            } else {
                return false;
            }
        }
    }
    
    inline u8* utf8_encode(u32 a, u8 b[4]) {
        if (a < 0x8F) {
            b[0] = a;
            return b + 1;
        } else if (a < 0x800) {
            b[0] = 0b11000000 | (a >> 6);
            b[1] = 0b10000000 | (a & 0b00111111);
            return b + 2;
        } else if (a < 0x10000) {
            b[0] = 0b11100000 | (a >> 12);
            b[1] = 0b10000000 | ((a >> 6) & 0b00111111);
            b[2] = 0b10000000 | (a & 0b00111111);
            return b + 3;
        } else if (a < 0x110000) {
            b[0] = 0b11110000 | (a >> 18);
            b[1] = 0b10000000 | ((a >> 12) & 0b00111111);
            b[2] = 0b10000000 | ((a >> 6) & 0b00111111);
            b[3] = 0b10000000 | (a & 0b00111111);
            return b + 4;
        } else {
            abort();
        }
    }
    
    
    inline unsigned char* utf8advance_unsafe(unsigned char* p) {
        
        // Advance to the next code point in a valid UFT-8 sequence
        
        // The top 4 bits of the first byte distinguish between 1, 2, 3 and 4 byte
        // encodings
        //
        //     0b0******* -> 1
        //     0b10****** -> invalid (marks a continuation byte)
        //     0b110***** -> 2
        //     0b1110**** -> 3
        //     0b11110*** -> 4
        //
        // In the form of a table indexed by the top 4 bits
        //
        //     [ 1, 1, 1, 1, 1, 1, 1, 1, *, *, *, *, 2, 2, 3, 4 ].
        //
        // We can pack the table into the half-bytes of a 64-bit integer
        
        static constexpr std::uint64_t delta = 0x4322000011111111;
        
        // and then index the table by shifting and masking
        
        return p + ((delta >> ((*p >> 2) & 0b111100)) & 0b1111);
        
        // Bytes of the form 0b10****** cannot begin a valid UTF-8 code point.  The
        // table value we choose for these affords us some limited error handling
        // options.  We may choose 1, permissive, to advance one byte and attempt
        // to continue on the sequence.  Or we may choose 0, strict, refusing to
        // advance the pointer and likely hanging the process.
        
        // Other forms of invalid UTF-8 are not detectable in this scheme.
        
    }
    
    struct utf8_iterator {
        
        u8 const* _ptr;
        
        bool _is_continuation_byte() const {
            return (*_ptr & 0xC0) == 0x80;
        }
        
        using difference_type = std::ptrdiff_t;
        using value_type = u32;
        using reference = u32;
        using pointer = void;
        using iterator_category = std::bidirectional_iterator_tag;
        
        utf8_iterator() = default;
        
        explicit utf8_iterator(void const* ptr)
        : _ptr((u8 const*) ptr) {
        }
        
        explicit utf8_iterator(std::nullptr_t) : _ptr(nullptr) {}
        
        explicit operator bool() const { return _ptr; }
        bool operator!() const { return !_ptr; }
        
        utf8_iterator& operator++() {
            do { ++_ptr; } while (_is_continuation_byte());
            return *this;
        }
        
        utf8_iterator operator++(int) {
            utf8_iterator a(*this); operator++(); return a;
        }
        
        utf8_iterator& operator--() {
            do { --_ptr; } while (_is_continuation_byte());
            return *this;
        }
        
        utf8_iterator operator--(int) {
            utf8_iterator a(*this); operator--(); return a;
        }
        
        u32 operator*() const {
            if (!(_ptr[0] & 0x80))
                return *_ptr;
            return _deref_multibyte(); // slow path
        }
        
        u32 _deref_multibyte() const {
            if ((_ptr[0] & 0xE0) == 0xC0)
                return (((_ptr[0] & 0x1F) <<  6) |
                        ((_ptr[1] & 0x3F)      ));
            if ((_ptr[0] & 0xF0) == 0xE0)
                return (((_ptr[0] & 0x0F) << 12) |
                        ((_ptr[1] & 0x3F) <<  6) |
                        ((_ptr[2] & 0x3F)      ));
            if ((_ptr[0] & 0xF8) == 0xF0)
                return (((_ptr[0] & 0x07) << 18) |
                        ((_ptr[1] & 0x3F) << 12) |
                        ((_ptr[2] & 0x3F) <<  6) |
                        ((_ptr[3] & 0x3F)      ));
            abort();
        }
        
        friend bool operator==(utf8_iterator a, utf8_iterator b) {
            return a._ptr == b._ptr;
        }
        
        friend bool operator!=(utf8_iterator a, utf8_iterator b) {
            return a._ptr != b._ptr;
        }
        
        friend bool operator==(utf8_iterator a, std::nullptr_t) {
            return a._ptr == nullptr;
        }
        
        friend bool operator==(std::nullptr_t, utf8_iterator b) {
            return nullptr == b._ptr;
        }
        
        friend bool operator!=(utf8_iterator a, std::nullptr_t) {
            return a._ptr != nullptr;
        }
        
        friend bool operator!=(std::nullptr_t, utf8_iterator b) {
            return nullptr != b._ptr;
        }
        
    }; // utf8_iterator
    
} // namespace manic

#endif /* unicode_hpp */
