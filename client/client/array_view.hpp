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

    // array_view into a contguous sequence
    
    template<typename T>
    struct array_view;
    
    template<typename T>
    struct rank<array_view<T>> : std::integral_constant<std::size_t, rank<T>::value + 1> {};

    template<typename T>
    struct array_view {
        
        using size_type = size_t;
        using difference_type = ptrdiff_t;
        using element_type = T;
        using value_type = std::remove_cv<T>;
        using pointer = T*;
        using const_pointer = std::add_const_t<T>*;
        using iterator = T*;
        using const_iterator = std::add_const_t<T>*;
        using reference = T&;
        using const_reference = std::add_const_t<T>&;
        
        
        using byte_type = std::conditional_t<std::is_const_v<T>, const byte, byte>;
        
        pointer _begin;
        size_type _size;
        
        
        // construction
        
        array_view()
        : _begin(nullptr)
        , _size(0) {
        }
                
        explicit array_view(auto& other)
        : _begin(std::begin(other))
        , _size(std::size(other)) {
        }
        
        array_view(auto* p, size_type n)
        : _begin(p)
        , _size(n) {
        }
        
        array_view(auto* first, auto* last)
        : _begin(first)
        , _size(last - first) {
        }
                     
        array_view& operator=(auto&& other) {
            if constexpr (wry::rank<decltype(other)>::value == 0) {
                for (auto& x : *this)
                    x = other;
            } else {
                wry::copy(std::begin(other),
                          std::end(other),
                          begin(),
                          end());
            }
            return *this;
        }
        
        array_view& assign(auto first, auto last) {
            wry::copy(first, last, begin(), end());
        }
        
        void swap(auto&& other) const {
            wry::swap_ranges(std::begin(other), std::end(other), begin(), end());
        }
        
        // iteration
        
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
        
        // accessors
                
        reference front() const {
            assert(_size);
            return _begin;
        }
        
        reference back() const {
            assert(_size);
            return *(_begin + _size - 1);
        }
        
        reference at(size_type i) const {
            if ((i < 0) || (i >= _size))
                throw std::range_error(__PRETTY_FUNCTION__);
            return _begin[i];
        }
        
        reference operator[](size_type i) const {
            assert((0 <= i) && (i < _size));
            return _begin[i];
        }
        
        pointer to(difference_type i) const {
            return _begin + i;
        }
        
        pointer data() const {
            return _begin;
        }

        // observers
        
        bool empty() const {
            return !_size;
        }

        size_type size() const {
            return _size;
        }
        
        size_type size_bytes() const {
            return _size * sizeof(T);
        }
        
        constexpr size_type stride_bytes() const {
            return sizeof(T);
        }
        
        // subviews
        
        array_view subview(size_type i, size_type n) const {
            assert((i + n) <= _size);
            return array_view(_begin + i, n);
        }
        
        array_view<byte_type> as_bytes() const {
            return array_view<byte_type>(static_cast<byte_type*>(_begin), size_bytes());
        }
        
        
        // mutators
        
        array_view& reset() {
            _begin = nullptr;
            _size = 0;
        }
        
        array_view& reset(std::nullptr_t, size_t n) {
            _begin = nullptr;
            _size = n;
        }
        
        array_view& reset(auto&& other) {
            _begin = std::begin(other);
            _size = std::size(other);
        }
        
        array_view& reset(auto* p, size_t n) {
            _begin = p;
            _size = n;
        }
        
        array_view& reset(auto* first, auto* last) {
            _begin = first;
            _size = last - first;
        }
                                
        void pop_front(difference_type n = 1) {
            assert(n <= _size);
            _begin += n;
            _size -= n;
        }
        
        void pop_back(difference_type n = 1) {
            assert(n <= _size);
            _size -= n;
        }
        
    };
    
    template<typename T>
    void swap(const array_view<T>& x, const array_view<T>& y) {
        x.swap(y);
    }
    
    template<typename T, typename Serializer>
    void serialize(const array_view<T>& x, Serializer& s) {
        serialize(x.size(), s);
        for (auto&& y : x)
            serialize(y, s);
    }
    
    
}

#endif /* array_view_hpp */
