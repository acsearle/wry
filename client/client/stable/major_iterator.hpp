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

    // this iterator yields views of successive rows of a matrix (columns of
    // an image)
    
    template<typename T>
    struct major_iterator {
        
        using difference_type = difference_type;
        using value_type = stride_view<T>;
        using pointer = indirect<value_type>;
        using reference = stride_view<T>;
        using iterator_category = std::random_access_iterator_tag;
        
        T* _pointer;
        difference_type _stride;
        size_type _minor;
        
        major_iterator() = default;
        major_iterator(const major_iterator&) = default;
        major_iterator(major_iterator&&) = default;
        ~major_iterator() = default;
        major_iterator& operator=(const major_iterator&) = default;
        major_iterator& operator=(major_iterator&&) = default;

        template<typename U>
        major_iterator(const major_iterator<U>& other)
        : _pointer(other._pointer)
        , _stride(other._stride)
        , _minor(other._minor) {
        }
        
        major_iterator(pointer p, difference_type stride, size_type minor)
        : _pointer(p)
        , _stride(stride)
        , _minor(minor) {
        }
                
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
            return major_iterator(_pointer++, _stride, _minor);
        }
        
        major_iterator operator--(int) {
            return major_iterator(_pointer--, _stride, _minor);
        }
        
        major_iterator& operator++() {
            ++_pointer;
            return *this;
        }
        
        major_iterator& operator--() {
            --_pointer;
            return *this;
        }
        
        major_iterator& operator+=(difference_type i) {
            _pointer += i;
            return *this;
        }
        
        major_iterator& operator-=(difference_type i) {
            _pointer -= i;
            return *this;
        }
        
        bool operator==(const major_iterator&) const = default;
        
    }; // const_matrix_iterator
    
    template<typename T>
    major_iterator<T> operator+(const major_iterator<T>& p, difference_type n) {
        return major_iterator<T>(p._pointer + n, p._stride, p._minor);
    }
    
    template<typename T>
    major_iterator<T> operator+(difference_type n, const major_iterator<T>& p) {
        return major_iterator<T>(n + p._pointer, p._stride, p._minor);
    }
    
    template<typename T>
    major_iterator<T> operator-(const major_iterator<T>& p, difference_type n) {
        return major_iterator(p._pointer - n, p._stride, p._minor);
    }
    
    template<typename T, typename U>
    auto operator-(const major_iterator<T>& a, const major_iterator<U>& b) {
        precondition(a._stride == b._stride);
        precondition(a._minor == b._minor);
        return a._pointer - b._pointer;
    }
    
} // namespace wry

#endif /* major_iterator_hpp */
