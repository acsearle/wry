//
//  stride_iterator.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef stride_iterator_hpp
#define stride_iterator_hpp

#include <iterator>
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
        
        using difference_type = std::ptrdiff_t;
        using value_type = std::remove_const_t<T>;
        using pointer = T*;
        using reference = T&;
        using iterator_category = std::random_access_iterator_tag;
        
        using C = std::conditional_t<std::is_const_v<T>, const std::byte, std::byte>;
        using V = std::conditional_t<std::is_const_v<T>, const void*, void*>;
        
        T* base;
        difference_type _stride;
        
        stride_iterator()
        : base(nullptr)
        , _stride(0) {
        }
        
        explicit stride_iterator(std::nullptr_t)
        : base(nullptr)
        , _stride(0) {
        }
        
        template<typename U, typename = std::enable_if_t<std::is_convertible_v<U, T*>>>
        stride_iterator(U ptr, ptrdiff_t stride)
        : base(ptr)
        , _stride(stride) {
            assert(!(_stride % alignof(T)));
            assert((std::abs(_stride) >= sizeof(T)) || !base);
        }
        
        template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        stride_iterator(const stride_iterator<U>& other)
        : base(other.base)
        , _stride(other._stride) {
        }

        template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        stride_iterator& operator=(const stride_iterator<U>& other) {
            base = other.base;
            _stride = other._stride;return *this;
        }
        
        stride_iterator& operator=(std::nullptr_t) {
            base = nullptr;
            _stride = 0;
            return *this;
        }

        stride_iterator operator++(int) {
            stride_iterator result(*this);
            C* p = reinterpret_cast<C*>(base);
            p += _stride;
            base = reinterpret_cast<T*>(p);
            return result;
        }
        
        stride_iterator operator--(int) {
            stride_iterator result(*this);
            C* p = reinterpret_cast<C*>(base);
            p -= _stride;
            base = reinterpret_cast<T*>(p);
            return result;
        }
        
        T& operator[](std::ptrdiff_t i) const {
            C* p = reinterpret_cast<C*>(base);
            p += _stride * i;
            T* q = reinterpret_cast<T*>(p);
            return *q;
        }
        
        T* operator->() const {
            return base;
        }
        
        stride_iterator& operator++() {
            C* p = reinterpret_cast<C*>(base);
            p += _stride;
            base = reinterpret_cast<T*>(p);
            return *this;
        }
        
        stride_iterator& operator--() {
            C* p = reinterpret_cast<C*>(base);
            p -= _stride;
            base = reinterpret_cast<T*>(p);
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
        
        explicit operator V() const {
            return reinterpret_cast<V>(base);
        }
        
        T& operator*() const {
            return *reinterpret_cast<T*>(base);
        }
        
        bool operator==(std::nullptr_t) const {
            return !base;
        }
        
        bool operator==(const stride_iterator& other) const {
            assert((_stride == other._stride)
                   || !base
                   || !other.base);
            return base == other.base;
        }
        
        stride_iterator& operator+=(std::ptrdiff_t i) {
            C* p = reinterpret_cast<C*>(base);
            p += _stride * i;
            base = reinterpret_cast<T*>(p);
            return *this;
        }
        
        stride_iterator& operator-=(std::ptrdiff_t i) {
            C* p = reinterpret_cast<C*>(base);
            p += _stride * i;
            base = reinterpret_cast<T*>(p);
            return *this;
        }
        
    };

    template<typename T>
    stride_iterator(T*, std::ptrdiff_t) -> stride_iterator<T>;
    
    template<typename T>
    stride_iterator<T> operator+(stride_iterator<T> a, std::ptrdiff_t b) {
        using C = std::conditional_t<std::is_const_v<T>, const std::byte, std::byte>;
        C* p = reinterpret_cast<C*>(a.base);
        p += a._stride * b;
        T* q = reinterpret_cast<T*>(p);
        return stride_iterator<T>(q, a._stride);
    }
    
    template<typename T>
    stride_iterator<T> operator+(std::ptrdiff_t a, stride_iterator<T> b) {
        using C = std::conditional_t<std::is_const_v<T>, const std::byte, std::byte>;
        C* p = reinterpret_cast<C*>(b.base);
        p += b._stride * a;
        T* q = reinterpret_cast<T*>(p);
        return stride_iterator<T>(q, b._stride);
    }
    
    template<typename T>
    stride_iterator<T> operator-(stride_iterator<T> a, std::ptrdiff_t b) {
        using C = std::conditional_t<std::is_const_v<T>, const std::byte, std::byte>;
        C* p = reinterpret_cast<C*>(a.base);
        p -= a._stride * b;
        T* q = reinterpret_cast<T*>(p);
        return stride_iterator<T>(q, a._stride);
    }
    
    template<typename T, typename U, typename = std::common_type_t<T*, U*>>
    ptrdiff_t operator-(stride_iterator<T> a, stride_iterator<U> b) {
        assert(a._stride == b._stride);
        using C = std::conditional_t<std::is_const_v<T>, const std::byte, std::byte>;
        C* p = reinterpret_cast<C*>(a.base);
        C* q = reinterpret_cast<C*>(b.base);
        assert(!(p - q) % a._stride);
        return (p - q) / a._stride;
    }
    
} // namespace wry

#endif /* stride_iterator_hpp */
