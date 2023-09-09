//
//  array_view.hpp
//  client
//
//  Created by Antony Searle on 9/9/2023.
//

#ifndef array_view_hpp
#define array_view_hpp

#include "utility.hpp"

namespace wry {
    
    template<typename T>
    struct array_view {
        
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using iterator = T*;
        using const_iterator = std::add_const_t<T>*;
        using reference = T&;
        using const_reference = std::add_const_t<T>&;
        
        T* _begin;
        size_t _size;
        
        template<typename U>
        array_view(U* p, size_t n)
        : _begin(p)
        , _size(n) {
        }

        template<typename U>
        array_view(U* first, U* last)
        : _begin(first)
        , _size(last - first) {
        }
        
        template<typename U>
        array_view(const array_view<U>& other)
        : _begin(other._begin)
        , _size(other._size) {
        }
        
        template<typename V>
        array_view& operator=(V&& other) {
            wry::copy(std::begin(other), std::end(other), begin(), end());
        }

        size_t size() const {
            return _size;
        }
        
        iterator begin() const {
            return _begin;
        }
        
        iterator end() const {
            return _begin + _size;
        }

        const_iterator cbegin() const {
            return _begin;
        }
        
        const_iterator cend() const {
            return _begin + _size;
        }
        
        bool empty() const {
            return !_size;
        }

        reference operator[](ptrdiff_t i) const {
            assert((0 <= i) && (i < _size));
            return _begin[i];
        }
        
        void pop_front(ptrdiff_t count = 1) {
            assert((0 < count) && (count <= _size));
            _begin += count;
            _size -= count;
        }
        
        void pop_back(ptrdiff_t count = 1) {
            assert((0 < count) && (count <= _size));
            _size -= count;
        }
        
        reference front() const {
            assert(_size);
            return _begin;
        }

        reference back() const {
            assert(_size);
            return *(_begin + _size - 1);
        }
        
        reference at(ptrdiff_t i) const {
            if ((i < 0) || (i >= _size))
                throw std::range_error(__PRETTY_FUNCTION__);
            return _begin[i];
        }

        T* to(ptrdiff_t i) const {
            return _begin + i;
        }
        
        T* data() const {
            return _begin;
        }
        
        std::size_t stride_in_bytes() const {
            return sizeof(T);
        }
        
        std::size_t size_in_bytes() const {
            return _size * sizeof(T);
        }

    };
    
}

#endif /* array_view_hpp */
