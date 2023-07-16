//
//  matrix_view.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef matrix_view_h
#define matrix_view_h

#include "const_matrix_view.hpp"
#include "matrix_iterator.hpp"

namespace wry {
    
    template<typename T>
    struct matrix_view
    : const_matrix_view<T> {
        
        using value_type = const_vector_view<T>;
        using reference = value_type;
        using iterator = matrix_iterator<T>;
        
        matrix_view() = delete;
        
        matrix_view(matrix_view&) = default;
        matrix_view(matrix_view&&) = default;
        matrix_view(const matrix_view&) = delete;
        matrix_view(const matrix_view&&) = delete;
        
        matrix_view(T* ptr, ptrdiff_t columns, ptrdiff_t stride, ptrdiff_t rows)
        : const_matrix_view<T>(ptr, columns, stride, rows) {
        }
        
        ~matrix_view() = default;
        
        matrix_view& operator=(const matrix_view& v) {
            return *this = static_cast<const_matrix_view<T>>(v);
        }
        
        template<typename U>
        matrix_view& operator=(const_matrix_view<U> v) {
            assert(this->_rows == v._rows);
            std::copy(v.begin(), v.end(), begin());
            return *this;
        }
        
        matrix_view& operator=(const T& x) {
            std::fill(begin(), end(), x);
            return *this;
        }
        
        using const_matrix_view<T>::data;
        using const_matrix_view<T>::begin;
        using const_matrix_view<T>::end;
        using const_matrix_view<T>::operator[];
        using const_matrix_view<T>::operator();
        using const_matrix_view<T>::front;
        using const_matrix_view<T>::back;
        using const_matrix_view<T>::sub;
        
        T* data() { return this->_begin; }
        
        iterator begin() {
            return iterator(this->_begin,
                            this->_columns,
                            this->_stride);
        }
        
        iterator end() { return begin() + this->_rows; }
        
        vector_view<T> operator[](ptrdiff_t i) {
            assert(0 <= i);
            assert(i < this->_rows);
            return vector_view<T>(this->_begin + i * this->_stride, this->_columns);
        }
        
        
        vector_view<T> front() { return *begin(); }
        vector_view<T> back() { return begin()[this->_rows - 1]; }
        
        matrix_view<T> sub(ptrdiff_t i,
                           ptrdiff_t j,
                           ptrdiff_t r,
                           ptrdiff_t c) {
            assert(0 <= i);
            assert(0 <= j);
            assert(0 <= r);
            assert(i + r <= this->_rows);
            assert(0 <= c);
            assert(j + c <= this->_columns);
            return matrix_view<T>(this->_begin + i * this->_stride + j, c, this->_stride, r);
        }
        
        T& operator()(ptrdiff_t i, ptrdiff_t j) {
            assert(i >= 0);
            assert(j >= 0);
            assert(i < this->_rows);
            assert(j < this->_columns);
            return *(this->_begin + i * this->_stride + j);
        }
        
        T& operator()(simd_long2 ij) {
            return operator()(ij.x, ij.y);
        }
        
        void swap(matrix_view<T> v) {
            for (ptrdiff_t i = 0; i != this->_rows; ++i)
                operator[](i).swap(v[i]);
        }
        
        matrix_view& operator/=(const T& x) {
            for (ptrdiff_t i = 0; i != this->_rows; ++i)
                for (ptrdiff_t j = 0; j != this->_columns; ++j)
                    operator()(i, j) /= x;
            return *this;
        }
        
        matrix_view& operator*=(const T& x) {
            for (ptrdiff_t i = 0; i != this->_rows; ++i)
                for (ptrdiff_t j = 0; j != this->_columns; ++j)
                    operator()(i, j) *= x;
            return *this;
        }
        
        matrix_view& operator+=(const_matrix_view<T> x) {
            for (ptrdiff_t i = 0; i != this->_rows; ++i)
                for (ptrdiff_t j = 0; j != this->_columns; ++j)
                    operator()(i, j) += x(i, j);
            return *this;
        }
        
        matrix_view& operator-=(const_matrix_view<T> x) {
            for (ptrdiff_t i = 0; i != this->_rows; ++i)
                for (ptrdiff_t j = 0; j != this->_columns; ++j)
                    operator()(i, j) -= x(i, j);
            return *this;
        }
        
        matrix_view& operator*=(const_matrix_view<T> x) {
            for (ptrdiff_t i = 0; i != this->_rows; ++i)
                for (ptrdiff_t j = 0; j != this->_columns; ++j)
                    operator()(i, j) *= x(i, j);
            return *this;
        }
        
        
    }; // struct matrix_view<T>
    
    template<typename T>
    void swap(matrix_view<T> a, matrix_view<T> b) {
        a.swap(b);
    }
    
} // namespace manic

#endif /* matrix_view_h */
