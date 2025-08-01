//
//  array.hpp
//  client
//
//  Created by Antony Searle on 25/6/2023.
//

#ifndef array_hpp
#define array_hpp

#include <array>
#include <iterator>

#include "algorithm.hpp"
#include "concepts.hpp"
#include "memory.hpp"
#include "stddef.hpp"
#include "utility.hpp"

#include "array_view.hpp"
#include "maybe.hpp"
#include "with_capacity.hpp"

namespace wry {
    
    // # Array
    //
    // A contiguous double-ended queue with amortized O(1) operations on both
    // ends, intended to be a general-purpose sequence storage type.

    template<Relocatable>
    struct ContiguousDeque;
    
    template<typename T>
    struct Rank<ContiguousDeque<T>> 
    : std::integral_constant<std::size_t, Rank<T>::value + 1> {};
        
    template<Relocatable T>
    struct ContiguousDeque {
                
        using size_type = size_type;
        using difference_type = difference_type;
        using value_type = T;
        using iterator = T*;
        using const_iterator = const T*;
        using reference = T&;
        using const_reference = const T&;
        using byte_type = byte;
        using const_byte_type = const byte;

        // this layout permits punning as array_views
        
        T* _allocation_begin;
        T* _begin;
        T* _end;
        T* _allocation_end;
                
        bool invariant() const {
            return ((_allocation_begin <= _begin)
                    && (_begin <= _end)
                    && (_end <= _allocation_end));
        }
        
        static T* _allocate_size_in_bytes(size_type count) {
            return static_cast<T*>(operator new(count));
        }
        
        static T* _allocate_size(size_type count) {
            return _allocate_size_in_bytes(count * sizeof(T));
        }
        
        static void _deallocate(T* allocation_begin) {
            operator delete(static_cast<void*>(allocation_begin));
        }
        
        void _destroy() noexcept {
            std::destroy(_begin, _end);
            _deallocate(_allocation_begin);
        }

        void _construct_with_capacity(size_type count) noexcept {
            _allocation_begin = _allocate_size(count);
            _begin = _allocation_begin;
            _end = _allocation_begin;
            _allocation_end = _allocation_begin + count;
        }
        
        ContiguousDeque() noexcept
        : _allocation_begin(nullptr)
        , _begin(nullptr)
        , _end(nullptr)
        , _allocation_end(nullptr) {
        }
        
        ContiguousDeque(const ContiguousDeque& other) 
        : ContiguousDeque(wry::with_capacity, other.size()) {
            _end = std::uninitialized_copy(other.begin(), other.end(), _end);
        }
        
        ContiguousDeque(ContiguousDeque&& other) noexcept
        : _allocation_begin(std::exchange(other._allocation_begin, nullptr))
        , _begin(std::exchange(other._begin, nullptr))
        , _end(std::exchange(other._end, nullptr))
        , _allocation_end(std::exchange(other._allocation_end, nullptr)) {
        }
        
        ~ContiguousDeque() {
            _destroy();
        }
        
        ContiguousDeque& operator=(const ContiguousDeque& other) {
            if (other.size() <= size()) {
                iterator last = std::copy(other.begin(), other.end(), _begin);
                std::destroy(last, _end);
                _end = last;
            } else if (other.size() <= capacity()) {
                const_iterator middle = other.begin() + size();
                _end = std::uninitialized_copy(middle, other.end(),
                                               std::copy(other.begin(), middle,
                                                         _begin));
            } else {
                _destroy();
                _construct_with_capacity(other.size());
                _end = std::uninitialized_copy(other.begin(), other.end(), begin());
            }
            return *this;
        }
        
        ContiguousDeque& operator=(ContiguousDeque&& other) {
            _destroy();
            _allocation_begin = std::exchange(other._allocation_begin, nullptr);
            _begin = std::exchange(other._begin, nullptr);
            _end = std::exchange(other._end, nullptr);
            _allocation_end = std::exchange(other._allocation_end, nullptr);
            return *this;
        }

        
        explicit ContiguousDeque(size_type count)
        : ContiguousDeque(wry::with_capacity_t{}, count) {
            _end = std::uninitialized_value_construct_n(_begin, count);
        }

