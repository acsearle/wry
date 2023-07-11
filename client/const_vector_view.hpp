//
//  const_vector_view.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef const_vector_view_hpp
#define const_vector_view_hpp

#include <iostream>
#include <numeric>

#include "common.hpp"
#include "serialize.hpp"

namespace wry {
    
    // Views contiguous objects.  Reference semantics, so assignment copies
    // the viewed elements
    
    template<typename T>
    struct const_vector_view {
        
        using value_type = T;
        using size_type = isize;
        using difference_type = isize;
        using reference = const T&;
        using const_reference = const T&;
        using iterator = const T*;
        using const_iterator = const T*;
        
        T* _begin;
        isize _size;
        
        const_vector_view() = delete;
        const_vector_view(const const_vector_view& r) = default;
        const_vector_view(const_vector_view&& r) = default;
        
        const_vector_view(std::nullptr_t, isize n)
        : _begin(nullptr)
        , _size(n) {
            assert(n == 0);
        }
        
        const_vector_view(const T* ptr, isize n)
        : _begin(const_cast<T*>(ptr))
        , _size(n) {
            assert(n >= 0);
            assert(ptr || (n == 0));
        }
        
        const_vector_view(const T* first, const T* last)
        : _begin(const_cast<T*>(first))
        , _size(last - first) {
            assert(_size >= 0);
        }
        
        const_vector_view(const std::vector<T>& v)
        : _begin(const_cast<T*>(v.data()))
        , _size(v.size()) {
        }
        
        ~const_vector_view() = default;
        
        const_vector_view& operator=(const const_vector_view&) = delete;
        const_vector_view& operator=(const_vector_view&&) = delete;
        
        isize size() const { return _size; }
        isize columns() const { return _size; }
        bool empty() const { return !_size; }
        
        const T* begin() const { return _begin; }
        const T* end() const { return _begin + _size; }
        
        const T& operator[](isize i) const { return _begin[i]; }
        const T& operator()(isize i) const { return _begin[i]; }
        const T& front() const { return *_begin; }
        const T& back() const { return _begin[_size - 1]; }
        
        // void pop_front(isize i = 1) { assert(i <= _size); _begin += i; _size -= i; }
        // void pop_back(isize i = 1) { assert(i <= _size); _size -= i; }
        
        const_vector_view sub(isize i, isize n) const {
            return const_vector_view(_begin + i, n);
        }
        
        void print() const {
            for (auto&& a : *this)
                std::cout << a << ", ";
            std::cout << std::endl;
        }
        
    };
    
    
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
    
    template<typename T, typename Serializer>
    void serialize(const_vector_view<T> const& v, Serializer& s) {
        serialize(v.size(), s);
        for (auto&& x : v)
            serialize(x, s);
    }
    
} // namespace manic

#endif /* const_vector_view_hpp */
