//
//  contiguous_view.hpp
//  client
//
//  Created by Antony Searle on 9/9/2023.
//

#ifndef contiguous_view_hpp
#define contiguous_view_hpp

#include <iterator>
#include <stdexcept>

#include "algorithm.hpp"
#include "utility.hpp"

namespace wry {

    // ContiguousView into a contguous sequence
    //
    // compare span, slice, range
    //
    // ContiguousView models a reference; assignment assigns to the elements, not
    // the bounds.  ContiguousView<const T>::operator= is an error.  use ::reset to
    // change what the ContiguousView points into
    
    // ContiguousView is always a contiguous true pointer; compare vector_view which
    // may not be (as when iterating over a stride_ptr)
        
    template<typename T>
    struct ContiguousView;
    
    template<typename T>
    struct Rank<ContiguousView<T>> 
    : std::integral_constant<std::size_t, Rank<T>::value + 1> {};
    
    template<typename T>
    struct ContiguousView {
        
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
        using const_byte_type = std::add_const_t<byte>;

        pointer _begin;
        pointer _end;
        
        bool invariant() const {
            return _begin <= _end;
        }
                                
        ContiguousView() = default;
        
        // reference semantics: copy is a shallow
        
        ContiguousView(const ContiguousView&) = default;
        ContiguousView(ContiguousView&&) = default;
                
        ~ContiguousView() = default;
        
        // reference semantics: assignment is deep
        
        ContiguousView& operator=(const ContiguousView& other) {
            copy(other._begin, other._end, _begin, _end);
            return *this;
        }
        
        ContiguousView& operator=(ContiguousView&& other) {
            copy(other._begin, other._end, _begin, _end);
            return *this;
        }
        
        // converting constructors
        
        explicit ContiguousView(auto&& other)
        : _begin(std::begin(other))
        , _end(std::end(other)) {
            assert(invariant());
        }
        
        // sequence constructors
        
        ContiguousView(iterator first, iterator last)
        : _begin(first)
        , _end(last) {
            assert(invariant());
        }
        
        ContiguousView(pointer first, size_type count)
        : _begin(first)
        , _end(first + count) {
        }
        
        const ContiguousView& assign(auto&& first, auto&& last) const {
            copy(FORWARD(first),
                 FORWARD(last),
                 _begin,
                 _end);
            return *this;
        }
        
        const ContiguousView& fill(const auto& value) const {
            std::fill(_begin, _end, value);
            return *this;
        }

        const ContiguousView& operator=(auto&& other) const {
            if constexpr (wry::Rank<std::decay_t<decltype(other)>>::value == 0) {
                fill(FORWARD(other));
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
        
        ContiguousView subview(size_type i, size_type n) const {
            precondition((i + n) <= size());
            return ContiguousView(_begin + i, n);
        }

        // mutate the ContiguousView itself
        
        ContiguousView& reset() {
            _begin = nullptr;
            _end = nullptr;
            return *this;
        }
        
        ContiguousView& reset(auto&& other) {
            using std::begin;
            using std::end;
            _begin = begin(other);
            _end = end(other);
            return *this;
        }
        
        ContiguousView& reset(pointer first, size_type count) {
            _begin = first;
            _end = first + count;
            return *this;
        }
        
        ContiguousView& reset(iterator first, iterator last) {
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
        const ContiguousView<U>& reinterpret_as() const {
            return reinterpret_cast<const ContiguousView<U>&>(*this);
        }
        
        template<typename U>
        ContiguousView<U>& reinterpret_as() {
            return reinterpret_cast<ContiguousView<U>&>(*this);
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
            return std::lexicographical_compare_three_way(_begin, _end, begin(other), end(other));
        }
        
        bool operator==(auto&& other) const {
            using std::begin;
            using std::end;
            return std::equal(_begin, _end, begin(other), end(other));
        }
        
#define X(Y) \
        ContiguousView& operator Y (auto&& other) {\
            if constexpr (wry::Rank<std::decay_t<decltype(other)>>::value == 0) {\
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
                assert(first2 == last2);\
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
        
    }; // struct ContiguousView<T>
    
    template<typename T>
    void swap(const ContiguousView<T>& x, const ContiguousView<T>& y) {
        x.swap(y);
    }
        
}

#endif /* contiguous_view_hpp */
