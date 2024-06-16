//
//  string.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef string_hpp
#define string_hpp

#include <atomic>

#include "array.hpp"
#include "hash.hpp"
#include "string_view.hpp"

namespace wry {
    
    // <string>
    
    // A String presents a UTF-8 array<char8_t> as a sequence of UTF-32 scalars
    //
    // String, StringView, array<char8_t> and ArrayView<char8_t> all maintain
    // valid UTF-8 strings.
    //
    // std::string::c_str() is not worth the complication it induces; we make a
    // copy where we are forced to deal with a zero-terminated String API such
    // as libpng, rather than maintaining a one-past-the end zero character.
    //
    // We are forced to consume zero-terminated strings in the form of string
    // literals, whose const char8_t[N] arrays include the zero.
    //
    // Practically speaking, we can probably just blanket ban zeros from
    // appearing in the strings
    
    struct String;
    
    template<> struct rank<String> : std::integral_constant<std::size_t, 1> {};
    
    
    
    struct String {
                
        Array<char8_t> chars;
        
        using const_iterator = utf8::iterator;
        using iterator = const_iterator;
        using value_type = char32_t;
                
        String() = default;
        String(const String& other) = default;
        String(String&&) = default;
        ~String() = default;
        String& operator=(const String& other) = default;
        String& operator=(String&&) = default;

        explicit String(StringView other)
        : chars(other.chars) {
        }
        
        String& operator=(StringView other) {
            chars = other;
            return *this;
        }
        
        explicit String(const_iterator a, const_iterator b) {
            chars.reserve(b.base - a.base + 1);
            chars.assign(a.base, b.base);
        }
        
        explicit String(Array<char8_t>&& bytes) 
        : chars(std::move(bytes)) {
        }
        
        operator ArrayView<const char8_t>() const {
            return ArrayView<const char8_t>(chars.begin(), chars.end());
        }

        operator StringView() const {
            return StringView(begin(), end());
        }
        
        const_iterator begin() const {
            return iterator{ chars.begin() };
        }
        
        const_iterator end() const {             
            return iterator{ chars.end() }; }
        
        const char8_t* data() const {
            return chars.data();
        }
        
        ArrayView<const byte> as_bytes() const {
            return ArrayView<const byte>(reinterpret_cast<const byte*>(chars.begin()),
                                          reinterpret_cast<const byte*>(chars.end()));
        }

        char32_t front() {
            assert(!empty());
            return *begin();
        }
        
        char32_t back() {
            assert(!empty());
            auto a = end();
            return *--a;
        }
        
        void push_back(char8_t ch) {
            // we can only push back an isolated char8_t if it is a single byte
            // encoding
            assert(!(ch & 0x80));
            chars.push_back(ch);
        }
        
        void push_back(char16_t ch) {
            // we can only push back an isolated char16_t if it is not a
            // surrogate
            assert(!utf16::issurrogate(ch));
            push_back(static_cast<char32_t>(ch));
        }
                
        void push_back(char32_t ch) {
            // todo: fixme / use common code
            if (ch < 0x80) {
                chars.push_back(ch);
            } else if (ch < 0x800) {
                chars.push_back(0xC0 | ((ch >>  6)       ));
                chars.push_back(0x80 | ((ch      ) & 0x3F));
            } else if (ch < 0x10000) {
                chars.push_back(0xE0 | ((ch >> 12)       ));
                chars.push_back(0x80 | ((ch >>  6) & 0x3F));
                chars.push_back(0x80 | ((ch      ) & 0x3F));
            } else {
                assert(ch <= 0x10FFFF);
                chars.push_back(0xF0 | ((ch >> 18)       ));
                chars.push_back(0x80 | ((ch >> 12) & 0x3F));
                chars.push_back(0x80 | ((ch >>  6) & 0x3F));
                chars.push_back(0x80 | ((ch      ) & 0x3F));
            }
        }
        
        void push_front(char8_t ch) {
            assert(!(ch & 0x80));
            chars.push_front(ch);
        }

        void push_front(char16_t ch) {
            assert(!utf16::issurrogate(ch));
            push_front(static_cast<char32_t>(ch));
        }
        
        void push_front(char32_t ch) {
            // fixme: use common / better code
            if (ch < 0x80) {
                chars.push_front(ch);
            } else if (ch < 0x800) {
                chars.push_front(0x80 | ((ch      ) & 0x3F));
                chars.push_front(0xC0 | ((ch >>  6)       ));
            } else if (ch < 0x10000) {
                chars.push_front(0x80 | ((ch      ) & 0x3F));
                chars.push_front(0x80 | ((ch >>  6) & 0x3F));
                chars.push_front(0xE0 | ((ch >> 12)       ));
            } else {
                assert (ch <= 0x10FFFF);
                chars.push_front(0x80 | ((ch      ) & 0x3F));
                chars.push_front(0x80 | ((ch >>  6) & 0x3F));
                chars.push_front(0x80 | ((ch >> 12) & 0x3F));
                chars.push_front(0xF0 | ((ch >> 18)       ));
            }
        }

        void pop_back() {
            assert(!empty());
            --reinterpret_cast<iterator&>(chars._end);
        }
        
        char32_t back_and_pop_back() {
            assert(!empty());
            return *--reinterpret_cast<iterator&>(chars._end);
        }
        
        void pop_front() {
            assert(!empty());
            ++reinterpret_cast<iterator&>(chars._begin);
        }

        char32_t front_and_pop_front() {
            assert(!empty());
            return  *reinterpret_cast<iterator&>(chars._begin)++;
        }

        bool empty() const {
            return chars.empty();
        }
        
        void clear() {
            chars.clear();
        }
        
        template<size_type N>
        void append(const char8_t (&x)[N]) {
            chars.append(std::begin(x), std::end(x));
        }
        
        void append(const char8_t* p) {
            if (p)
                for (char ch; (ch = *p); ++p)
                    chars.push_back(ch);
        }

        
        void append(StringView& v) {
            chars.append(v.chars.begin(), v.chars.end());
        }
        
        void append(ArrayView<const char8_t> v) {
            chars.append(v);
        }
        
        
         bool operator==(const String& other) const {
             return std::equal(begin(), end(), other.begin(), other.end());
         }

        bool operator==(const StringView& other) const {
            return std::equal(begin(), end(), other.begin(), other.end());
        }
        
        auto operator<=>(const String& other) const {
            return wry::lexicographical_compare_three_way(begin(), end(), other.begin(), other.end());
        }

    };
    
    inline uint64_t hash(const String& x) {
        return hash_combine(x.chars.data(), x.chars.size());
    }
    
    inline std::ostream& operator<<(std::ostream& a, String const& b) {
        a.write((char const*) b.chars.begin(), b.chars.size());
        return a;
    }
    
} // namespace wry




#endif /* string_hpp */
