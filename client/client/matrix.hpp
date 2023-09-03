//
//  matrix.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef matrix_hpp
#define matrix_hpp

#include <numeric>
#include <vector>
#include <iostream>

#include "matrix_view.hpp"
#include "utility.hpp"

namespace wry {
    
    
    //
    // Views into row-major memory.
    //
    // We use a hierarchy matrix<T> : matrix_view<T> : const_matrix_view<T>,
    // corresponding to mutable metadata, mutable data, and immutable.
    //
    // matrix_view can only be copy-constructed from a mutable matrix_view&, so
    // that a const matrix cannot be used to produce a non-const matrix_view.
    //
    // The views have reference sematics.  They can be copy constructed, but
    // assignment (if mutable) copies the viewed sequences.  There is no way
    // to change the view, so it behaves like a non-const reference or a
    // T* const.  Views may be passed by value.
    //
    
    
    template<typename T>
    struct matrix : matrix_view<T> {
        
        T* _allocation;
        isize _capacity;
        
        bool _invariant() {
            assert(this->_columns >= 0);
            assert(this->_stride >= 0);
            assert(this->_rows >= 0);
            assert(this->_begin >= this->_allocation);
            assert(this->_stride >= this->_columns);
            assert(this->_begin + (this->_rows - 1) * this->_stride + (this->_columns)
                   <= this->_allocation + this->_capacity);
            return true;
        }
        
        matrix()
        : matrix_view<T>(nullptr, 0, 0, 0)
        , _allocation(nullptr)
        , _capacity(0) {
        }
        
        matrix(const matrix& r) : matrix() {
            *this = r;
        }
        
        matrix(matrix&& r) : matrix() {
            r.swap(*this);
        }
        
        matrix(const_matrix_view<T> v)
        : matrix() {
            *this = v;
        }
        
        matrix(isize rows, isize columns)
        : matrix_view<T>(nullptr, columns, columns, rows)
        , _allocation((T*) std::calloc(rows * columns, sizeof(T)))
        , _capacity(rows * columns) {
            this->_begin = this->_allocation;
            std::uninitialized_default_construct_n(this->_begin, rows * columns);
            assert(_invariant());
        }
        
        matrix(isize rows, isize columns, const T& x)
        : matrix(rows, columns) {            
            this->_begin = this->_allocation;
            std::uninitialized_fill_n(this->_begin, rows * columns, x);
            assert(_invariant());
        }
        
        void _destroy_all() {
            if constexpr (!std::is_trivially_destructible<T>::value) {
                for (isize i = 0; i != this->_rows; ++i)
                    std::destroy_n(this->_begin + i * this->_stride, this->_columns);
            }
        }
        
        ~matrix() {
            free(_allocation);
        }
        
        matrix& operator=(const matrix& r) {
            return operator=(static_cast<const_matrix_view<T>>(r));
        }
        
        matrix& operator=(matrix&& r) {
            matrix(std::move(r)).swap(*this);
            return *this;
        }
        
        matrix& operator=(const_matrix_view<T> other) {
            assert(_invariant());
            _destroy_all();
            if (this->_capacity < other.rows() * other.columns()) {
                std::size_t n = std::max(other.rows() * other.columns(),
                                         this->_capacity << 1);
                T* p = (T*) std::calloc(n, sizeof(T));
                free(exchange(this->_allocation, p, nullptr));
                this->_capacity = n;
            }
            this->_begin = this->_allocation;
            this->_columns = other._columns;
            this->_stride = other._columns;
            this->_rows = other._rows;
            for (isize i = 0; i != this->_rows; ++i)
                std::uninitialized_copy_n(other._begin + i * other._stride,
                                          other._columns,
                                          this->_begin + i * this->_stride);
            assert(_invariant());
            return *this;
        }
        
        matrix& operator=(const T& x) {
            std::fill(this->begin(), this->end(), x);
            return *this;
        }
        
        
        using matrix_view<T>::swap;
        void swap(matrix& r) {
            assert(_invariant());
            assert(r._invariant());
            using std::swap;
            swap(this->_begin, r._begin);
            swap(this->_columns, r._columns);
            swap(this->_stride, r._stride);
            swap(this->_rows, r._rows);
            swap(this->_allocation, r._allocation);
            swap(this->_capacity, r._capacity);
            assert(_invariant());
        }
        
        void clear() {
            _destroy_all();
            this->_rows = 0;
            this->_columns = 0;
            assert(_invariant());
        }
        
        using matrix_view<T>::begin;
        using matrix_view<T>::end;
        using matrix_view<T>::size;
        using matrix_view<T>::operator[];
        
        
        // Mutators
        
