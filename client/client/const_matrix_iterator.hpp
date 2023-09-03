//
//  const_matrix_iterator.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef const_matrix_iterator_hpp
#define const_matrix_iterator_hpp

#include "const_vector_view.hpp"
#include "indirect.hpp"

namespace wry {
    
    template<typename T>
    struct const_matrix_iterator {
        
        using difference_type = ptrdiff_t;
        using value_type = const_vector_view<T>;
        using pointer = indirect<const_vector_view<T>>;
        using reference = const_vector_view<T>;
        using iterator_category = std::random_access_iterator_tag;
        
        T* _begin;
        ptrdiff_t _columns;
        ptrdiff_t _stride;
        
        const_matrix_iterator() : _begin(nullptr), _columns(0), _stride(0) {}
        
        const_matrix_iterator(const const_matrix_iterator&) = default;
        
        const_matrix_iterator(const T* ptr, ptrdiff_t columns, ptrdiff_t stride)
        : _begin(const_cast<T*>(ptr))
        , _columns(columns)
        , _stride(stride) {
            assert(columns <= stride);
        }
        
        ~const_matrix_iterator() = default;
        
        const_matrix_iterator& operator=(const_matrix_iterator const&) = default;
        
        const_vector_view<T> operator*() const {
            return const_vector_view<T>(_begin, _columns);
        }
        
        const_vector_view<T> operator[](ptrdiff_t i) const {
            return const_vector_view<T>(_begin + i * _stride, _columns);
        }
        
        const_matrix_iterator& operator++() {
            _begin += _stride;
            return *this;
        }
        
        const_matrix_iterator& operator--() {
            _begin -= _stride;
            return *this;
        }
        
        const_matrix_iterator& operator+=(ptrdiff_t i) {
            _begin += _stride * i;
            return *this;
        }
        
        const_matrix_iterator& operator-=(ptrdiff_t i) {
            _begin -= _stride * i;
            return *this;
        }
        
        indirect<const_vector_view<T>> operator->() const {
            return indirect(**this);
        }
        
    }; // const_matrix_iterator
    
    template<typename T>
    const_matrix_iterator<T> operator+(const_matrix_iterator<T> i, ptrdiff_t n) {
        return i += n;
    }
    
    template<typename T>
    bool operator==(const_matrix_iterator<T> a, const_matrix_iterator<T> b) {
        return a._begin == b._begin;
    }
    
    template<typename T>
    bool operator!=(const_matrix_iterator<T> a, const_matrix_iterator<T> b) {
        return a._begin != b._begin;
    }
    
    template<typename T>
    ptrdiff_t operator-(const_matrix_iterator<T> a, const_matrix_iterator<T> b) {
        //assert(a._stride || !a._columns);
        assert(a._stride == b._stride);
        return a._stride ? (a._begin - b._begin) / a._stride : 0;
    }
    
} // namespace manic

#endif /* const_matrix_iterator_hpp */