        template<typename U>
        explicit ContiguousDeque(std::initializer_list<U> x)
        : ContiguousDeque(x.begin(), x.end()) {
        }

        explicit ContiguousDeque(auto&& other) : ContiguousDeque() {
            using std::begin;
            using std::end;
            assign(begin(other), end(other));
        }
        
        template<typename U>
        explicit ContiguousDeque(ContiguousDeque<U>&& other)
        : ContiguousDeque(std::make_move_iterator(other.begin()),
                std::make_move_iterator(other.end())) {
        }

        // todo: steal the repr for rvalue construction from arrays of
        // representation-interconvertible types such as {byte, char, char8_t},
        // {intN_t and uintN_t}, {T*, intptr_t, uintptr_t, size_t, difference_t}

        ContiguousDeque(wry::with_capacity_t, size_type count)
        : _allocation_begin(_allocate_size(count))
        , _begin(_allocation_begin)
        , _end(_begin)
        , _allocation_end(_allocation_begin + count) {
        }
        
        ContiguousDeque(auto first, auto last, std::input_iterator_tag)
        : ContiguousDeque() {
            std::copy(first, last, std::back_inserter(*this));
        }
        
        ContiguousDeque(auto first, auto last, std::random_access_iterator_tag)
        : ContiguousDeque(wry::with_capacity_t{}, std::distance(first, last)) {
            _end = std::uninitialized_copy(first, last, _end);
        }
        
        ContiguousDeque(auto first, decltype(first) last)
        : ContiguousDeque(first, last,
                typename std::iterator_traits<std::decay_t<decltype(first)>>::iterator_category{}) {
        }

        ContiguousDeque(size_type count, const value_type& value) noexcept
        : ContiguousDeque(wry::with_capacity, count) {
            _end = std::uninitialized_fill_n(_begin, count, value);
        }
                                
        ContiguousDeque(T* allocation_begin_,
              T* begin_,
              T* end_,
              T* allocation_end_) noexcept
        : _allocation_begin(allocation_begin_)
        , _begin(begin_)
        , _end(end_)
        , _allocation_end(allocation_end_) {
            assert(invariant());
        }
                
        ContiguousDeque& operator=(auto&& other) {
            if constexpr (wry::Rank<std::decay_t<decltype(other)>>::value == 0) {
                fill(std::forward<decltype(other)>(other));
            } else {
                using std::begin;
                using std::end;
                assign(begin(other), end(other));
            }
            return *this;
        }
        
        operator ContiguousView<T>&() {
            return reinterpret_cast<ContiguousView<T>&>(_begin);
        }

        operator const ContiguousView<const T>&() const {
            return reinterpret_cast<const ContiguousView<const T>&>(_begin);
        }
                
        iterator& begin() { return _begin; }
        const const_iterator& begin() const { return _begin; }

        iterator& end() { return _end; }
        const const_iterator& end() const { return _end; }

        const_iterator& cbegin() { return _begin; }
        const const_iterator& cbegin() const { return _begin; }
        
        const_iterator& cend() { return _end; }
        const const_iterator& cend() const { return _end; }

        /*
        bool operator==(const auto& other) const {
            using std::begin;
            using std::end;
            return std::equal(begin(), end(), begin(other), end(other));
        }
        */
        
        void swap(ContiguousDeque& other) {
            using std::swap;
            swap(_allocation_begin, other._allocation_begin);
            swap(_begin, other._begin);
            swap(_end, other._end);
            swap(_allocation_end, other._allocation_end);
        }

        size_type size() const { return _end - _begin; }
        size_type size_in_bytes() const { return size() * sizeof(T); }
        size_type stride_in_bytes() const { return sizeof(T); }

        size_type max_size() const {
            return std::numeric_limits<std::ptrdiff_t>::max();
        }

        bool empty() const { return _begin == _end; }
        
        /*
        auto operator<=>(const auto& other) const {
            using std::begin;
            using std::end;
            return std::lexicographical_compare_three_way(this->begin(), this->end(),
                                                     begin(other), end(other));
        }
         */
                
        template<typename... Args>
        iterator emplace(const_iterator pos, Args&&... args) {
            iterator pos2 = _insert_uninitialized_n(pos, 1);
            return std::construct_at(pos2, std::forward<Args>(args)...);
        }

