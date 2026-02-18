//
//  stride_iterator.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef stride_iterator_hpp
#define stride_iterator_hpp

#include <iterator>

#include "concepts.hpp"
#include "stddef.hpp"
#include "type_traits.hpp"

namespace wry {
    
    // Fancy pointer that strides over multiple elements, as in traversing
    // the non-contiguous dimensions of an n-dimensional array, or
    //
    // Stride is specified in bytes and must be a multiple of the alignment
    // of the type, but not necessarily of the size of the type
    //
    // Example: An RGB8 image with a width that is not divisible by four,
    // but whose rows are power-of-two aligned, will have a power of two
    // bytes per row and a fractional number of unused RGB8 slots at the end
    // of each row
    
    template<typename T>
    struct stride_iterator {
        
        using difference_type = difference_type;
        using value_type = std::remove_cv_t<T>;
        using pointer = T*;
        using reference = T&;
        using iterator_category = std::random_access_iterator_tag;
        
        using byte_type = copy_const_t<T, std::byte>;
        using void_type = copy_const_t<T, void*>;
        
        T* base;
        difference_type _stride_bytes;
        
        void _assert_invariant() const {
            assert(!(_stride_bytes % alignof(T)));
            assert((abs(_stride_bytes) >= sizeof(T)) || !base);
        }
        
        stride_iterator()
        : base(nullptr)
        , _stride_bytes(0) {
        }
        
        explicit stride_iterator(std::nullptr_t)
        : base(nullptr)
        , _stride_bytes(0) {
        }
        
        template<PointerConvertibleTo<T> U>
        stride_iterator(U* p, difference_type n)
        : base(p)
        , _stride_bytes(n) {
            _assert_invariant();
        }
        
        template<PointerConvertibleTo<T> U>
        stride_iterator(const stride_iterator<U>& other)
        : base(other.base)
        , _stride_bytes(other._stride_bytes) {
        }
        
        ~stride_iterator() = default;
        
        stride_iterator& operator=(std::nullptr_t) {
            base = nullptr;
            _stride_bytes = 0;
            return *this;
        }

        template<PointerConvertibleTo<T> U>
        stride_iterator& operator=(const stride_iterator<U>& other) {
            base = other.base;
            _stride_bytes = other._stride_bytes;
            return *this;
        }
        
        T* _succ() const {
            return reinterpret_cast<T*>(reinterpret_cast<byte_type*>(base) + _stride_bytes);
        }
        
        T* _pred() const {
            return reinterpret_cast<T*>(reinterpret_cast<byte_type*>(base) - _stride_bytes);
        }
        
        T* _plus(difference_type i) const {
            return reinterpret_cast<T*>(reinterpret_cast<byte_type*>(base) + _stride_bytes * i);
        }

        T* _minus(difference_type i) const {
            return reinterpret_cast<T*>(reinterpret_cast<byte_type*>(base) + _stride_bytes * i);
        }

        stride_iterator operator++(int) {
            stride_iterator result(*this);
            base = _succ();
            return result;
        }
        
        stride_iterator operator--(int) {
            stride_iterator result(*this);
            base = _pred();
            return result;
        }
        
        T& operator[](difference_type i) const {
            return *_plus(i);
        }
        
        T* operator->() const {
            return base;
        }
        
        stride_iterator& operator++() {
            base = _succ();
            return *this;
        }
        
        stride_iterator& operator--() {
            base = _pred();
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
        
        explicit operator void_type() const {
            return static_cast<void_type>(base);
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
            assert((_stride_bytes == other._stride_bytes) || !base || !other.base);
            return base == other.base;
        }
        
        stride_iterator& operator+=(difference_type i) {
            base = _plus(i);
            return *this;
        }
        
        stride_iterator& operator-=(difference_type i) {
            base = _minus(i);
            return *this;
        }
        
    };

    template<typename T>
    stride_iterator(T*, difference_type) -> stride_iterator<T>;
    
    template<typename T>
    stride_iterator<T> operator+(stride_iterator<T> x, difference_type y) {
        return stride_iterator<T>(x._plus(y), x._stride_bytes);
    }
    
    template<typename T>
    stride_iterator<T> operator+(difference_type x, stride_iterator<T> y) {
        return stride_iterator<T>(y._plus(x), y._stride_bytes);
    }
    
    template<typename T>
    stride_iterator<T> operator-(stride_iterator<T> x, difference_type y) {
        return stride_iterator<T>(x._minus(y), x._stride_bytes);
    }
    
    template<typename T, typename U>
    difference_type operator-(stride_iterator<T> x, stride_iterator<U> y) {
        assert(x._stride_bytes == y._stride_bytes);
        using V = const std::byte;
        V* z = reinterpret_cast<V*>(x.base);
        V* w = reinterpret_cast<V*>(y.base);
        assert(!((z - w) % x._stride_bytes));
        return (z - w) / x._stride_bytes;
    }
    
} // namespace wry

#endif /* stride_iterator_hpp */
