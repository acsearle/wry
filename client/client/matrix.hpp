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

#include "matrix_transpose_view.hpp"
#include "matrix_view.hpp"

namespace wry {
    
    // matrix is indexed (i, j) = (row, column)
    // matrix is stored column major
    //
    // image is indexed (x, y) = (column, row)
    // image is stored row major
    //
    // these objects differ only in our interpretation of the major and minor
    // indices
    //
    // like wry::array, we support expansion along any dimension by
    // (ruinous) overallocation and amortization

    
    template<typename T>
    struct matrix;
    
    template<typename T>
    struct matrix<const T>; // undefined
    
    template<typename T>
    struct rank<matrix<T>> : std::integral_constant<std::size_t, wry::rank<T>::value + 2> {};
    

    template<typename T>
    struct matrix {
        
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using value_type = vector_view<T>;
        using iterator = minor_iterator<T>;
        using const_iterator = minor_iterator<const T>;
        using reference = vector_view<T>;
        using const_reference = vector_view<const T>;
        
        stride_iterator<T> base;
        size_t _minor;
        size_t _major;
        void* _allocation;
        size_t _capacity;
                
        size_t bytes_per_row() const {
            return base._stride;
        }
        
        matrix()
        : base(nullptr)
        , _minor(0)
        , _major(0)
        , _allocation(nullptr)
        , _capacity(0) {
        }
        
        matrix(size_t minor, size_t major) {
            _minor = minor;
            _major = major;
            base._stride = _major * sizeof(T);
            _capacity = base._stride * _minor;
            _allocation = operator new(_capacity);
            assert(_allocation);
            base.base = static_cast<T*>(_allocation);
        }
        
        matrix(const matrix& other)
        : matrix(other._minor, other._major) {
            std::copy(other.begin(), other.end(), begin());
        }
        
        matrix(matrix&& other)
        : base(exchange(other.base, nullptr))
        , _minor(exchange(other._minor, 0))
        , _major(exchange(other._major, 0))
        , _allocation(exchange(other._allocation, nullptr))
        , _capacity(exchange(other._capacity, 0)) {
        }
        
        ~matrix() {
            operator delete(_allocation);
        }
        
        void swap(matrix& other) {
            using std::swap;
            swap(base, other.base);
            swap(_minor, other._minor);
            swap(_major, other._major);
            swap(_allocation, other._allocation);
            swap(_capacity, other._capacity);
        }
        
        matrix& operator=(matrix&& other) {
            matrix(std::move(other)).swap(*this);
            return *this;
        }
        
        matrix& operator=(auto&& other) {
            wry::copy(std::begin(other), std::end(other), begin(), end());
            return *this;
        }
        
        size_type get_major() const { return _major; }
        size_type get_minor() const { return _minor; }
        difference_type get_stride() const { return base._stride; }
        
        size_type size() const { return _minor; }
        
        iterator begin() const {
            return iterator(base, _major);
        }
        
        iterator end() const {
            return iterator(base + _minor, _major);
        }
        
        const_iterator cbegin() const {
            return const_iterator(base, _major);
        }
        
        const_iterator cend() const {
            return const_iterator(base + _minor, _major);
        }
        
        reference operator[](difference_type i) {
            return reference((base + i).base, _major);
        }

        const_reference operator[](difference_type i) const {
            return reference((base + i).base, _major);
        }

        T& operator()(difference_type i, difference_type j) {
            return (base + i).base[j];
        }

        const T& operator()(difference_type i, difference_type j) const {
            return (base + i).base[j];
        }
        
        reference front() const { return reference(base._pointer, _major); }
        reference back() const { return reference((base + _minor - 1)._pointer, _major); }
        
        matrix_view<T> sub(ptrdiff_t i,
                           ptrdiff_t j,
                           ptrdiff_t minor,
                           ptrdiff_t major) const {
            assert(0 <= i);
            assert(0 <= j);
            assert(0 <= minor);
            assert(i + minor <= _minor);
            assert(0 <= major);
            assert(j + major <= _major);
            return matrix_view(stride_iterator<T>(base.base + j,
                                                  base._stride) + i,
                               minor,
                               major);
        }
        
        operator matrix_view<T>() {
            return matrix_view<T>(base, _minor, major);
        }
        
        operator matrix_view<const T>() const {
            return matrix_view<const T>(base, _minor, _major);
        }
        
        matrix_transpose_view<T> transpose() {
            return matrix_transpose_view<T>(base, _minor, _major);
        }
        
        T* data() {
            return base.base;
        }

        const T* data() const {
            return base.base;
        }

    };

    template<typename T>
    matrix_view(stride_iterator<T>, std::size_t, std::size_t) -> matrix_view<T>;
    
    
} // namespace wry