        iterator insert(const_iterator pos, const T& value) {
            iterator pos2 = _insert_uninitialized_n(pos, 1);
            return std::construct_at(pos2, value);
        }
        
        iterator insert(const_iterator pos, T&& value) {
            iterator pos2 = _insert_uninitialized_n(pos, 1);
            return std::construct_at(pos2, std::move(value));
        }
        
        iterator insert(const_iterator pos, size_type count, const T& value) {
            iterator pos2 = _insert_uninitialized_n(pos, count);
            std::uninitialized_fill_n(pos2, count, value);
            return pos2;
        }
        
        template<typename InputIterator>
        iterator insert(const_iterator pos, InputIterator first, InputIterator last) {
            size_type n = size();
            difference_type i = pos - begin();
            for (; first != last; ++first)
                push_back(*first);
            iterator pos2 = begin() + i;
            std::rotate(pos2, begin() + n, end());
            return pos2;
        }
        
        iterator erase(const_iterator pos) {
            std::destroy_at(pos);
            return _erase_uninitialized_n(pos, 1);
        }
        
        iterator erase(const_iterator first, const_iterator last) {
            std::destroy(first, last);
            return _erase_uninitialized_n(first, last - first);
        }
        
        void clear() {
            std::destroy(_begin, _end);
            //difference_type n = _allocation_end - _allocation_begin;
            //_end = _begin = _allocation_begin + (n >> 1);
            _begin = _end;
        }
        
        ContiguousDeque& assign(auto first, decltype(first) last) {
            return _assign(first, last,
                           typename std::iterator_traits<std::decay_t<decltype(first)>>::iterator_category());
        }
        
        ContiguousDeque& assign(size_type count, const value_type& value) {
            if (count <= size()) {
                iterator pos = std::fill_n(begin(), count, value);
                std::destroy(pos, _end);
                _end = pos;
            } else if (count <= capacity()) {
                std::fill(begin(), end(), value);
                _end = std::uninitialized_fill_n(end(), count - size(), value);
            } else {
                _destroy();
                _construct_with_capacity(count);
                _end = std::uninitialized_fill_n(begin(), count, value);
            }
            return *this;
        }
        
        ContiguousDeque& fill(const value_type& value) {
            std::fill(begin(), end(), value);
            return *this;
        }
        
        // [[C++ named requirement]] SequenceContainer (optional)

        reference front() {
            precondition(!empty());
            return *_begin;
        }
        
        const_reference front() const {
            precondition(!empty());
            return *_begin;
        }
        
        reference back() {
            precondition(!empty());
            return *(_end - 1);
        }
        
        const_reference back() const {
            precondition(!empty());
            return *(_end - 1);
        }

        template<typename... Args>
        reference emplace_front(Args&&... args) {
            _reserve_front(1);
            _emplace_front(std::forward<Args>(args)...);
            return front();
        }
        
        template<typename... Args>
        reference emplace_back(Args&&... args) {
            _reserve_back(1);
            _emplace_back(std::forward<Args>(args)...);
            return back();
        }

        void push_front(const T& value) {
            _reserve_front(1);
            _emplace_front(value);
        }
        
        void push_front(T&& value) {
            _reserve_front(1);
            _emplace_front(std::move(value));
        }

        void push_back(const T& value) {
            _reserve_back(1);
            _emplace_back(value);
        }
        
        void push_back(T&& value) {
            _reserve_back(1);
            _emplace_back(std::move(value));
        }
        
        void pop_front() noexcept {
            precondition(!is_empty());
            std::destroy_at(_begin++);
        }
        
        void pop_back() noexcept {
            precondition(!is_empty());
            std::destroy_at(--_end);
        }

        void unsafe_unpop_front() {
            static_assert(std::is_trivially_destructible_v<T>);
            precondition(_allocation_begin != _begin);
            --_begin;
        }
        
        void unsafe_unpop_back() {
            static_assert(std::is_trivially_destructible_v<T>);
            precondition(_end != _allocation_end);
            ++_end;
        }
        
        reference operator[](size_type pos) {
            precondition(pos < size());
            return _begin[pos];
        }
        
        const_reference operator[](size_type pos) const {
            precondition(pos < size());
            return _begin[pos];
        }

