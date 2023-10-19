//
//  array_view.hpp
//  client
//
//  Created by Antony Searle on 9/9/2023.
//

#ifndef array_view_hpp
#define array_view_hpp

#include <iterator>

#include "algorithm.hpp"
#include "utility.hpp"

namespace wry {

    // array_view into a contguous sequence
    //
    // compare span, slice, range
    //
    // array_view models a reference; assignment assigns to the elements, not
    // the bounds.  array_view<const T>::operator= is an error.  use ::reset to
    // change what the array_view points into
    
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
        using const_byte_type = const byte;

        pointer _begin;
        pointer _end;
        
        bool invariant() const {
            return _begin <= _end;
        }
                                
        array_view() = default;
        
        array_view(const array_view&) = default;
        array_view(array_view&&) = default;
                
        array_view(pointer first, pointer last)
        : _begin(first)
        , _end(last) {
            assert(invariant());
        }
        
        array_view(pointer first, size_type count)
        : _begin(first)
        , _end(first + count) {
        }
        
        template<typename C>
        explicit array_view(C& other)
        : _begin(std::begin(other))
        , _end(std::end(other)) {
        }
        
        ~array_view() = default;
        
        array_view& operator=(const array_view& other) {
            assert(size() == other.size());
            std::copy(other._begin, other._end, _begin);
            return *this;
        }
        
        array_view& operator=(array_view&& other) {
            assert(size() == other.size());
            std::copy(other._begin, other._end, _begin);
            return *this;
        }

        array_view& operator=(auto&& other) {
            if constexpr (wry::rank<decltype(other)>::value == 0) {
                for (auto& x : *this)
                    x = other;
            } else {
                wry::copy(std::begin(other),
                          std::end(other),
                          _begin,
                          _end);
            }
            return *this;
        }

        template<typename I>
        array_view& assign(I first, I last) {
            wry::copy(first, last, begin(), end());
            return *this;
        }

        template<typename V>
        void swap(V&& other) const {
            wry::swap_ranges(std::begin(other), std::end(other), begin(), end());
        }
        
        // iteration
        
        iterator begin() const {
            return _begin;
        }
        
        iterator end() const {
            return _end;
        }
        
        const_iterator cbegin() const {
            return _begin;
        }
        
        const_iterator cend() const {
            return _end;
        }
        
        // accessors
                
        reference front() const {
            assert(!empty());
            return _begin[0];
        }
        
        reference back() const {
            assert(!empty());
            return _end[-1];
        }
        
        reference at(size_type i) const {
            if ((i < 0) || (i >= size()))
                throw std::range_error(__PRETTY_FUNCTION__);
            return _begin[i];
        }
        
        reference operator[](size_type i) const {
            assert((0 <= i) && (i < size()));
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
            return _end == _begin;
        }

        size_type size() const {
            return _end - _begin;
        }
        
        size_type size_in_bytes() const {
            return (_end - _begin) * sizeof(T);
        }
        
        constexpr size_type stride_in_bytes() const {
            return sizeof(T);
        }
        
        // subviews
        
        array_view subview(size_type i, size_type n) const {
            assert((i + n) <= size());
            return array_view(_begin + i, n);
        }
        
        array_view<byte_type> as_bytes() const {
            return array_view<byte_type>(reinterpret_cast<byte_type*>(_begin),
                                         reinterpret_cast<byte_type*>(_end));
        }

        // mutate the array_view itself
        
        array_view& reset() {
            _begin = nullptr;
            _end = nullptr;
            return *this;
        }
        
        array_view& reset(auto&& other) {
            _begin = std::begin(other);
            _end = std::end(other);
            return *this;
        }
        
        array_view& reset(auto* p, size_t n) {
            _begin = p;
            _end = p + n;
            return *this;
        }
        
        array_view& reset(auto* first, auto* last) {
            _begin = first;
            _end = last;
            return *this;
        }
                                
        void pop_front() {
            assert(!empty());
            ++_begin;
        }
        
        void pop_back() {
            assert(!empty());
            --_end;
        }
        
        void unsafe_unpop_front() {
            --_begin;
        }

        void unsafe_unpop_back() {
            --_end;
        }

        
        // byte-oriented interface
        
        // if we structure array and array_view appropriately, we can actually
        // pun an array_view onto an array [_begin, _end) or [_end, _capacity)
        // and directly manipulate _begin and _end
        
        // read from the front and move up _begin
        
        // how many can we read
        
        size_type can_read() const {
            return size();
        }
        
        size_type can_read_first() const {
            return size();
        }
        
        // before reading an unspecified number
        const_pointer may_read_first() const {
            return _begin;
        }
        
        // before reading up to a specified number
        const_pointer may_read_first(size_t n) const {
            assert(n <= size());
            return _begin;
        }
        
        // the amount we actually did read
        void did_read_first(size_t n) {
            assert(n <= size());
            _begin += n;
        }
        
        // commit to reading exactly a specified number
        const_pointer will_read_first(size_type n) {
            assert(n <= size());
            T* a = _begin;
            _begin += n;
            return a;
        }
                
        // read from the back and decrease the size
        //
        // because most bulk memory operations operate forwards, we can't
        // permit a short operation here like we can on the front
        
        size_type can_read_last() const {
            return size();
        }
        
        const_pointer will_read_last(size_type n) {
            assert(n <= size());
            return _end -= n;
        }
        
        void did_read_last(size_type n) {
            assert(n <= size());
            _end -= n;
        }
        
        
        // write to the front, over the existing data, and then move _begin
        // to the end of the write
        //
        // this makes most sense when the view is into uninitialized or
        // otherwise expendable data at the end of a larger sequence
        
        size_type can_overwrite_first() const {
            return size();
        }
        
        pointer may_overwrite_first() const {
            return _begin;
        }
        
        pointer may_overwrite_first(size_type n) const {
            assert(n <= size());
            return _begin;
        }
        
        pointer will_overwrite_first(size_type n) {
            assert(n <= size());
            T* ptr = _begin;
            _begin += n;
            return ptr;
        }
        
        void did_overwrite_first(size_type n) {
            assert(n <= size());
            _begin += n;
        }
        
        // write to the back, over the existing data, having moved _end
        // to the beginning of the write
        //
        // this makes most sense when the view is into uninitialized or
        // otherwise expendable data at the beginning of a larger sequence
        
        size_type can_overwrite_last() {
            return size();
        }
        
        pointer will_overwrite_last(size_type n) {
            assert(n <= size());
            _end -= n;
        }
        
        void did_overwrite_last(size_type n) {
            assert(n <= size());
            _end -= n;
        }

        template<typename U>
        array_view<U>& reinterpret_as() const {
            return reinterpret_cast<const array_view<U>&>(*this);
        }

        template<typename U>
        array_view<U>& reinterpret_as() {
            return reinterpret_cast<array_view<U>&>(*this);
        }
        
        const array_view<byte_type>& bytes() const {
            return reinterpret_as<byte_type>();
        }

        array_view<byte_type>& bytes(){
            return reinterpret_as<byte_type>();
        }
                
    }; // array_view
    
    template<typename T>
    void swap(const array_view<T>& x, const array_view<T>& y) {
        x.swap(y);
    }
        
}

#endif /* array_view_hpp */
