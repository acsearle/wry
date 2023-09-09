//
//  vector_view.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef vector_view_hpp
#define vector_view_hpp

#include <iterator>
#include "type_traits.hpp"
#include "utility.hpp"

namespace wry {
    
    // View a series of elements produced by an iterator
    // Typically just a pointer
    
    template<typename T, typename Iterator = T*, typename ConstIterator = const T*>
    struct vector_view {
        
        using value_type = std::remove_const_t<T>;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = T&;
        using const_reference = std::add_const_t<T>&;
        using iterator = Iterator;
        using const_iterator = ConstIterator;
        
        Iterator base;
        std::ptrdiff_t _size;
        
        vector_view() = delete;

        template<typename T2, typename Iterator2, typename ConstIterator2>
        vector_view(const vector_view<T2, Iterator2, ConstIterator2>& other)
        : base(other.base)
        , _size(other._size) {
        }

        vector_view(auto first, auto last)
        : base(first)
        , _size(std::distance(first, last)) {
        }

        vector_view(auto first, std::size_t count)
        : base(first)
        , _size(count) {
        }
        
        ~vector_view() = default;
        
        vector_view& operator=(auto&& other) {
            wry::copy(std::begin(other), std::end(other), begin(), end());
            return *this;
        }
        
        size_type size() const { return _size; }
        bool empty() const { return !_size; }
        
        iterator begin() const { return base; }
        iterator end() const { return base + _size; }
        const_iterator cbegin() const { return base; }
        const_iterator cend() const { return base + _size; }

        T& operator[](std::size_t i) const { return base[i]; }
        T& front() const { return *base; }
        T& back() const { return base[_size - 1]; }
        
        vector_view sub(std::ptrdiff_t i, std::size_t n) const {
            assert((0 <= i) && (i + n < size()));
            return vector_view(base + i, n);
        }
        
        void assign(auto&& first, auto&& last) {
            wry::copy(first, last, begin(), end());
        }
        
        void swap(auto&& other) {
            wry::swap_ranges(std::begin(other), std::end(other), begin(), end());
        }
                
    }; // struct vector_view<T>
    
    template<typename... Args>
    void swap(vector_view<Args...>& a, auto&& b) {
        a.swap(std::forward<decltype(b)>(b));
    }
    
    template<typename T>
    struct stride_iterator;
    
    template<typename T>
    using stride_view = vector_view<T, stride_iterator<T>, stride_iterator<std::add_const_t<T>>>;

    
    template<typename Serializer, typename... Args>
    void serialize(const vector_view<Args...>& v, Serializer& s) {
        serialize(v.size(), s);
        for (auto&& x : v)
            serialize(x, s);
    }

} // namespace wry

#endif /* vector_view_hpp */

/*
 
 
 namespace wry {
 
 // Views contiguous objects.  Reference semantics, so assignment copies
 // the viewed elements

 
 // Undefined specialization prevents instantiation with const T
 template<typename T> struct const_vector_view<const T>;
 
 template<typename T, typename U>
 auto dot(const_vector_view<T> a, const_vector_view<U> b) {
 return std::accumulate(a.begin(), a.end(), b.begin(), decltype(std::declval<T>() * std::declval<U>())(0));
 }
 
 template<typename T>
 T sum(const_vector_view<T> a, T b = 0.0) {
 for (isize i = 0; i != a.size(); ++i)
 b += a(i);
 return b;
 }

 } // namespace manic

 
 */