        reference at(size_type pos) {
            if (!(pos < size()))
                throw std::out_of_range(__PRETTY_FUNCTION__);
            return _begin[pos];
        }
        
        const_reference at(size_type pos) const {
            if (!(pos < size()))
                throw std::out_of_range(__PRETTY_FUNCTION__);
            return _begin[pos];
        }
        
        // [[C++ named requirement]] ReversibleContainer

        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;
        
        reverse_iterator rbegin() { return reverse_iterator(end()); }
        const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
        
        reverse_iterator rend() { return reverse_iterator(begin()); }
        const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }
        
        const_reverse_iterator crbegin() const { return rbegin(); }
        
        const_reverse_iterator crend() const { return rend(); }
        
        // [[C++]] std::vector
        
        using pointer = T*;
        using const_pointer = const T*;

        pointer data() { return _begin; }
        const_pointer data() const { return _begin; }
        
        void reserve(size_type count) {
            if (count > capacity())
                _reserve_back(count - size());
        }

        size_type capacity() const {
            return _allocation_end - _begin;
        }
                
        void shrink_to_fit() const {
            ContiguousDeque(*this).swap(*this);
        }
                        
        void resize(size_t count) {
            iterator pos = _resize(count);
            std::uninitialized_value_construct(pos, _end);
        }
        
        void resize(size_t count, T value) {
            iterator pos = _resize(count);
            std::uninitialized_fill(pos, _end, value);
        }
                
        // Nullable
        
        explicit operator bool() const {
            return _begin != _end;
        }
        
        bool operator!() const {
            return _begin == _end;
        }
        
        // [[Rust]] std::collections::Vec
        
        static ContiguousDeque with_capacity(size_type count) {
            T* p = static_cast<T*>(::operator new(count * sizeof(T)));
            return ContiguousDeque(p, p, p, p + count);
        }
        
        static ContiguousDeque from_raw_parts(T* allocation_begin_,
                                    T* begin_,
                                    T* end_,
                                    T* allocation_end_) {
            return ContiguousDeque(allocation_begin_, begin_, end_, allocation_end_);
        }
        
        std::tuple<T*, T*, T*, T*> into_raw_parts() && {
            return {
                std::exchange(_allocation_begin, nullptr),
                std::exchange(_begin, nullptr),
                std::exchange(_end, nullptr),
                std::exchange(_allocation_end, nullptr),
            };
        }
        
        void reserve_exact(size_type additional) {
            if (_allocation_end - _end < additional) {
                size_type n = size();
                size_type m = n + additional;
                T* p = static_cast<T*>(::operator new(m * sizeof(T)));
                relocate(_begin, _end, p);
                ::operator delete(_allocation_begin);
                _allocation_begin = p;
                _begin = p;
                _end = p + n;
                _allocation_end = p + m;
            }
        }
        
        void unsafe_set_size(size_type count) {
            precondition(count < capacity());
            _end = _begin + count;
        }
        
        T swap_remove(size_type index) {
            T value = std::exchange(operator[](index), std::move(back()));
            pop_back();
            return value; // NRVO
        }
        
        bool is_empty() const {
            return _begin == _end;
        }
        
        template<typename F>
        void resize_with(size_type count, F&& f) {
            iterator pos = _resize(count);
            for (; pos != end(); ++pos)
                new ((void*) pos) T(f());
        }

        
        // Extensions
        
        iterator to(difference_type pos) {
            precondition(((_allocation_begin - _begin) <= pos)
                   && (pos <= (_allocation_end - _begin)));
            return _begin + pos;
        }

        const_iterator to(difference_type pos) const {
            precondition(((_allocation_begin - _begin) <= pos)
                   && (pos <= (_allocation_end - _begin)));
            return _begin + pos;
        }
        
        const_iterator cto(difference_type pos) const {
            return to(pos);
        }
                
        // Implementation
            
        template<typename InputIt>
        ContiguousDeque& _assign(InputIt first, InputIt last, std::input_iterator_tag) {
            T* pos = _begin;
            for (;; ++pos, ++first) {
                if (first == last) {
                    std::destroy(pos, _end);
                    _end = pos;
                    return *this;
                }
                if (pos == _end) {
                    do {
                        push_back(*first);
                        ++first;
                    } while (first != last);
                    return *this;
                }
                *pos = *first;
            }
        }
        
