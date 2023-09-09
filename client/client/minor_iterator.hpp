//
//  minor_iterator.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef const_minor_iterator_hpp
#define const_minor_iterator_hpp

#include "indirect.hpp"
#include "vector_view.hpp"
#include "stride_iterator.hpp"

namespace wry {
    
    // iterate across the minor (strided) index of a matrix
    
    template<typename T>
    struct minor_iterator {
        
        using difference_type = std::ptrdiff_t;
        using value_type = vector_view<T>;
        using pointer = indirect<value_type>;
        using reference = vector_view<T>;
        using iterator_category = std::random_access_iterator_tag;

        stride_iterator<T> _pointer;
        size_t _major;
        
        minor_iterator() = default;
        
        template<typename U>
        minor_iterator(const minor_iterator<U>& other)
        : _pointer(other._pointer)
        , _major(other._major) {
        }
        
        template<typename U>
        minor_iterator(stride_iterator<U> p, size_t major)
        : _pointer(p)
        , _major(major) {
        }
        
        ~minor_iterator() = default;
        
        template<typename U>
        minor_iterator& operator=(const minor_iterator<U>& other) {
            _pointer = other._pointer;
            _major = other._major;
            return *this;
        }
        
        reference operator*() const {
            return value_type(_pointer.operator->(), _major);
        }
        
        pointer operator->() const {
            return pointer(operator*());
        }
        
        reference operator[](ptrdiff_t i) const {
            auto q = _pointer + i;
            return reference(q.operator->(), _major);
        }
        
        minor_iterator operator++(int) {
            return minor_iterator(_pointer++, _major);
        }
        
        minor_iterator operator--(int) {
            return minor_iterator(_pointer--, _major);
        }

        minor_iterator& operator++() {
            ++_pointer;
            return *this;
        }
        
        minor_iterator& operator--() {
            --_pointer;
            return *this;
        }
        
        minor_iterator& operator+=(std::ptrdiff_t i) {
            _pointer += i;
            return *this;
        }
        
        minor_iterator& operator-=(std::ptrdiff_t i) {
            _pointer -= i;
            return *this;
        }
        
        bool operator==(const minor_iterator&) const = default;
                
    }; // const_matrix_iterator
    
    template<typename T>
    minor_iterator<T> operator+(const minor_iterator<T>& p, std::ptrdiff_t n) {
        return minor_iterator<T>(p._pointer + n, p._major);
    }

    template<typename T>
    minor_iterator<T> operator+(std::ptrdiff_t n, const minor_iterator<T>& p) {
        return minor_iterator<T>(n + p._pointer, p._major);
    }

    template<typename T>
    minor_iterator<T> operator-(const minor_iterator<T>& p, std::ptrdiff_t n) {
        return minor_iterator(p._pointer - n, p._major);
    }

    template<typename T, typename U>
    auto operator-(const minor_iterator<T>& a, const minor_iterator<U>& b) {
        return a._pointer - b._pointer;
    }
    
} // namespace wry

#endif /* minor_iterator_hpp */

