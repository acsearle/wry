//
//  vector_view.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef vector_view_hpp
#define vector_view_hpp

#include <iterator>

#include "algorithm.hpp"
#include "type_traits.hpp"
#include "utility.hpp"

namespace wry {
    
    template<typename T, typename I, typename C>
    struct vector_view;
    
    template<typename T, typename I, typename C>
    struct rank<vector_view<T, I, C>> : std::integral_constant<std::size_t, rank<T>::value + 1> {};


    // view a series of elements specified by an iterator, which may not be
    // contiguous
    
    template<typename T, typename Iterator = T*, typename ConstIterator = const T*>
    struct vector_view {
        
        using element_type = T;
        using value_type = std::remove_cv_t<T>;
        using size_type = size_t;
        using difference_type = ptrdiff_t;
        using reference = T&;
        using const_reference = std::add_const_t<T>&;
        using iterator = Iterator;
        using const_iterator = ConstIterator;
        
        iterator _begin;
        difference_type _size;
        
        // constructors
        
        vector_view()
        : _begin(nullptr)
        , _size(0) {
        }
        
        template<typename T2, typename I2, typename C2>
        vector_view(const vector_view<T2, I2, C2>& other)
        : _begin(other._begin)
        , _size(other._size) {
        }
        
        vector_view(iterator first, iterator last)
        : _begin(first)
        , _size(std::distance(first, last)) {
        }
        
        vector_view(iterator first, size_type count)
        : _begin(first)
        , _size(count) {
        }

        vector_view& operator=(const vector_view& other) {
            wry::copy(std::begin(other), std::end(other), begin(), end());
            return *this;
        }

        vector_view& operator=(vector_view&& other) {
            wry::copy(std::begin(other), std::end(other), begin(), end());
            return *this;
        }

        vector_view& operator=(auto&& other) {
            if constexpr (rank_v<std::decay_t<decltype(other)>>) {
                wry::copy(std::begin(other), std::end(other), begin(), end());
            } else {
                std::fill(begin(), end(), std::forward<decltype(other)>(other));
            }
            return *this;
        }
        
        void assign(auto first, auto last) {
            wry::copy(first, last, begin(), end());
        }
        
        void swap(auto&& other) {
            wry::swap_ranges(std::begin(other), std::end(other), begin(), end());
        }
        
        
        // observers
        
        size_type size() const {
            return _size;
        }
        
        bool empty() const {
            return !_size;
        }
        
        
        // iterators
        
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
        
        iterator to(size_type i) const {
            assert(i < _size);
            return _begin + i;
        }
        
        
        // accessors
        
        T& front() const { return *_begin; }
        T& back() const { return _begin[_size - 1]; }
        T& operator[](std::size_t i) const { return _begin[i]; }
        reference at(size_type i) const {
            if ((i < 0) || (i >= _size))
                throw std::range_error(__PRETTY_FUNCTION__);
            return _begin[i];
        }
        
        // subviews
        
        vector_view sub(size_type i, size_type n) const {
            assert((0 <= i) && (i + n < size()));
            return vector_view(_begin + i, n);
        }
        
        
        // reset
        
        vector_view& reset() {
            _begin = nullptr;
            _size = 0;
        }
        
        vector_view& reset(std::nullptr_t, size_type n) {
            _begin = nullptr;
            _size = n;
        }
        
        vector_view& reset(auto&& other) {
            _begin = std::begin(other);
            _size = std::size(other);
        }
        
        vector_view& reset(iterator p, size_type n) {
            _begin = p;
            _size = n;
        }
        
        vector_view& reset(iterator first, iterator last) {
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
        
        // compound assignment
        
        
#define WRY_COMPOUND_ASSIGNMENT( OP )\
        vector_view& operator OP (auto&& other) {\
            auto first1 = begin();\
            auto last1 = end();\
            if constexpr (std::rank<decltype(other)>::value == 0) {\
                for (; first1 != last1; ++first1) {\
                    *first1 OP other;\
                }\
            } else {\
                auto first2 = std::begin(other);\
                auto last2 = std::end(other);\
                for (; first1 != last1; ++first1, ++first2) {\
                    assert(first2 != last2);\
                    *first1 OP *first2;\
                }\
                assert(first2 == last2);\
            }\
            return *this;\
        }
        
        WRY_COMPOUND_ASSIGNMENT( += )
        WRY_COMPOUND_ASSIGNMENT( -= )
        WRY_COMPOUND_ASSIGNMENT( *= )
        WRY_COMPOUND_ASSIGNMENT( /= )
        WRY_COMPOUND_ASSIGNMENT( %= )
        WRY_COMPOUND_ASSIGNMENT( <<= )
        WRY_COMPOUND_ASSIGNMENT( >>= )
        WRY_COMPOUND_ASSIGNMENT( &= )
        WRY_COMPOUND_ASSIGNMENT( |= )
        WRY_COMPOUND_ASSIGNMENT( != )
        
#undef WRY_COMPOUND_ASSIGNMENT
        
    }; // struct vector_view<T>
    
    
    template<typename T, typename I, typename C>
    void swap(vector_view<T, I, C>& a, auto&& b) {
        a.swap(std::forward<decltype(b)>(b));
    }
    
    template<typename T>
    struct stride_iterator;
    
    template<typename T>
    using stride_view = vector_view<T, stride_iterator<T>, stride_iterator<std::add_const_t<T>>>;

    
    

} // namespace wry

#endif /* vector_view_hpp */

/*
 
 
 namespace wry {
 
 // Views contiguous objects.  Reference semantics, so assignment copies
 // the viewed elements

 
 // Undefined specialization prevents instantiation with const T
 template<typename T> struct const_vector_view<const T>;
 
 template<typename T, typename U>
 auto dot(const_vector_view<T> a, const_vector_view<U> b) {
 return std::accumulate(a.begin(), a.end(), b.begin(), decltype(std::declval<T>() * std::declval<U>())(0));
 }
 
 template<typename T>
 T sum(const_vector_view<T> a, T b = 0.0) {
 for (ptrdiff_t i = 0; i != a.size(); ++i)
 b += a(i);
 return b;
 }

 } // namespace manic

 
 */
