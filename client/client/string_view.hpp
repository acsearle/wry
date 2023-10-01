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

#include "algorithm.hpp"
#include "array_view.hpp"
#include "unicode.hpp"
#include "hash.hpp"

namespace wry {
    
    struct string_view {
        
        // almost an array_view<uchar>, but utf8 iterators and no size
        
        using const_iterator = utf8_iterator;
        using iterator = const_iterator;
        using value_type = uint;
        
        const_iterator a, b;
        
        string_view() : a(nullptr), b(nullptr) {}
        string_view(const char* z) : a(z), b(z + strlen(z)) {}
        string_view(const char* p, size_t n) : a(p), b(p + n) {}
        string_view(const char* p, const char* q) : a(p), b(q) {}
        string_view(const_iterator p, const_iterator q) : a(p), b(q) {}
        string_view(string_view const&) = default;
        
        bool operator=(const string_view& other) {
            a = other.a;
            b = other.b;
            return true;
        }
        
        bool empty() const { return a == b; }
        
        const_iterator begin() const { return a; }
        const_iterator end() const { return b; }
        const_iterator cbegin() const { return a; }
        const_iterator cend() const { return b; }
        
        uint front() const { assert(!empty()); return *a; }
        uint back() const { assert(!empty()); utf8_iterator c(b); return *--c; }
        
        bool operator==(const string_view& other) const {
            return std::equal(a, b, other.a, other.b);
        }
        
        auto operator<=>(const string_view& other) const {
            return lexicographical_compare_three_way(a, b, other.a, other.b);
        }
        
        array_view<const char> as_bytes() const {
            return array_view<const char>(a._ptr,
                                           b._ptr);
        }
        
        void pop_front() { assert(!empty()); ++a; }
        void pop_back() { assert(!empty()); --b; }
        void unsafe_pull_front() { --a; }
        void unsafe_pull_back() { ++b; }

        // terse and frankly dangerous syntax for parsing
        
        uint operator*() const { assert(!empty()); return *a; }
        explicit operator bool() const { return a != b; }
        bool operator++() { pop_front(); return true; }
        bool operator--() { unsafe_pull_front(); return true; }
        bool operator++(int) { unsafe_pull_back(); return true; }
        bool operator--(int) { pop_back(); return true; }
        
        // string concatenation is often modelled as + but it is better thought
        // of as noncommutative *
        
        // concatenate views, which must be consecutive
        string_view operator*(const string_view& other) const {
            assert(b == other.a);
            return string_view(a, other.b);
        }
        
        // division is the complementary operation to concatenation, removing
        // a suffix so that for c = a * b we have a = c / b
        
        // difference of a view and its suffix
        string_view operator/(const string_view& other) const {
            assert(b == other.b);
            assert(a._ptr <= other.a._ptr);
            return string_view(a, other.a);
        }
                
    }; // struct string_view
    
    inline std::ostream& operator<<(std::ostream& a, string_view b) {
        a.write((char const*) b.a._ptr, b.b._ptr - b.a._ptr);
        return a;
    }
    
    inline uint64_t hash(string_view v) {
        return hash_combine(v.as_bytes().begin(), v.as_bytes().size(), 0);
    }
    
    inline uint64_t hash(const char* c) {
        return hash(string_view(c));
    }
    
    
} // namespace wry

#endif /* string_view_hpp */
