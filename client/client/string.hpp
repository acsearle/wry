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
    
    // A string presents a UTF-8 array<char8_t> as a sequence of UTF-32 scalars
    //
    // Support for c_str is dropped
            
    struct string {
                
        array<char8_t> chars;
        
        using const_iterator = utf8::iterator;
        using iterator = const_iterator;
        using value_type = char32_t;
                
        string() = default;
        string(const string& other) = default;
        string(string&&) = default;
        ~string() = default;
        string& operator=(const string& other) = default;
        string& operator=(string&&) = default;

        explicit string(string_view other)
        : chars(other.chars) {
        }
        
        string& operator=(string_view other) {
            chars = other;
            return *this;
        }
        
        explicit string(const_iterator a, const_iterator b) {
            chars.reserve(b.base - a.base + 1);
            chars.assign(a.base, b.base);
        }
        
        explicit string(array<char8_t>&& bytes) : chars(std::move(bytes)) {
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
            return chars.begin();
        }
        
        array_view<const byte> as_bytes() const {
            return array_view<const byte>(reinterpret_cast<const byte*>(chars.begin()),
                                          reinterpret_cast<const byte*>(chars.end()));
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
                
        void push_back(char32_t c) {
            if (c < 0x80) {
                chars.push_back(c);
            } else if (c < 0x800) {
                chars.push_back(0xC0 | ((c >>  6)       ));
                chars.push_back(0x80 | ((c      ) & 0x3F));
            } else if (c < 0x10000) {
                chars.push_back(0xE0 | ((c >> 12)       ));
                chars.push_back(0x80 | ((c >>  6) & 0x3F));
                chars.push_back(0x80 | ((c      ) & 0x3F));
            } else {
                chars.push_back(0xF0 | ((c >> 18)       ));
                chars.push_back(0x80 | ((c >> 12) & 0x3F));
                chars.push_back(0x80 | ((c >>  6) & 0x3F));
                chars.push_back(0x80 | ((c      ) & 0x3F));
            }
        }

        void push_front(uint c) {
            if (c < 0x80) {
                chars.push_front(c);
            } else if (c < 0x800) {
                chars.push_front(0x80 | ((c      ) & 0x3F));
                chars.push_front(0xC0 | ((c >>  6)       ));
            } else if (c < 0x10000) {
                chars.push_front(0x80 | ((c      ) & 0x3F));
                chars.push_front(0x80 | ((c >>  6) & 0x3F));
                chars.push_front(0xE0 | ((c >> 12)       ));
            } else {
                chars.push_back(0x80 | ((c      ) & 0x3F));
                chars.push_back(0x80 | ((c >>  6) & 0x3F));
                chars.push_back(0x80 | ((c >> 12) & 0x3F));
                chars.push_back(0xF0 | ((c >> 18)       ));
            }
        }

        char32_t pop_back() {
            assert(!empty());
            iterator e = end();
            --e;
            uint c = *e;
            chars._end += (e.base - chars._end);
            return c;
        }
        
        char32_t pop_front() {
            assert(!empty());
            iterator b = begin();
            uint c = *b;
            ++b;
            chars._begin += (b.base - chars._begin);
            return c;
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
        
        
         bool operator==(const string& other) const {
             return std::equal(begin(), end(), other.begin(), other.end());
         }

        bool operator==(const string_view& other) const {
            return std::equal(begin(), end(), other.begin(), other.end());
        }

    };
    
    inline uint64_t hash(const string& x) {
        return hash_combine(x.chars.data(), x.chars.size());
    }
    
    inline std::ostream& operator<<(std::ostream& a, string const& b) {
        a.write((char const*) b.chars.begin(), b.chars.size());
        return a;
    }
        
    struct immutable_string {
        
        // When a string is used as a hash table key, it is important to
        // keep the inline size small.  It will only be accessed for strcmp
        // to check hash collisions, and never mutated.  We can dispense
        // with _data and _capacity, place _end at the beginning of the
        // allocation, place the string after it, and infer _begin from
        // the allocation.
        
        // This is the simplest of several ways we can lay out the data, allowing
        // increasing mutability.  The downside is that accessing the iterators
        // requires pointer chasing, making use cases where we access the metadata
        // but not the data slower.
        
        // We don't store the hash since the hash table will usually do this
        
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
    
    string string_from_file(string_view);
    
    
    
    
} // namespace manic




#endif /* string_hpp */
