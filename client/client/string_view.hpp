//
//  string_view.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//


#ifndef string_view_hpp
#define string_view_hpp

#include <algorithm>  // lexicographical_compare
#include <cassert>    // assert
#include <cstring>    // strlen
#include <ostream>    // ostream

#include "array_view.hpp"
#include "common.hpp" // u8, u32
#include "unicode.hpp"
#include "hash.hpp"
#include "serialize.hpp"

namespace wry {
    
    struct string_view {
        
        // almost an array_view<u8>, but utf8 iterators and no size
        
        using const_iterator = utf8_iterator;
        using iterator = const_iterator;
        using value_type = u32;
        
        const_iterator a, b;
        
        string_view() : a(nullptr), b(nullptr) {}
        string_view(char const* z) : a(z), b(z + strlen(z)) {}
        string_view(char const* p, usize n) : a(p), b(p + n) {}
        string_view(char const* p, char const* q) : a(p), b(q) {}
        string_view(u8 const* p, u8 const* q) : a{p}, b{q} {}
        string_view(const_iterator p, const_iterator q) : a(p), b(q) {}
        string_view(string_view const&) = default;
        
        bool empty() const { return a == b; }
        
        const_iterator begin() const { return a; }
        const_iterator end() const { return b; }
        const_iterator cbegin() const { return a; }
        const_iterator cend() const { return b; }
        
        u32 front() const { assert(!empty()); return *a; }
        u32 back() const { assert(!empty()); utf8_iterator c(b); return *--c; }
        
        friend bool operator==(string_view a, string_view b);
        
        friend bool operator!=(string_view a, string_view b);
        
        friend bool operator<(string_view a, string_view b) {
            return std::lexicographical_compare(a.a, a.b, b.a, b.b);
        }
        
        friend bool operator>(string_view a, string_view b);
        friend bool operator<=(string_view a, string_view b);
        friend bool operator>=(string_view a, string_view b);
        
        
        
        // terse (dangerous?) operations useful for parsing
        u32 operator*() const { assert(!empty()); return *a; }
        string_view& operator++() { assert(!empty());++a; return *this; }
        string_view& operator--() { assert(!empty()); --b; return *this; }
        string_view operator++(int) { assert(!empty()); string_view old{*this}; ++a; return old; }
        string_view operator--(int) { assert(!empty()); string_view old{*this}; --b; return old; }
        explicit operator bool() const { return a != b; }
        
        array_view<const byte> as_bytes() const {
            return array_view<const byte>(reinterpret_cast<const byte*>(a._ptr),
                                          reinterpret_cast<const byte*>(b._ptr));
        }
        
    }; // struct string_view
    
    
    inline bool operator==(string_view a, string_view b) {
        return std::equal(a.a, a.b, b.a, b.b);
    }
    
    
    inline bool operator!=(string_view a, string_view b) { return !(a == b); }
    inline bool operator>(string_view a, string_view b) { return b < a; }
    inline bool operator<=(string_view a, string_view b) { return !(b < a); }
    inline bool operator>=(string_view a, string_view b) { return !(a < b); }
    
    
    inline std::ostream& operator<<(std::ostream& a, string_view b) {
        a.write((char const*) b.a._ptr, b.b._ptr - b.a._ptr);
        return a;
    }
    
    inline u64 hash(string_view v) {
        return hash_combine(v.as_bytes().begin(), v.as_bytes().size(), 0);
    }
    
    inline u64 hash(const char* c) {
        return hash(string_view(c));
    }
    
    template<typename Serializer>
    void serialize(string_view const& v, Serializer& s) {
        serialize(v.as_bytes(), s);
    }
    
} // namespace manic

#endif /* string_view_hpp */
