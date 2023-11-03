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
    
    // array_view is always a contiguous true pointer; compare vector_view which
    // may not be (as when iterating over a stride_ptr)
    
    template<typename T>
    struct array_view;
    
    template<typename T>
    using ArrayView = array_view<T>;
    
    template<typename T>
    struct rank<array_view<T>> : std::integral_constant<std::size_t, rank<T>::value + 1> {};

    template<typename T>
    struct array_view {
        
        using size_type = size_t;
        using difference_type = ptrdiff_t;
        
        using value_type = std::remove_cv_t<T>;
        using element_type = T;
        using pointer = T*;
        using const_pointer = std::add_const_t<T>*;
        using iterator = T*;
        using const_iterator = std::add_const_t<T>*;
        using reference = T&;
        using const_reference = std::add_const_t<T>&;
                
        using byte_type = copy_const_t<T, byte>;
        using const_byte_type = const byte;

        pointer _begin;
        pointer _end;
        
        bool invariant() const {
            return _begin <= _end;
        }
                                
        array_view() = default;
        
        // reference semantics: copy is a shallow
        
        array_view(const array_view&) = default;
        array_view(array_view&&) = default;
                
        ~array_view() = default;
        
        // reference semantics: assignment is deep
        
        array_view& operator=(const array_view& other) {
            copy(other._begin, other._end, _begin, _end);
            return *this;
        }
        
        array_view& operator=(array_view&& other) {
            copy(other._begin, other._end, _begin, _end);
            return *this;
        }
        
        // converting constructors
        
        explicit array_view(auto&& other)
        : _begin(std::begin(other))
        , _end(std::end(other)) {
            assert(invariant());
        }
        
        // sequence constructors
        
        array_view(iterator first, iterator last)
        : _begin(first)
        , _end(last) {
            assert(invariant());
        }
        
        array_view(pointer first, size_type count)
        : _begin(first)
        , _end(first + count) {
        }
        
        array_view& assign(auto&& first, auto&& last) const {
            copy(std::forward<decltype(first)>(first),
                 std::forward<decltype(last)>(last),
                 _begin,
                 _end);
            return *this;
        }
        
        array_view& fill(const auto& value) const {
            std::fill(_begin, _end, value);
            return *this;
        }

        array_view& operator=(auto&& other) const {
            if constexpr (wry::rank<std::decay_t<decltype(other)>>::value == 0) {
                fill(std::forward<decltype(other)>(other));
            } else {
                using std::begin;
                using std::end;
                assign(begin(other), end(other));
            }
            return *this;
        }

        void swap(auto&& other) const {
            using std::begin;
            using std::end;
            swap_ranges(begin(other), end(other), begin(), end());
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
            precondition(!empty());
            return *_begin;
        }
        
        reference back() const {
            precondition(!empty());
            return *(_end - 1);
        }
        
        reference at(size_type i) const {
            if ((i < 0) || (i >= size()))
                throw std::range_error(__PRETTY_FUNCTION__);
            return _begin[i];
        }
        
        reference operator[](size_type i) const {
            precondition((0 <= i) && (i < size()));
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
            precondition((i + n) <= size());
            return array_view(_begin + i, n);
        }

        // mutate the array_view itself
        
        array_view& reset() {
            _begin = nullptr;
            _end = nullptr;
            return *this;
        }
        
        array_view& reset(auto&& other) {
            using std::begin;
            using std::end;
            _begin = begin(other);
            _end = end(other);
            return *this;
        }
        
        array_view& reset(pointer first, size_type count) {
            _begin = first;
            _end = first + count;
            return *this;
        }
        
        array_view& reset(iterator first, iterator last) {
            _begin = first;
            _end = last;
            return *this;
        }
                                
        void pop_front() {
            precondition(!empty());
            ++_begin;
        }
        
        void pop_back() {
            precondition(!empty());
            --_end;
        }
        
        void unsafe_unpop_front() {
            precondition(_begin);
            --_begin;
        }

        void unsafe_unpop_back() {
            precondition(_end);
            --_end;
        }
        
        // unsafe alternative views
        
        template<typename U>
        const array_view<U>& reinterpret_as() const {
            return reinterpret_cast<const array_view<U>&>(*this);
        }
        
        template<typename U>
        array_view<U>& reinterpret_as() {
            return reinterpret_cast<array_view<U>&>(*this);
        }
                
        // bulk-copy interface
        
        // consume from the front
        
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
            precondition(n <= size());
            return _begin;
        }
        
        // the amount we actually did read
        void did_read_first(size_t n) {
            precondition(n <= size());
            _begin += n;
        }
        
        // commit to reading exactly a specified number
        [[nodiscard]] const_pointer will_read_first(size_type n) {
            precondition(n <= size());
            T* a = _begin;
            _begin += n;
            return a;
        }
                
        // read from the back and decrease the size
        
        size_type can_read_last() const {
            return size();
        }

        [[nodiscard]] const_pointer may_read_last(size_type n) const {
            precondition(n <= size());
            return _end - n;
        }

        [[nodiscard]] const_pointer will_read_last(size_type n) {
            precondition(n <= size());
            return _end -= n;
        }
        
        void did_read_last(size_type n) {
            precondition(n <= size());
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
        
        [[nodiscard]] pointer may_overwrite_first(size_type n) const {
            precondition(n <= size());
            return _begin;
        }
        
        [[nodiscard]] pointer will_overwrite_first(size_type n) {
            precondition(n <= size());
            T* ptr = _begin;
            _begin += n;
            return ptr;
        }
        
        void did_overwrite_first(size_type n) {
            precondition(n <= size());
            _begin += n;
        }
        
        // write to the back, over the existing data, having moved _end
        // to the beginning of the write
        //
        // this makes most sense when the view is into uninitialized or
        // otherwise expendable data at the beginning of a larger sequence
        
        size_type can_overwrite_last() const {
            return size();
        }

        [[nodiscard]] pointer may_overwrite_last(size_type n) const {
            precondition(n <= size());
            return _end - n;
        }

        [[nodiscard]] pointer will_overwrite_last(size_type n) {
            precondition(n <= size());
            return _end -= n;
        }
        
        void did_overwrite_last(size_type n) {
            precondition(n <= size());
            _end -= n;
        }
        
        // concise interface
        
        explicit operator bool() const {
            return _begin != _end;
        }
        
        bool operator!() const {
            return _begin == _end;
        }
        
        auto operator<=>(auto&& other) const {
            using std::begin;
            using std::end;
            return wry::lexicographical_compare_three_way(_begin, _end, begin(other), end(other));
        }
        
        bool operator==(auto&& other) const {
            using std::begin;
            using std::end;
            return std::equal(_begin, _end, begin(other), end(other));
        }
        
#define X(Y) \
        array_view& operator Y (auto&& other) {\
            if constexpr (wry::rank<std::decay_t<decltype(other)>>::value == 0) {\
                iterator first = _begin;\
                for (; first != _end; ++first)\
                    *first Y other;\
            } else {\
                using std::begin;\
                using std::end;\
                iterator first = _begin;\
                auto first2 = begin(other);\
                auto last2 = end(other);\
                for (; first != _end; ++first, ++first2) {\
                    assert(first2 != last2);\
                    *first += *first2;\
                }\
                postcondition(first2 == last2);\
            }\
            return *this;\
        }
        
        X(+=)
        X(-=)
        X(*=)
        X(/=)
        X(%=)
        X(<<=)
        X(>>=)
        X(&=)
        X(^=)
        X(|=)

#undef X
        
    }; // struct array_view<T>
    
    template<typename T>
    void swap(const array_view<T>& x, const array_view<T>& y) {
        x.swap(y);
    }
        
}

#endif /* array_view_hpp */
