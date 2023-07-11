//
//  matrix_iterator.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef matrix_iterator_hpp
#define matrix_iterator_hpp

#include "const_matrix_iterator.hpp"
#include "vector_view.hpp"

namespace wry {
    
    template<typename T>
    struct matrix_iterator : const_matrix_iterator<T> {
        
        using difference_type = ptrdiff_t;
        using value_type = vector_view<T>;
        using pointer = indirect<vector_view<T>>;
        using reference = vector_view<T>;
        using iterator_category = std::random_access_iterator_tag;
        
        matrix_iterator() = default;
        matrix_iterator(const matrix_iterator&) = default;
        
        matrix_iterator(T* ptr, ptrdiff_t columns, ptrdiff_t stride)
        : const_matrix_iterator<T>(ptr, columns, stride) {
        }
        
        ~matrix_iterator() = default;
        
        matrix_iterator& operator=(const matrix_iterator&) = default;
        
        vector_view<T> operator*() const {
            return vector_view<T>(this->_begin, this->_columns);
        }
        
        vector_view<T> operator[](ptrdiff_t i) const {
            return vector_view<T>(this->_begin + i * this->_stride,
                                  this->_columns);
        }
        
        indirect<vector_view<T>> operator->() const {
            return indirect(**this);
        }
        
        matrix_iterator& operator++() {
            this->_begin += this->_stride;
            return *this;
        }
        
        matrix_iterator& operator--() {
            this->_begin -= this->_stride;
            return *this;
        }
        
        matrix_iterator& operator+=(ptrdiff_t i) {
            this->_begin += this->_stride * i;
            return *this;
        }
        
        matrix_iterator& operator-=(ptrdiff_t i) {
            this->_begin -= this->_stride * i;
            return *this;
        }
        
    };
    
    template<typename T>
    matrix_iterator<T> operator+(matrix_iterator<T> a, ptrdiff_t b) {
        return matrix_iterator<T>(a._begin + b * a._stride,
                                  a._columns,
                                  a._stride);
    }
    
    template<typename T>
    matrix_iterator<T> operator-(matrix_iterator<T> a, ptrdiff_t b) {
        return matrix_iterator<T>(a._begin - b * a._stride,
                                  a._columns,
                                  a._stride);
    }
    
} // namespace wry

#endif /* matrix_iterator_hpp */
