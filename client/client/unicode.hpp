//
//  unicode.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef unicode_hpp
#define unicode_hpp

#include <iterator>

#include "rust.hpp"

namespace wry {
    
    // We use char to represent utf-8 code units so that string literals have
    // the correct type
    
    inline bool utf8validatez(const char*& z) {
        
        if (!z)
            // null string
            return true;
        
        char a = 0;
        char b = 0;
        
    first:
        
        // expect first code unit or terminating zero
        
        a = *z;
        
        if ((a & 0xC0) == 0x80) {
            // 1xxxxxxx: unexpected contiunuation code unit
            return false;
        }
                
        ++z;
        
        if (!a) {
            // 00000000: teminating zero
            return true;
        }
        
        if (a & 0x80) {
            // 00xxxxxx: one code unit encoded character
            goto first;
        }
        
        // 11xxxxxx: first code unit of multibyte encoded character
                
    second:
        
        b = *z;
        if ((b & 0xC0) != 0x80) {
            // !10xxxxxx: unexpected noncontinuation byte
            return false;
        }
        
        ++z;
        
        if (!(a & 0x20)) {
            // end of two byte character
            goto first;
        }
        
    third:
        
        b = *z;
        if ((b & 0xC0) != 0x80) {
            // !10xxxxxx: unexpected noncontinuation byte
            return false;
        }
        
        ++z;
        
        if (!(a & 0x10)) {
            // end of three byte character
            goto first;
        }
        
    fourth:
        
        b = *z;
        if ((b & 0xC0) != 0x80) {
            // !10xxxxxx: unexpected noncontinuation byte
            return false;
        }
        
        ++z;
        
        if (a & 0x08) {
            // 11111xxx: more than four bytes
            return false;
        }

        goto first;
        
    }
    
    /*
    inline bool utf8validate(const uchar*& first, const uchar* last) {
        auto clo = [](
    initial:
        if (first == last)
            return true;
    expect_first:
        switch (__builtin_clz(*((const char*) first))
            case 4:
                if (
            case 3:
            case 2:
            case 1:
                return false;
            case 0:
                goto initial;
        }
        
    }
     */
    
    inline char* utf8_encode(u32 a, char b[4]) {
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
        
        const char* _ptr;
        
        bool _is_continuation_byte() const {
            return (*_ptr & 0xC0) == 0x80;
        }
        
        using difference_type = ptrdiff_t;
        using value_type = u32;
        using reference = u32;
        using pointer = void;
        using iterator_category = std::bidirectional_iterator_tag;
        
        utf8_iterator() = default;
        
        explicit utf8_iterator(const void* ptr)
        : _ptr((const char*) ptr) {
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
        
        bool operator==(const utf8_iterator& other) const = default;
        
        bool operator==(std::nullptr_t) const {
            return _ptr == nullptr;
        }

        
    }; // utf8_iterator
    
} // namespace manic

#endif /* unicode_hpp */