        template<typename InputIt>
        ContiguousDeque& _assign(InputIt first, InputIt last, std::random_access_iterator_tag) {
            size_type count = std::distance(first, last);
            if (count <= size()) {
                iterator end2 = std::copy(first, last, begin());
                std::destroy(end2, end());
                _end = end2;
            } else if (count <= capacity()) {
                InputIt pos = first + size();
                std::copy(first, pos, begin());
                _end = std::uninitialized_copy(pos, last, end());
            } else {
                _destroy();
                _construct_with_capacity(count);
                _end = std::uninitialized_copy(first, last, begin());
            }
            return *this;
        }
        
        T* _insert_uninitialized_n(const T* pos, size_type count) {
            size_type h = _begin - _allocation_begin;
            size_type i = pos - _begin;
            size_type j = _end - pos;
            size_type k = _allocation_end - _end;
            if ((j <= i) && (k >= count)) {
                // relocate_backward_n(j, _end, _end + count);
                std::memmove(_end - j + count, _end - j, j * sizeof(T));
                _end += count;
            } else if ((i <= j) && (h >= count)) {
                // relocate_n(_begin, i, _begin - count);
                std::memmove(_begin - count, _begin, i * sizeof(T));
                _begin -= count;
            } else {
                size_type n = _end - _begin;
                size_type m = 3 * n + count;
                T* a = static_cast<T*>(::operator new(m * sizeof(T)));
                T* d = a + m;
                T* b = a + ((m - n - count) >> 1);
                T* c = b + n + count;
                // relocate_n(_begin, i, b);
                std::memcpy(b, _begin, i * sizeof(T));
                // relocate_backward_n(j, _end, c);
                std::memcpy(c - j, _end - j, j * sizeof(T));
                ::operator delete(static_cast<void*>(_allocation_begin));
                _allocation_begin = a;
                _begin = b;
                _end = c;
                _allocation_end = d;
            }
            return _begin + i;
        }
                        
        iterator _erase_uninitialized_n(const_iterator pos, size_type count) {
            size_type i = pos - _begin;
            size_type j = _end - (pos + count);
            if (i <= j) {
                relocate_backward(_begin, _begin + i, _begin + i + count);
                _begin += count;
            } else {
                relocate(_begin + i + count, _end, _begin + i);
                _end -= count;
            }
            return _begin + i;
        }
        
        T* erase_n(const T* first, size_type count) {
            std::destroy_n(first, count);
            return _erase_uninitialized_n(first, count);
        }
        
        iterator _resize(size_t count) {
            if (count > size()) {
                size_type n = count - size();
                return _insert_uninitialized_n(_end, n);
            } else {
                iterator end2 = _begin + count;
                std::destroy(end2, _end);
                _end = end2;
                return _end;
            }
        }
                        
        void _reserve_back(size_type count) {
            if (count > capacity_back()) {
                size_type n = _end - _begin;
                size_type m = 3 * n + count;
                T* a = static_cast<T*>(::operator new(m * sizeof(T)));
                T* d = a + m;
                T* b = a + ((m - n - count) >> 1);
                T* c = b + n;
                std::memcpy(b, _begin, n * sizeof(T));
                ::operator delete(static_cast<void*>(_allocation_begin));
                _allocation_begin = a;
                _begin = b;
                _end = c;
                _allocation_end = d;
            }
            assert(count <= capacity_back());
        }
        
        void _reserve_front(size_type count) {
            if (count > capacity_front()) {
                size_type n = _end - _begin;
                size_type m = 3 * n + count;
                T* a = static_cast<T*>(::operator new(m * sizeof(T)));
                T* d = a + m;
                T* b = a + ((m - n + count) >> 1);
                T* c = b + n;
                std::memcpy(b, _begin, n * sizeof(T));
                ::operator delete(static_cast<void*>(_allocation_begin));
                _allocation_begin = a;
                _begin = b;
                _end = c;
                _allocation_end = d;
            }
            assert(count <= capacity_front());
        }
                        
                
        template<typename... Args>
        void _emplace_front(Args&&... args) {
            precondition(_allocation_begin < _begin);
            std::construct_at(_begin - 1, std::forward<Args>(args)...);
            --_begin;
        }
        
