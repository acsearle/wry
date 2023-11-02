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
    
    // string_view presents an aray_view<const char8_t> as a UTF-32 sequence
    
    struct string_view;
    
    using StringView = string_view;
    
    template<> struct rank<string_view> : std::integral_constant<std::size_t, 1> {};
    
    struct string_view {
        
        using const_iterator = utf8::iterator;
        using iterator = const_iterator;
        using value_type = char32_t;
        
        array_view<const char8_t> chars;
        
        bool _invariant() const {
            return utf8::isvalid(chars);
        }
                
        string_view() : chars() {}
        
        string_view(const string_view&) = default;
        string_view(string_view&&) = default;
        
        ~string_view() = default;

        // views are reference-like, so assignment to const can't change the
        // data and won't change the pointers
        //
        // instead, use .reset(...) to swing the view
        
        string_view& operator=(const string_view&) = delete;
        string_view& operator=(string_view&&) = delete;

        template<size_type N>
        string_view(const char8_t (&literal)[N])
        : string_view(literal, N - 1) {
            assert(_invariant());
        }

        string_view(const char8_t* zstr)
        : chars(zstr, zstr + ::std::strlen(reinterpret_cast<const char*>(zstr))) {
            assert(_invariant());
        }
        
        string_view(const char8_t* p, size_type n)
        : chars(p, n) {
            assert(_invariant());
        }
        
        string_view(const char8_t* first, const char8_t* last) 
        : chars(first, last) {
            assert(_invariant());
        }
        
        string_view(const_iterator first, const_iterator last) 
        : chars(first.base, last.base) {
            assert(_invariant());
        }
        
    
        template<size_type N>
        string_view(char (&literal)[N])
        : string_view(reinterpret_cast<const char8_t*>(literal), N - 1) {
            assert(*chars._end == 0);
            if (!_invariant())
                throw EINVAL;
        }

        // validating strlen
        string_view(const char* p)
        : string_view(reinterpret_cast<const char8_t*>(p),
                      strlen(p)) { // <-- validating strlen?
            assert(_invariant());
        }
        
        string_view(const char* p, size_type n)
        : chars(reinterpret_cast<const char8_t*>(p), n) {
            if (!_invariant())
                throw EINVAL;
        }

        bool empty() const {
            return chars.empty();
        }
        
        const_iterator begin() const {
            return const_iterator(chars.begin());
        }
        
        const_iterator end() const {
            return const_iterator(chars.end());
        }
        
        const_iterator cbegin() const { return begin(); }
        const_iterator cend() const { return end(); }
        
        char32_t front() const {
            assert(!empty());
            return *begin();
        }
        
        char32_t back() const {
            assert(!empty());
            iterator it = end();
            return *--it;
        }
        
        bool operator==(const string_view& other) const {
            return std::equal(begin(), end(), other.begin(), other.end());
        }
        
        auto operator<=>(const string_view& other) const {
            return lexicographical_compare_three_way(begin(), end(), other.begin(), other.end());
        }
        
        void pop_front() { 
            assert(!empty());
            ++reinterpret_cast<iterator&>(chars._begin);
        }
        
        void pop_back() {
            --reinterpret_cast<iterator&>(chars._end);
        }
        
        void unsafe_unpop_front() {
            --reinterpret_cast<iterator&>(chars._begin);
        }
        
        void unsafe_unpop_back() {
            ++reinterpret_cast<iterator&>(chars._end);
        }

        
        void reset(string_view other) {
            chars.reset(other.chars);
        }
        
        // string concatenation is often modelled as + but it is better thought
        // of as noncommutative *
        
        // concatenate views, which must be consecutive
        string_view operator*(const string_view& other) const {
            assert(chars._end == other.chars._begin);
            return string_view(chars._begin, other.chars._end);
        }
        
        // division is the complementary operation to concatenation, removing
        // a suffix so that for c = a * b we have a = c / b
        
        // difference of a view and its suffix
        string_view operator/(const string_view& other) const {
            assert(chars._end == other.chars._end);
            assert(chars._begin <= other.chars._begin);
            return string_view(chars._begin, other.chars._begin);
        }
                
    }; // struct string_view
    
    inline std::ostream& operator<<(std::ostream& a, string_view b) {
        a.write(reinterpret_cast<const char*>(b.chars.begin()),
                b.chars.size());
        return a;
    }
    
    inline uint64_t hash(string_view v) {
        return hash_combine(v.chars.begin(), v.chars.size(), 0);
    }
    
    inline uint64_t hash(const char8_t* c) {
        return hash(string_view(c));
    }
    
    
} // namespace wry

#endif /* string_view_hpp */
