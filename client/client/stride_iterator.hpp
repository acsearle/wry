//
//  stride_iterator.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef stride_iterator_hpp
#define stride_iterator_hpp

#include <iterator>

#include "common.hpp"
#include "type_traits.hpp"

namespace wry {

    // Fancy pointer that strides over multiple elements, as in traversing
    // the major axis of a matrix
    //
    // Stride is specified in bytes and must be a multiple of the alignment
    // of the type, but not necessarily of the size of the type
    //
    // Example: An RGB8 image with a width that is not divisible by four,
    // but whose rows are power-of-two aligned, will have a power of two
    // bytes per row and a fractional number of unused RGB slots at the end
    // of each row
    
    template<typename T>
    struct stride_iterator {
        
        using difference_type = ptrdiff_t;
        using value_type = std::remove_cv_t<T>;
        using pointer = T*;
        using reference = T&;
        using iterator_category = std::random_access_iterator_tag;
        
        using _byte_type = std::conditional_t<std::is_const_v<T>, const std::byte, std::byte>;
        using _void_type = std::conditional_t<std::is_const_v<T>, const void*, void*>;
        
        T* base;
        difference_type _stride;
        
        void _assert_invariant() const {
            assert(!(_stride % alignof(T)));
            assert((std::abs(_stride) >= sizeof(T)) || !base);
        }
        
        stride_iterator()
        : base(nullptr)
        , _stride(0) {
        }
        
        explicit stride_iterator(std::nullptr_t)
        : base(nullptr)
        , _stride(0) {
        }
        
        template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        stride_iterator(U* p, difference_type n)
        : base(p)
        , _stride(n) {
            _assert_invariant();
        }
        
        template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        stride_iterator(const stride_iterator<U>& other)
        : base(other.base)
        , _stride(other._stride) {
        }
        
        stride_iterator& operator=(std::nullptr_t) {
            base = nullptr;
            _stride = 0;
            return *this;
        }

        template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        stride_iterator& operator=(const stride_iterator<U>& other) {
            base = other.base;
            _stride = other._stride;return *this;
        }
        
        stride_iterator operator++(int) {
            stride_iterator result(*this);
            base = reinterpret_cast<T*>(reinterpret_cast<_byte_type*>(base) + _stride);
            return result;
        }
        
        stride_iterator operator--(int) {
            stride_iterator result(*this);
            base = reinterpret_cast<T*>(reinterpret_cast<_byte_type*>(base) - _stride);
            return result;
        }
        
        T& operator[](difference_type i) const {
            return *reinterpret_cast<T*>(reinterpret_cast<_byte_type*>(base) + _stride * i);
        }
        
        T* operator->() const {
            return base;
        }
        
        stride_iterator& operator++() {
            base = reinterpret_cast<T*>(reinterpret_cast<_byte_type*>(base) + _stride);
            return *this;
        }
        
        stride_iterator& operator--() {
            base = reinterpret_cast<T*>(reinterpret_cast<_byte_type*>(base) - _stride);
            return *this;
        }
        
        bool operator!() const {
            return !base;
        }
        
        explicit operator bool() const {
            return static_cast<bool>(base);
        }
        
        explicit operator T*() const {
            return base;
        }
        
        explicit operator _void_type() const {
            return static_cast<_void_type>(base);
        }
        
        T& operator*() const {
            assert(base);
            return *base;
        }
        
        bool operator==(std::nullptr_t) const {
            return !base;
        }

        template<typename U>
        bool operator==(const stride_iterator<U>& other) const {
            assert((_stride == other._stride) || !base || !other.base);
            return base == other.base;
        }
        
        stride_iterator& operator+=(difference_type i) {
            base = reinterpret_cast<T*>(reinterpret_cast<_byte_type*>(base) + _stride * i);
            return *this;
        }
        
        stride_iterator& operator-=(difference_type i) {
            base = reinterpret_cast<T*>(reinterpret_cast<_byte_type*>(base) - _stride * i);
            return *this;
        }
        
    };

    template<typename T>
    stride_iterator(T*, ptrdiff_t) -> stride_iterator<T>;
    
    template<typename T>
    stride_iterator<T> operator+(stride_iterator<T> x, ptrdiff_t y) {
        using U = typename stride_iterator<T>::_byte_type;
        U* z = reinterpret_cast<U*>(x.base) + x._stride * y;
        return stride_iterator<T>(reinterpret_cast<T*>(z), x._stride);
    }
    
    template<typename T>
    stride_iterator<T> operator+(ptrdiff_t x, stride_iterator<T> y) {
        using U = typename stride_iterator<T>::_byte_type;
        U* z = x * y._stride + reinterpret_cast<U*>(y.base);
        return stride_iterator<T>(reinterpret_cast<T*>(z), y._stride);
    }
    
    template<typename T>
    stride_iterator<T> operator-(stride_iterator<T> x, ptrdiff_t y) {
        using U = typename stride_iterator<T>::_byte_type;
        U* z = reinterpret_cast<U*>(x.base) - x._stride * y;
        return stride_iterator<T>(reinterpret_cast<T*>(z), x._stride);
    }
    
    template<typename T, typename U>
    ptrdiff_t operator-(stride_iterator<T> x, stride_iterator<U> y) {
        assert(x._stride == y._stride);
        using V = const std::byte;
        V* z = reinterpret_cast<V*>(x.base);
        V* w = reinterpret_cast<V*>(y.base);
        assert(!(z - w) % x._stride);
        return (z - w) / x._stride;
    }
    
} // namespace wry

#endif /* stride_iterator_hpp */
