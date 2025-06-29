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
    
    template<typename> 
    struct matrix_view;
        
    template<typename T>
    struct Rank<matrix_view<T>> 
    : std::integral_constant<std::size_t, wry::Rank<T>::value + 2> {
    };
    
    template<typename T>
    struct matrix_view {
        
        using element_type = std::decay_t<T>;
        using value_type = vector_view<T>;
        using size_type = size_type;
        using difference_type = difference_type;
        using reference = vector_view<T>;
        using const_reference = vector_view<std::add_const_t<T>>;
        using iterator = minor_iterator<T>;
        using const_iterator = minor_iterator<std::add_const_t<T>>;

        stride_iterator<T> base;
        size_type _minor;
        size_type _major;
        
        matrix_view() = delete;
        matrix_view(const matrix_view&) = default;
        matrix_view(matrix_view&&) = default;
        ~matrix_view() = default;

        matrix_view& operator=(const matrix_view& other) {
            copy_checked(other.begin(), other.end(), begin(), end());
            return *this;
        }
        
        matrix_view& operator=(matrix_view&& other) {
            copy_checked(other.begin(), other.end(), begin(), end());
            return *this;
        }

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
                
        matrix_view& operator=(auto&& other) {
            if constexpr (rank_v<std::decay_t<decltype(other)>>) {
                using std::begin;
                using std::end;
                copy_checked(begin(other), end(other), this->begin(), this->end());
            } else {
                std::fill(begin(), end(), std::forward<decltype(other)>(other));
            }
            return *this;
        }
        
        size_type minor() const { return _minor; }
        size_type major() const { return _major; }
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
        
        T& operator[](difference_type i, difference_type j) const {
            return (base + i).base[j];
        }
                
        reference front() const { return reference(base.base, _major); }
        reference back() const { return reference((base + _minor - 1).base, _major); }
        
        matrix_view<T> sub(difference_type i,
                           difference_type j,
                           difference_type minor,
                           difference_type major) const {
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
        
        difference_type major_bytes() const {
            return base._stride_bytes;
        }

    }; // struct matrix_view
    
} // namespace wry

#endif /* matrix_view_hpp */