        template<typename... Args>
        void _emplace_back(Args&&... args) {
            precondition(_end < _allocation_end);
            std::construct_at(_end, std::forward<Args>(args)...);
            ++_end;
        }
        
        size_t capacity_back() const {
            return _allocation_end - _end;
        }
        
        size_t capacity_front() const {
            return _begin - _allocation_begin;
        }
                
        void _did_write_back(size_t n) {
            precondition(n <= _allocation_end - _end);
            _end += n;
        }
        
        void _did_read_front(size_t n) {
            precondition(n <= _end - _begin);
            _begin += n;
        }
                                
        // bulk memcpy interface
        
        [[nodiscard]] size_type can_write_back() const {
            return _allocation_end - _end;
        }
        
        T* may_write_back(size_type n) {
            _reserve_back(n);
            return _end;
        }
        
        [[nodiscard]] T* will_write_back(size_type n) {
            _reserve_back(n);
            return exchange(_end, _end + n);
        }
        
        void did_write_back(size_type n) {
            precondition(n <= _allocation_end - _end);
            _end += n;
        }
        
        size_type can_read_first() {
            return _end - _begin;
        }
        
        T* may_read_first(size_type n) {
            precondition(n <= _end - _begin);
            return _begin;
        }
        
        [[nodiscard]] T* will_read_first(size_type n) {
            precondition(n <= _end - _begin);
            return exchange(_begin, _begin + n);
        }
        
        void did_read_first(size_t n) {
            precondition(n <= _end - _begin);
            _begin += n;
        }
        
        size_type can_read_last() const {
            return size();
        }
        
        [[nodiscard]] const_pointer will_read_last(size_type n) {
            precondition(n <= size());
            return _end -= n;
        }
        
        void did_read_last(size_type n) {
            precondition(n <= size());
            return _end -= n;
        }
        
        size_type can_write_front(size_type n) const {
            return _begin - _allocation_begin;
        }
        
        pointer may_write_front(size_type n) {
            _reserve_front(n);
            return _begin - n;
        }
        
        [[nodiscard]] pointer will_write_front(size_type n) {
            _reserve_front(n);
            return _begin -= n;
        }
        
        void did_write_front(size_type n) {
            precondition(n <= (_begin - _allocation_begin));
            _begin -= n;
        }
        
        //
        
        // TODO: use insert when std::distance is available
                
        template<typename I, typename J>
        void append(I first, J last) {
            for (; first != last; ++first) {
                push_back(*first);
            }
        }
        
        template<typename X>
        void append(const X& x) {
            append(std::begin(x), std::end(x));
        }

        void append(ContiguousDeque&& x) {
            if (empty()) {
                *this = std::move(x);
            } else if ((capacity_back() >= x.size())
                       && ((x.size() <= size())
                           || (x.capacity_front() < size()))) {
                // we can relocate x to free space at the back of this
                // and it's the better or only option
                relocate(x._begin, x._end, _end);
                _end += x.size();
                x._begin = x._end;
            } else if (size() <= x.capacity_front()) {
                // we can relocate this to free space at the front of x
                x._begin -= size();
                relocate(_begin, _end, x._begin);
                _begin = _end;
                swap(x);
            } else {
                ContiguousDeque y(wry::with_capacity, size() + x.size());
                assert(y.capacity_back() >= size());
                relocate(_begin, _end, y._end);
                y._end += size();
                _begin = _end;
                assert(y.capacity_back() >= x.size());
                relocate(x._begin, x._end, y._end);
                y._end += x.size();
                x._begin = x._end;
                swap(y);
            }
        }

        //
        
        ContiguousView<T>& as_view() {
            return reinterpret_cast<ContiguousView<T>&>(_begin);
        }

        const ContiguousView<const T>& as_view() const {
            return reinterpret_cast<const ContiguousView<const T>&>(_begin);
        }

        ContiguousView<const T>& as_const_view() {
            return reinterpret_cast<ContiguousView<const T>&>(_begin);
        }

        const ContiguousView<const T>& as_const_view() const {
            return reinterpret_cast<const ContiguousView<const T>&>(_begin);
        }

