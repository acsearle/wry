//
//  matrix_transpose_view.hpp
//  client
//
//  Created by Antony Searle on 9/9/2023.
//

#ifndef matrix_transpose_view_hpp
#define matrix_transpose_view_hpp

#include <iostream>

#include "minor_iterator.hpp"
#include "major_iterator.hpp"

namespace wry {
    
    template<typename T>
    struct matrix_transpose_view {
        
        using value_type = stride_view<T>;
        using size_type = size_type;
        using difference_type = difference_type;
        using reference = stride_view<T>;
        using const_reference = stride_view<std::add_const_t<T>>;
        using iterator = major_iterator<T>;
        using const_iterator = major_iterator<std::add_const_t<T>>;
        
        T* base;
        difference_type _stride;
        size_type _minor;
        size_type _major;
        
        matrix_transpose_view() = delete;
        
        template<typename U>
        matrix_transpose_view(const matrix_transpose_view<U>& other)
        : base(other.base)
        , _stride(other._stride)
        , _minor(other._minor)
        , _major(other._major) {
        }
        
        template<typename U>
        matrix_transpose_view(stride_iterator<U> p,
                              size_type minor,
                              size_type major)
        : base(p.base)
        , _stride(p._stride)
        , _minor(minor)
        , _major(major) {
        }
        
        ~matrix_transpose_view() = default;
        
        matrix_transpose_view& operator=(auto&& x) {
            wry::copy(std::begin(x), std::end(x), begin(), end());
            return *this;
        }
        
        size_type get_major() const { return _major; }
        size_type get_minor() const { return _minor; }
        difference_type get_stride() const { return _stride; }
        
        size_type size() const { return _minor; }
        
        iterator begin() const {
            return iterator(base, _stride, _minor);
        }
        
        iterator end() const {
            return iterator(base + _major, _stride, _minor);
        }
        
        const_iterator cbegin() const {
            return const_iterator(base, _stride, _minor);
        }
        
        const_iterator cend() const {
            return const_iterator(base + _major, _stride, _minor);
        }
        
        reference operator[](difference_type i) const {
            return reference(base + i, _stride, _minor);
        }
        
        const T& operator[](difference_type i, difference_type j) const {
            return stride_iterator(base + i, _stride)[j];
        }
        
        reference front() const { return reference(base, _stride, _minor); }
        reference back() const { return reference(base + _major - 1, _stride, _minor); }
        
        matrix_transpose_view<T> sub(std::ptrdiff_t i,
                                     std::ptrdiff_t j,
                                     std::size_t minor,
                                     std::size_t major) const {
            assert(0 <= i);
            assert(0 <= j);
            assert(0 <= minor);
            assert(i + minor <= _minor);
            assert(0 < major);
            assert(j + major <= _major);
            return matrix_transpose_view(stride_iterator<T>(base + i, _stride) + j,
                                         minor,
                                         major);
        }
                
    }; // struct matrix_transpose_view
    
} // namespace wry

#endif /* matrix_transpose_view_hpp */