        void crop(isize i, isize j, isize r, isize c) {
            assert(i >= 0);
            assert(j >= 0);
            assert(r >= 0);
            assert(c >= 0);
            assert(i + r <= this->_rows);
            assert(j + c <= this->_columns);
            
            if constexpr (!std::is_trivially_destructible<T>::value) {
                for (isize i2 = 0; i2 != i; ++i2) {
                    std::destroy_n(this->_begin + this->_stride * i2, this->_columns);
                }
                for (isize i2 = i; i2 != (i + r); ++i2) {
                    std::destroy_n(this->_begin + this->_stride * i2, j);
                    std::destroy_n(this->_begin + this->_stride * i2 + j + c, this->_columns - j - c);
                }
                for (isize i2 = i + r; i2 != this->_rows; ++i2) {
                    std::destroy_n(this->_begin + this->_stride * i2, this->_columns);
                }
            }
            
            this->_begin += i * this->_stride + j;
            this->_columns = c;
            this->_rows = r;
            assert(_invariant());
            
        }
        
        // Resizes without preserving values
        void discard_and_resize(isize rows, isize columns) {
            _destroy_all();
            if (this->_capacity < rows * columns) {
                std::size_t n = std::max(rows * columns,
                                         this->_capacity << 1);
                T* p = (T*) std::calloc(n, sizeof(T));
                free(exchange(this->_allocation, p, nullptr));
                this->_capacity = n;
            }
            this->_begin = this->_allocation;
            this->_columns = columns;
            this->_stride = columns;
            this->_rows = rows;
            std::uninitialized_default_construct_n(this->_begin, rows * columns);
            assert(_invariant());
            
        }
        
        void expand(isize i, isize j, isize r, isize c, const T& x) {
            // todo: detect when we can do this in-place
            matrix<T> a(r, c, x);
            a.sub(i, j, this->_rows, this->_columns) = *this;
            a.swap(*this);
        }
        
        // Resizes preserving values, and padding with x
        void resize(isize r, isize c, const T& x = T()) {
            assert(_invariant());
            matrix<T> a(r, c, x);
            r = std::min(r, this->_rows);
            c = std::min(c, this->_columns);
            a.sub(0, 0, r, c) = this->sub(0, 0, r, c);
            assert(_invariant());
            a.swap(*this);
        }
        
    };
    
    template<typename T>
    void swap(matrix<T>& a, matrix<T>& b) {
        a.swap(b);
    }
    
    template<typename T> matrix<T> operator+(matrix_view<T> a, matrix_view<T> b) {
        assert(a.rows() == b.rows());
        assert(b.columns() == b.columns());
        matrix<T> c(a.rows(), a.columns());
        for (isize i = 0; i != a.rows(); ++i)
            for (isize j = 0; j != a.columns(); ++j)
                c(i, j) = a(i, j) + b(i, j);
        return c;
    }
    
    
    template<typename T> matrix<T> operator+(matrix_view<T> a, T b) {
        matrix<T> c(a.rows(), a.columns());
        for (isize i = 0; i != a.rows(); ++i)
            for (isize j = 0; j != a.columns(); ++j)
                c(i, j) = a(i, j) + b;
        return c;
    }
    
    template<typename T> matrix<T> transpose(const_matrix_view<T> a) {
        matrix<T> b(a.columns(), a.rows());
        for (isize i = 0; i != a.rows(); ++i)
            for (isize j = 0; j != a.columns(); ++j)
                b(j, i) = a(i, j);
        return b;
    }
    
    template<typename T> matrix<T> outer_product(const_vector_view<T> a,
                                                 const_vector_view<T> b) {
        matrix<T> c(a.size(), b.size());
        for (isize i = 0; i != c.rows(); ++i)
            for (isize j = 0; j != c.columns(); ++j)
                c(i, j) = a[i] * b[j];
        return c;
    }
    
    template<typename T>
    matrix<T> operator-(const_matrix_view<T> a, const_matrix_view<T> b) {
        matrix<T> c(a.rows(), a.columns());
        for (isize i = 0; i != a.rows(); ++i)
            for (isize j = 0; j != a.columns(); ++j)
                c(i, j) = a(i, j) - b(i, j);
        return c;
    }
    
    
    template<typename A, typename B, typename C>
    void filter_rows(matrix_view<C> c, const_matrix_view<A> a, const_vector_view<B> b) {
        assert(c.rows() == a.rows());
        assert(c.columns() + b.columns() == a.columns());
        for (isize i = 0; i != c.rows(); ++i)
            for (isize j = 0; j != c.columns(); ++j) {
                for (isize k = 0; k != b.size(); ++k)
                    c(i, j) += a(i, j + k) * b(k);
            }
    }
    
    template<typename A, typename B, typename C>
    void filter_columns(matrix_view<C> c, const_matrix_view<A> a, const_vector_view<B> b) {
        assert(c.columns() == a.columns());
        assert(c.rows() + b.size() == a.rows());
        for (isize i = 0; i != c.rows(); ++i)
            for (isize j = 0; j != c.columns(); ++j) {
                for (isize k = 0; k != b.size(); ++k)
                    c(i, j) += a(i + k, j) * b(k);
            }
    }
    
    template<typename A, typename B>
    void explode(matrix_view<B> b, const_matrix_view<A> a) {
        assert(b.rows() == 2 * a.rows());
        assert(b.columns() == 2 * a.columns());
        for (isize i = 0; i != a.rows(); ++i)
            for (isize j = 0; j != a.columns(); ++j)
                b(2 * i, 2 * j) = a(i, j);
    }
    
    
    
    
}

#endif /* matrix_hpp */
