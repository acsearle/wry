//
//  major_iterator.hpp
//  client
//
//  Created by Antony Searle on 9/9/2023.
//

#ifndef major_iterator_hpp
#define major_iterator_hpp

#include "indirect.hpp"
#include "vector_view.hpp"

namespace wry {
    
    // iterate across the major (contiguous) index of a matrix
    
    template<typename T>
    struct major_iterator {
        
        using difference_type = std::ptrdiff_t;
        using value_type = stride_view<T>;
        using pointer = indirect<value_type>;
        using reference = stride_view<T>;
        using iterator_category = std::random_access_iterator_tag;
        
        T* _pointer;
        ptrdiff_t _stride;
        size_t _minor;
        
        major_iterator() = default;
        
        template<typename U>
        major_iterator(const major_iterator<U>& other)
        : _pointer(other._pointer)
        , _stride(other._stride)
        , _minor(other._minor) {
        }
        
        template<typename U>
        major_iterator(U* p, ptrdiff_t stride, size_t minor)
        : _pointer(p)
        , _stride(stride)
        , _minor(minor) {
        }
        
        ~major_iterator() = default;
        
        template<typename U>
        major_iterator& operator=(const major_iterator<U>& other) {
            _pointer = other._pointer;
            _stride = other._stride;
            _minor = other._minor;
            return *this;
        }
        
        reference operator*() const {
            return value_type(stride_iterator(_pointer, _stride), _minor);
        }
        
        pointer operator->() const {
            return pointer(operator*());
        }
        
        reference operator[](ptrdiff_t i) const {
            return reference(stride_iterator(_pointer + i, _stride), _minor);
        }
        
        major_iterator operator++(int) {
            return minor_iterator(_pointer++, _stride, _minor);
        }
        
        major_iterator operator--(int) {
            return minor_iterator(_pointer--, _stride, _minor);
        }
        
        major_iterator& operator++() {
            ++_pointer;
            return *this;
        }
        
        major_iterator& operator--() {
            --_pointer;
            return *this;
        }
        
        major_iterator& operator+=(std::ptrdiff_t i) {
            _pointer += i;
            return *this;
        }
        
        major_iterator& operator-=(std::ptrdiff_t i) {
            _pointer -= i;
            return *this;
        }
        
        bool operator==(const major_iterator&) const = default;
        
    }; // const_matrix_iterator
    
    template<typename T>
    major_iterator<T> operator+(const major_iterator<T>& p, std::ptrdiff_t n) {
        return minor_iterator<T>(p._pointer + n, p._stride, p._minor);
    }
    
    template<typename T>
    major_iterator<T> operator+(std::ptrdiff_t n, const major_iterator<T>& p) {
        return minor_iterator<T>(n + p._pointer, p._stride, p._minor);
    }
    
    template<typename T>
    major_iterator<T> operator-(const major_iterator<T>& p, std::ptrdiff_t n) {
        return minor_iterator(p._pointer - n, p._stride, p._minor);
    }
    
    template<typename T, typename U>
    auto operator-(const major_iterator<T>& a, const major_iterator<U>& b) {
        assert(a._stride == b._stride);
        assert(a._minor == b._minor);
        return a._pointer - b._pointer;
    }
    
} // namespace wry

#endif /* major_iterator_hpp */
