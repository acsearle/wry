//
//  string.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef string_hpp
#define string_hpp

#include "array.hpp"
#include "hash.hpp"
#include "string_view.hpp"

namespace wry {
    
    // <cstring>
    
    // two-way compares two null-terminated byte strings lexicographically
    
    inline bool strlt(const char* s1, const char* s2) {
        for (; *s1 && (*s1 == *s2); ++s1, ++s2)
            ;
        return ((unsigned char) *s1) < ((unsigned char) *s2);
    }

    // <string>
    
    // A String presents a UTF-8 array<char8_t> as a sequence of UTF-32 scalars
    //
    // String, string_view, array<char8_t> and array_view<char8_t> all maintain
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
                
        array<char8_t> chars;
        
        using const_iterator = utf8::iterator;
        using iterator = const_iterator;
        using value_type = char32_t;
                
        String() = default;
        String(const String& other) = default;
        String(String&&) = default;
        ~String() = default;
        String& operator=(const String& other) = default;
        String& operator=(String&&) = default;

        explicit String(string_view other)
        : chars(other.chars) {
        }
        
        String& operator=(string_view other) {
            chars = other;
            return *this;
        }
        
        explicit String(const_iterator a, const_iterator b) {
            chars.reserve(b.base - a.base + 1);
            chars.assign(a.base, b.base);
        }
        
        explicit String(array<char8_t>&& bytes) 
        : chars(std::move(bytes)) {
        }
        
        operator array_view<const char8_t>() const {
            return array_view<const char8_t>(chars.begin(), chars.end());
        }

        operator string_view() const {
            return string_view(begin(), end());
        }
        
        const_iterator begin() const {
            return iterator{ chars.begin() };
        }
        
        const_iterator end() const {             
            return iterator{ chars.end() }; }
        
        const char8_t* data() const {
            return chars.data();
        }
        
        array_view<const byte> as_bytes() const {
            return array_view<const byte>(reinterpret_cast<const byte*>(chars.begin()),
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

        
        void append(string_view& v) {
            chars.append(v.chars.begin(), v.chars.end());
        }
        
        void append(array_view<const char8_t> v) {
            chars.append(v);
        }
        
        
         bool operator==(const String& other) const {
             return std::equal(begin(), end(), other.begin(), other.end());
         }

        bool operator==(const string_view& other) const {
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
        
    struct immutable_string {
        
        // When a String is used as a hash table key, it is important to
        // keep the inline size small.  It will only be accessed for strcmp
        // to check hash collisions, and never mutated.  We can dispense
        // with _data and _capacity, place _end at the beginning of the
        // allocation, place the String after it, and infer _begin from
        // the allocation.
        
        // This is the simplest of several ways we can lay out the data, allowing
        // increasing mutability.  The downside is that accessing the iterators
        // requires pointer chasing, making use cases where we access the metadata
        // but not the data slower.
        
        // We don't store the hash since the hash table will usually do this
        
        //:todo: But on the other hand, the hash will be cheap to store here?
        
        // We could reference count the implementation
        // We could intern the strings of this sort
        
        struct implementation {
            
            char8_t* _end;
            char8_t _begin[];
            
            static implementation* make(string_view v) {
                size_type n = v.chars.size();
                implementation* p = (implementation*) operator new(sizeof(implementation) + n + 1);
                p->_end = p->_begin + n;
                memcpy(p->_begin, v.chars.begin(), n);
                *p->_end = 0;
                return p;
            }
            
            static implementation* make(implementation* q) {
                auto n = q->_end - q->_begin;
                implementation* p = (implementation*) operator new(sizeof(implementation) + n + 1);
                p->_end = p->_begin + n;
                memcpy(p->_begin, q->_begin, n + 1);
                return p;
            }
            
        };
        
        implementation* _body;
        
        immutable_string() : _body(nullptr) {}
        
        immutable_string(const immutable_string&) = delete;
        
        immutable_string(immutable_string&& s)
        : _body(std::exchange(s._body, nullptr)) {}
        
        ~immutable_string() {
            free(_body);
        }
        
        immutable_string& operator=(const immutable_string& other) = delete;
            
        void swap(immutable_string& other) {
            using std::swap;
            swap(_body, other._body);
        }
            
        immutable_string& operator=(immutable_string&& other) {
            immutable_string(std::move(other)).swap(*this);
            return *this;
        }
        
        immutable_string clone() const {
            immutable_string r;
            if (_body)
                r._body = implementation::make(_body);
            return r;
        }
        
        explicit immutable_string(string_view v)
        : _body(implementation::make(v)) {
        }
        
        using const_iterator = utf8::iterator;
        using iterator = utf8::iterator;
        
        const_iterator begin() const {
            return iterator{_body ? _body->_begin : nullptr};
        }
        
        const_iterator end() const {
            return iterator{_body ? _body->_end : nullptr};
        }
        
        const char* c_str() const {
            return _body ? reinterpret_cast<char const*>(_body->_begin) : nullptr;
        }
        
        const char8_t* data() const {
            return _body ? _body->_begin : nullptr;
        }
        
        array_view<const byte> as_bytes() const {
            return array_view<const byte>(_body ? reinterpret_cast<const byte*>(_body->_begin) : nullptr,
                                          _body ? reinterpret_cast<const byte*>(_body->_end + 1) : nullptr);
        }
        
        operator string_view() const {
            return string_view(begin(), end());
        }
        
        bool empty() const {
            return !(_body && (_body->_end - _body->_begin));
        }
                
    };
    
} // namespace manic




#endif /* string_hpp */