        ContiguousView<const byte> as_bytes() {
            return ContiguousView<const byte>(reinterpret_cast<const byte*>(_begin),
                                                   reinterpret_cast<const byte*>(_end));
        }

        ContiguousView<T> sub(difference_type i, size_type n) {
            assert(0 <= i);
            assert(i + n <= size());
            return ContiguousView(_begin + i, n);
        }

        ContiguousView<const T> sub(difference_type i, size_type n) const {
            assert(0 <= i);
            assert(i + n <= size());
            return ContiguousView(_begin + i, n);
        }

        ContiguousView<const T> csub(difference_type i, size_type n) const {
            assert(0 <= i);
            assert(i + n <= size());
            return ContiguousView(_begin + i, n);
        }

        // Array structs for all T share a compatible layout of four pointers,
        // so we can pun the entire Array into an Array of a different type,
        // and even mutate it in that form.  This relies on common
        // implementation-defined behavior:
        //   - type punning
        //   - pointers are addresses
        //   - ?
        // When mutating the reinterpreted Array, we must leave it in a state
        // that respects the size and alignment of the original Array.
        //
        // Similarly, the three regions of the Array (left, middle, and right)
        // can themselves be punned to mutable array_views.  A common use
        // case for this might be to supply the right region as a buffer
        // for bulk writes, with the writer moving up the view's _begin
        // which aliases the array's _end, and thus directly leaving the Array
        // in the right state.
        //
        // Compare with Rust `Vec::spare_capacity_mut` and `set_len`.
        
        template<typename U>
        ContiguousDeque<U>& reinterpret_as() {
            return reinterpret_cast<ContiguousDeque<U>&>(*this);
        }

        template<typename U>
        const ContiguousDeque<U>& reinterpret_as() const {
            return reinterpret_cast<const ContiguousDeque<U>&>(*this);
        }

        ContiguousDeque<byte>& bytes() {
            return reinterpret_cast<ContiguousDeque<byte>&>(*this);
        }

        const ContiguousDeque<byte>& bytes() const {
            return reinterpret_cast<const ContiguousDeque<byte>&>(*this);
        }
                
        template<typename U = Maybe<T>>
        ContiguousView<U>& reinterpret_left_as() {
            return reinterpret_cast<ContiguousView<U>&>(_allocation_begin);
        }
        
        template<typename U = T>
        ContiguousView<U>& reinterpret_middle_as() {
            return reinterpret_cast<ContiguousView<U>&>(_begin);
        }

        template<typename U = Maybe<T>>
        ContiguousView<U>& reinterpret_right_as() {
            return reinterpret_cast<ContiguousView<U>&>(_end);
        }
        
        
        // remove-erase idiom and friends
        
        size_type erase(const T& value) {
            iterator last = std::remove(_begin, _end, value);
            size_type n = _end - last;
            std::destroy(last, _end);
            _end = last;
            return n;
        }

        size_type erase_if(auto&& predicate) {
            iterator last = std::remove_if(_begin, _end, std::forward<decltype(predicate)>(predicate));
            size_type n = _end - last;
            std::destroy(last, _end);
            _end = last;
            return n;
        }
        
        iterator erase_first(const T& value) {
            iterator i = std::find(_begin, _end, value);
            return this->erase(i);
        }
        
        iterator erase_first_if(auto&& predicate) {
            iterator i = std::find_if(_begin, _end, std::forward<decltype(predicate)>(predicate));
            return this->erase(i);
        }
        
        size_type count(const T& value) const {
            return std::count(_begin, _end, value);
        }

        size_type count_if(auto&& predicate) const {
            return std::count(_begin, _end, std::forward<decltype(predicate)>(predicate));
        }
        
        bool contains(const T& value) const {
            return std::find(_begin, _end, value) != _end;
        }

        bool contains_if(auto&& predicate) const {
            return std::find_if(_begin, _end,  std::forward<decltype(predicate)>(predicate)) != _end;
        }

    }; // struct ContiguousDeque<T>
    
    template<typename T>
    void swap(ContiguousDeque<T>& a, ContiguousDeque<T>& b) {
        a.swap(b);
    }
    
} // namespace wry

#endif /* array_hpp */
