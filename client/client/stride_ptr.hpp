//
//  stride_ptr.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef stride_ptr_hpp
#define stride_ptr_hpp

#include <iterator>
#include "type_traits.hpp"

namespace wry {
    
    // Pointer that strides over an array, as when traversing a column of a
    // row-major matrix.
    //
    // It could potentially stride by multiples of alignof(T) rather than
    // sizeof(T).
    
    template<typename T>
    struct stride_ptr {
        
        using difference_type = ptrdiff_t;
        using value_type = std::remove_const_t<T>;
        using pointer = T*;
        using reference = T&;
        using iterator_category = std::random_access_iterator_tag;
        
        intptr_t _address;
        ptrdiff_t _stride;
        
        stride_ptr(T* p, ptrdiff_t bytes)
        : _address(reinterpret_cast<intptr_t>(p))
        , _stride(bytes) {
            assert(!(_address % alignof(T)));
            assert(!(_stride % alignof(T)));
            assert(std::abs(_stride) >= sizeof(T));
        }

        stride_ptr operator++(int) { stride_ptr a(*this); ++*this; return a; }
        stride_ptr operator--(int) { stride_ptr a(*this); --*this; return a; }
        T& operator[](ptrdiff_t i) const { return *reinterpret_cast<T*>(_address + _stride * i); }
        T* operator->() const { return reinterpret_cast<T*>(_address); }
        
        stride_ptr& operator++() { _address += _stride; return *this; }
        stride_ptr& operator--() { _address -= _stride; return *this; }
        T& operator*() const { return *reinterpret_cast<T*>(_address); }
        bool operator!() const { return !_address; }
        explicit operator bool() const { return static_cast<bool>(_address); }


        stride_ptr& operator+=(ptrdiff_t i) { _address += _stride * i; return *this; }
        
        stride_ptr& operator-=(ptrdiff_t i) { _address -= _stride * i; return *this; }
        
        auto operator<=>(const stride_ptr& other) {
            return (0 < _stride) ? (_address <=> other._address) : (other._address <=> _address);
        }
        
    };
    
    template<typename T>
    stride_ptr<T> operator+(stride_ptr<T> a, ptrdiff_t b) {
        return stride_ptr(a._ptr + a._stride * b, a._stride);
    }
    
    template<typename T>
    stride_ptr<T> operator+(ptrdiff_t a, stride_ptr<T> b) {
        return stride_ptr(b._ptr + a * b._stride, b._stride);
    }
    
    template<typename T>
    stride_ptr<T> operator-(stride_ptr<T> a, ptrdiff_t b) {
        return stride_ptr(a._ptr - a._stride * b, a._stride);
    }
    
    template<typename T>
    stride_ptr<T> operator-(ptrdiff_t a, stride_ptr<T> b) {
        return stride_ptr(b._ptr - a * b._stride, b._stride);
    }
    
    template<typename T, typename U>
    ptrdiff_t operator-(stride_ptr<T> a, stride_ptr<U> b) {
        assert(a._stride == b._stride);
        return (a._ptr - b._ptr) / a._stride;
    }
    
} // namespace wry

#endif /* stride_ptr_hpp */
