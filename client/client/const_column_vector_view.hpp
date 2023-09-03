//
//  const_column_vector_view.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef const_column_vector_view_hpp
#define const_column_vector_view_hpp

#include <iostream>

#include "stride_ptr.hpp"

namespace wry {
    
    template<typename T>
    struct const_column_vector_view {
        
        using value_type = T;
        using size_type = ptrdiff_t;
        using difference_type = ptrdiff_t;
        using reference = const T&;
        using const_reference = const T&;
        using iterator = stride_ptr<const T>;
        using const_iterator = stride_ptr<const T>;
        
        T* _begin;
        ptrdiff_t _stride;
        ptrdiff_t _rows;
        
        const_column_vector_view() = delete;
        
        const_column_vector_view(T* p, ptrdiff_t stride, ptrdiff_t rows)
        : _begin(p)
        , _stride(stride)
        , _rows(rows) {
        }
        
        const_column_vector_view(std::nullptr_t, ptrdiff_t s, ptrdiff_t n)
        : _begin(nullptr)
        , _stride(s)
        , _rows(n) {
        }
        
        const_column_vector_view(const T* ptr, ptrdiff_t s, ptrdiff_t n)
        : _begin(const_cast<T*>(ptr))
        , _stride(s)
        , _rows(n) {
        }
        
        ~const_column_vector_view() = default;
        
        const_column_vector_view& operator=(const const_column_vector_view&) = delete;
        const_column_vector_view& operator=(const_column_vector_view&&) = delete;
        
        const_iterator begin() const { return const_iterator(_begin, _stride); }
        const_iterator end() const { return const_iterator(_begin + _stride * _rows, _stride); }
        ptrdiff_t size() const { return _rows; }
        ptrdiff_t rows() const { return _rows; }
        const T& operator[](ptrdiff_t i) const { return _begin[i * _stride]; }
        const T& operator()(ptrdiff_t i) const { return _begin[i * _stride]; }
        const T& front() const { return *_begin; }
        const T& back() const { return _begin[_stride * (_rows - 1)]; }
        
        const_column_vector_view sub(ptrdiff_t i, ptrdiff_t n) const {
            return const_column_vector_view(_begin + i * _stride, _stride, n);
        }
        
        void print() const {
            for (auto&& a : *this)
                std::cout << a << ", ";
            std::cout << std::endl;
        }
        
    };
    
    template<typename T>
    struct const_column_vector_view<const T>;
    
    
}

#endif /* const_column_vector_view_hpp */