/*
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
        ptrdiff_t _capacity;
        
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
        
        matrix(ptrdiff_t rows, ptrdiff_t columns)
        : matrix_view<T>(nullptr, columns, columns, rows)
        , _allocation((T*) std::calloc(rows * columns, sizeof(T)))
        , _capacity(rows * columns) {
            this->_begin = this->_allocation;
            std::uninitialized_default_construct_n(this->_begin, rows * columns);
            assert(_invariant());
        }
        
        matrix(ptrdiff_t rows, ptrdiff_t columns, const T& x)
        : matrix(rows, columns) {            
            this->_begin = this->_allocation;
            std::uninitialized_fill_n(this->_begin, rows * columns, x);
            assert(_invariant());
        }
        
        void _destroy_all() {
            if constexpr (!std::is_trivially_destructible<T>::value) {
                for (ptrdiff_t i = 0; i != this->_rows; ++i)
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
            for (ptrdiff_t i = 0; i != this->_rows; ++i)
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
        
        void crop(ptrdiff_t i, ptrdiff_t j, ptrdiff_t r, ptrdiff_t c) {
            assert(i >= 0);
            assert(j >= 0);
            assert(r >= 0);
            assert(c >= 0);
            assert(i + r <= this->_rows);
            assert(j + c <= this->_columns);
            
            if constexpr (!std::is_trivially_destructible<T>::value) {
                for (ptrdiff_t i2 = 0; i2 != i; ++i2) {
                    std::destroy_n(this->_begin + this->_stride * i2, this->_columns);
                }
                for (ptrdiff_t i2 = i; i2 != (i + r); ++i2) {
                    std::destroy_n(this->_begin + this->_stride * i2, j);
                    std::destroy_n(this->_begin + this->_stride * i2 + j + c, this->_columns - j - c);
                }
                for (ptrdiff_t i2 = i + r; i2 != this->_rows; ++i2) {
                    std::destroy_n(this->_begin + this->_stride * i2, this->_columns);
                }
            }
            
            this->_begin += i * this->_stride + j;
            this->_columns = c;
            this->_rows = r;
            assert(_invariant());
            
        }
        
        // Resizes without preserving values
        void discard_and_resize(ptrdiff_t rows, ptrdiff_t columns) {
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
        
        void expand(ptrdiff_t i, ptrdiff_t j, ptrdiff_t r, ptrdiff_t c, const T& x) {
            // todo: detect when we can do this in-place
            matrix<T> a(r, c, x);
            a.sub(i, j, this->_rows, this->_columns) = *this;
            a.swap(*this);
        }
        
        // Resizes preserving values, and padding with x
        void resize(ptrdiff_t r, ptrdiff_t c, const T& x = T()) {
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
        for (ptrdiff_t i = 0; i != a.rows(); ++i)
            for (ptrdiff_t j = 0; j != a.columns(); ++j)
                c(i, j) = a(i, j) + b(i, j);
        return c;
    }
    
    
    template<typename T> matrix<T> operator+(matrix_view<T> a, T b) {
        matrix<T> c(a.rows(), a.columns());
        for (ptrdiff_t i = 0; i != a.rows(); ++i)
            for (ptrdiff_t j = 0; j != a.columns(); ++j)
                c(i, j) = a(i, j) + b;
        return c;
    }
    
    template<typename T> matrix<T> transpose(const_matrix_view<T> a) {
        matrix<T> b(a.columns(), a.rows());
        for (ptrdiff_t i = 0; i != a.rows(); ++i)
            for (ptrdiff_t j = 0; j != a.columns(); ++j)
                b(j, i) = a(i, j);
        return b;
    }
    
    template<typename T> matrix<T> outer_product(const_vector_view<T> a,
                                                 const_vector_view<T> b) {
        matrix<T> c(a.size(), b.size());
        for (ptrdiff_t i = 0; i != c.rows(); ++i)
            for (ptrdiff_t j = 0; j != c.columns(); ++j)
                c(i, j) = a[i] * b[j];
        return c;
    }
    
    template<typename T>
    matrix<T> operator-(const_matrix_view<T> a, const_matrix_view<T> b) {
        matrix<T> c(a.rows(), a.columns());
        for (ptrdiff_t i = 0; i != a.rows(); ++i)
            for (ptrdiff_t j = 0; j != a.columns(); ++j)
                c(i, j) = a(i, j) - b(i, j);
        return c;
    }
    
    
    template<typename A, typename B, typename C>
    void filter_rows(matrix_view<C> c, const_matrix_view<A> a, const_vector_view<B> b) {
        assert(c.rows() == a.rows());
        assert(c.columns() + b.columns() == a.columns());
        for (ptrdiff_t i = 0; i != c.rows(); ++i)
            for (ptrdiff_t j = 0; j != c.columns(); ++j) {
                for (ptrdiff_t k = 0; k != b.size(); ++k)
                    c(i, j) += a(i, j + k) * b(k);
            }
    }
    
    template<typename A, typename B, typename C>
    void filter_columns(matrix_view<C> c, const_matrix_view<A> a, const_vector_view<B> b) {
        assert(c.columns() == a.columns());
        assert(c.rows() + b.size() == a.rows());
        for (ptrdiff_t i = 0; i != c.rows(); ++i)
            for (ptrdiff_t j = 0; j != c.columns(); ++j) {
                for (ptrdiff_t k = 0; k != b.size(); ++k)
                    c(i, j) += a(i + k, j) * b(k);
            }
    }
    
    template<typename A, typename B>
    void explode(matrix_view<B> b, const_matrix_view<A> a) {
        assert(b.rows() == 2 * a.rows());
        assert(b.columns() == 2 * a.columns());
        for (ptrdiff_t i = 0; i != a.rows(); ++i)
            for (ptrdiff_t j = 0; j != a.columns(); ++j)
                b(2 * i, 2 * j) = a(i, j);
    }
    
    
    
    
}
 */

#endif /* matrix_hpp */
