//
//  matrix_view.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef matrix_view_hpp
#define matrix_view_hpp

#include <iostream>

#include "minor_iterator.hpp"
#include "major_iterator.hpp"

namespace wry {
    
    template<typename T>
    struct matrix_view {
        
        // by default we iterate across the minor axis and yield a contiguous
        // view of the major axis
        
        using value_type = vector_view<T>;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = vector_view<T>;
        using const_reference = vector_view<std::add_const_t<T>>;
        using iterator = minor_iterator<T>;
        using const_iterator = minor_iterator<std::add_const_t<T>>;

        stride_iterator<T> base;
        std::size_t _minor;
        std::size_t _major;
        
        matrix_view() = delete;
        
        template<typename U>
        matrix_view(const matrix_view<U>& other)
        : base(other.base)
        , _minor(other._minor)
        , _major(other._major) {
        }
        
        template<typename U>
        matrix_view(stride_iterator<U> p,
                    std::size_t minor,
                    std::size_t major)
        : base(p)
        , _minor(minor)
        , _major(major) {
        }
        
        ~matrix_view() = default;
        
        matrix_view& operator=(auto&& x) {
            wry::copy(std::begin(x), std::end(x), begin(), end());
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
        
        reference operator[](difference_type i) const {
            return reference((base + i).base, _major);
        }
        
        const T& operator()(difference_type i, difference_type j) const {
            return (base + i).base[j];
        }
                
        reference front() const { return reference(base.base, _major); }
        reference back() const { return reference((base + _minor - 1).base, _major); }
        
        matrix_view<T> sub(ptrdiff_t i,
                           ptrdiff_t j,
                           ptrdiff_t minor,
                           ptrdiff_t major) const {
            assert(0 <= i);
            assert(0 <= j);
            assert(0 <= minor);
            assert(i + minor <= _minor);
            assert(0 < major);
            assert(j + major <= _major);
            return matrix_view(stride_iterator<T>(base.base + j,
                                                  base._stride) + i,
                               minor,
                               major);
        }
        
        void print() const {
            for (auto&& row : *this) {
                for (auto&& value : row)
                    std::cout << value << ' ';
                std::cout << '\n';
            }
        }
        
        T* data() {
            return base.base;
        }

        const T* data() const {
            return base.base;
        }
        
        std::size_t bytes_per_row() const {
            return base._stride;
        }

    }; // struct matrix_view
    
} // namespace wry

#endif /* matrix_view_hpp */

