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

    // this iterator yields views of successive columns of a matrix (rows of
    // an image)
    
    template<typename T>
    struct minor_iterator {
        
        using difference_type = difference_type;
        using value_type = vector_view<T>;
        using pointer = indirect<value_type>;
        using reference = vector_view<T>;
        using iterator_category = std::random_access_iterator_tag;

        stride_iterator<T> _iterator;
        size_t _major;
        
        minor_iterator() = default;
        minor_iterator(const minor_iterator&) = default;
        minor_iterator(minor_iterator&&) = default;
        ~minor_iterator() = default;
        minor_iterator& operator=(const minor_iterator&) = default;
        minor_iterator& operator=(minor_iterator&&) = default;
        
        template<typename U>
        minor_iterator(const minor_iterator<U>& other)
        : _iterator(other._iterator)
        , _major(other._major) {
        }
        
        template<typename U>
        minor_iterator(stride_iterator<U> p, size_type major)
        : _iterator(p)
        , _major(major) {
        }
                
        template<typename U>
        minor_iterator& operator=(const minor_iterator<U>& other) {
            _iterator = other._iterator;
            _major = other._major;
            return *this;
        }
        
        reference operator*() const {
            return value_type(_iterator.operator->(), _major);
        }
        
        pointer operator->() const {
            return pointer(operator*());
        }
        
        reference operator[](ptrdiff_t i) const {
            auto q = _iterator + i;
            return reference(q.operator->(), _major);
        }
        
        minor_iterator operator++(int) {
            return minor_iterator(_iterator++, _major);
        }
        
        minor_iterator operator--(int) {
            return minor_iterator(_iterator--, _major);
        }

        minor_iterator& operator++() {
            ++_iterator;
            return *this;
        }
        
        minor_iterator& operator--() {
            --_iterator;
            return *this;
        }
        
        minor_iterator& operator+=(difference_type i) {
            _iterator += i;
            return *this;
        }
        
        minor_iterator& operator-=(difference_type i) {
            _iterator -= i;
            return *this;
        }
        
        bool operator==(const minor_iterator&) const = default;
                
    }; // const_matrix_iterator
    
    template<typename T>
    minor_iterator<T> operator+(const minor_iterator<T>& p, difference_type n) {
        return minor_iterator<T>(p._iterator + n, p._major);
    }

    template<typename T>
    minor_iterator<T> operator+(difference_type n, const minor_iterator<T>& p) {
        return minor_iterator<T>(n + p._iterator, p._major);
    }

    template<typename T>
    minor_iterator<T> operator-(const minor_iterator<T>& p, difference_type n) {
        return minor_iterator(p._iterator - n, p._major);
    }

    template<typename T, typename U>
    auto operator-(const minor_iterator<T>& a, const minor_iterator<U>& b) {
        assert(a._major == b._major);
        return a._iterator - b._iterator;
    }
    
} // namespace wry

#endif /* minor_iterator_hpp */

