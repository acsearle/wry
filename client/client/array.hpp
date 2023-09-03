//
//  array.hpp
//  client
//
//  Created by Antony Searle on 25/6/2023.
//

#ifndef array_hpp
#define array_hpp

#include <algorithm>
#include <iterator>

#include "utility.hpp"
#include "with_capacity.hpp"

namespace wry {
    
    // # Array
    //
    // A contiguous double-ended queue with amortized O(1) operations on the
    // ends, intended to be the general-purpose sequence storage type
    //
    // Benchmarking indicates performance is competitive with the better of
    // C++ vector and deque on various tasks
    //
    // + Amortized O(1) push_front and pop_front
    // + Amortized O(min(distance(begin, pos), distance(pos, end)) insert and erase
    // + Contiguous / pointer iterators
    // - Higher constant factor for memory overhead
    // - Higher constant factor for amortized O(1) operations (4 vs 3?)
    // - Larger stack footprint (4 pointers vs 3 for std::vector)
    // - Less iterator stability
    //
    // Array assumes that the stored type is Relocatable
    
    template<typename T>
    struct array {
                
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using iterator = T*;
        using const_iterator = const T*;
        using reference = T&;
        using const_reference = const T&;

        T* _begin;
        T* _end;
        T* _allocation_begin;
        T* _allocation_end;
                
        bool _invariant() const {
            return ((_allocation_begin <= _begin)
                    && (_begin <= _end)
                    && (_end <= _allocation_end)
                    && ((_allocation_begin == nullptr)
                        == (_allocation_end == nullptr)));
        }
                
        array(T* begin_,
              T* end_,
              T* allocation_begin_,
              T* allocation_end_) noexcept
        : _begin(begin_)
        , _end(end_)
        , _allocation_begin(allocation_begin_)
        , _allocation_end(allocation_end_) {
            assert(_invariant());
        }
        
        // [[C++ named requirement]] Container

        array() noexcept
        : _begin(nullptr)
        , _end(nullptr)
        , _allocation_begin(nullptr)
        , _allocation_end(nullptr) {
        }
        
        array(const array& other) noexcept
        : array(with_capacity_t{}, other.size()) {
            _end = std::uninitialized_copy(other.begin(),
                                           other.end(),
                                           _begin);
        }
        
        array(array&& other)
        : _begin(std::exchange(other._begin, nullptr))
        , _end(std::exchange(other._end, nullptr))
        , _allocation_begin(std::exchange(other._allocation_begin, nullptr))
        , _allocation_end(std::exchange(other._allocation_end, nullptr)) {
        }
                
        array& operator=(const array& other) {
            if (other.size() <= size()) {
                iterator end2 = std::copy(other.begin(), other.end(), begin());
                std::destroy(end2, end());
                _end = end2;
            } else if (other.size() <= capacity()) {
                const_iterator mid = other.begin() + size();
                std::copy(other.begin(), mid, begin());
                _end = std::uninitialized_copy(mid, other.end(), end());
            } else {
                _destruct();
                _construct_with_capacity(other.size());
                _end = std::uninitialized_copy(other.begin(), other.end(), begin());
            }
            return *this;
        }
        
        array& operator=(array&& other) {
            _destruct();
            _begin = std::exchange(other._begin, nullptr);
            _end = std::exchange(other._end, nullptr);
            _allocation_begin = std::exchange(other._allocation_begin, nullptr);
            _allocation_end = std::exchange(other._allocation_end, nullptr);
            return *this;
        }
        
        ~array() { _destruct(); }
        
        iterator begin() { return _begin; }
        const_iterator begin() const { return _begin; }

        iterator end() { return _end; }
        const_iterator end() const { return _end; }

        const_iterator cbegin() const { return _begin; }
        
        const_iterator cend() const { return _end; }

        bool operator==(const array& other) {
            return std::equal(begin(), end(), other.begin(), other.end());
        }
        
        void swap(array& other) {
            using std::swap;
            swap(_begin, other._begin);
            swap(_end, other._end);
            swap(_allocation_begin, other._allocation_begin);
            swap(_allocation_end, other._allocation_end);
        }

        size_type size() const { return _end - _begin; }
        
        size_type max_size() const {
            return std::numeric_limits<std::ptrdiff_t>::max();
        }

        bool empty() const { return _begin == _end; }
        
        auto operator<=>(const array& other) const {
            return lexicographical_compare_three_way(begin(), end(),
                                                     other.begin(), other.end());
        }
        
        // [[C++ named requirement]] SequenceContainer (core)
        
        explicit array(size_type count) noexcept
        : array(wry::with_capacity_t{}, count) {
            _end = std::uninitialized_value_construct_n(_begin, count);
        }

        array(size_type count, const value_type& value) noexcept
        : array(wry::with_capacity_t{}, count) {
            _end = std::uninitialized_fill_n(_begin, count, value);
        }

        template<typename InputIt>
        array(InputIt first, InputIt last)
        : array(first, last, typename std::iterator_traits<InputIt>::iterator_category{}) {
        }
        
        array(std::initializer_list<T> x)
        : array(x.begin(), x.end()) {
        }
        
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
            difference_type n = _allocation_end - _allocation_begin;
            _end = _begin = _allocation_begin + (n >> 1);
        }
        
        template<typename InputIt>
        void assign(InputIt first, InputIt last) {
            _assign(first, last, typename std::iterator_traits<InputIt>::iterator_category());
        }
        
        void assign(size_type count, const value_type& value) {
            if (count <= size()) {
                iterator pos = std::fill_n(begin(), count, value);
                std::destroy(pos, _end);
                _end = pos;
            } else if (count <= capacity()) {
                std::fill(begin(), end(), value);
                _end = std::uninitialized_fill_n(end(), count - size(), value);
            } else {
                _destruct();
                _construct_with_capacity(count);
                _end = std::uninitialized_fill_n(begin(), count, value);
            }
        }
        
        // [[C++ named requirement]] SequenceContainer (optional)

        reference front() {
            assert(!empty());
            return *_begin;
        }
        
        const_reference front() const {
            assert(!empty());
            return *_begin;
        }
        
        reference back() {
            assert(!empty());
            return _end[-1];
        }
        
        const_reference back() const {
            assert(!empty());
            return _end[-1];
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
            assert(!is_empty());
            std::destroy_at(_begin++);
        }
        
        void pop_back() noexcept {
            assert(!is_empty());
            std::destroy_at(--_end);
        }
        
        reference operator[](size_type pos) {
            assert(pos < size());
            return _begin[pos];
        }
        
        const_reference operator[](size_type pos) const {
            assert(pos < size());
            return _begin[pos];
        }

        reference at(size_type pos) {
            if (!(pos < size()))
                throw std::out_of_range("array::at");
            return _begin[pos];
        }
        
        const_reference at(size_type pos) const {
            if (!(pos < size()))
                throw std::out_of_range("array::at");
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
            array(*this).swap(*this);
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
        
        static array with_capacity(size_type count) {
            T* p = static_cast<T*>(::operator new(count * sizeof(T)));
            return array(p, p, p, p + count);
        }
        
        static array from_raw_parts(T* begin_,
                                    T* end_,
                                    T* allocation_begin_,
                                    T* allocation_end_) {
            return array(begin_, end_, allocation_begin_, allocation_end_);
        }
        
        std::tuple<T*, T*, T*, T*> into_raw_parts() && {
            return {
                std::exchange(_begin, nullptr),
                std::exchange(_end, nullptr),
                std::exchange(_allocation_begin, nullptr),
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
                _begin = p;
                _end = p + n;
                _allocation_begin = p;
                _allocation_end = p + m;
            }
        }
        
        void unsafe_set_size(size_type count) {
            assert(count < capacity());
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
        
        array(wry::with_capacity_t, size_type count)
        : _begin(static_cast<T*>(::operator new(count * sizeof(T))))
        , _end(_begin)
        , _allocation_begin(_begin)
        , _allocation_end(_begin + count) {
        }
                
        iterator to(difference_type pos) {
            assert(((_allocation_begin - _begin) <= pos)
                   && (pos <= (_allocation_end - _begin)));
            return _begin + pos;
        }

        const_iterator to(difference_type pos) const {
            assert(((_allocation_begin - _begin) <= pos)
                   && (pos <= (_allocation_end - _begin)));
            return _begin + pos;
        }
        
        const_iterator cto(difference_type pos) const {
            return to(pos);
        }
                
        // Implementation
        
        template<typename InputIt>
        array(InputIt first, InputIt last, std::input_iterator_tag)
        : array() {
            for (; first != last; ++first)
                push_back(*first);
        }

        template<typename InputIt>
        array(InputIt first, InputIt last, std::random_access_iterator_tag)
        : array(wry::with_capacity_t{}, std::distance(first, last)) {
            for (; first != last; ++first)
                push_back(*first);
        }
        
        void _destruct() noexcept {
            std::destroy(_begin, _end);
            operator delete((void*) _allocation_begin);
        }
        
        void _construct_with_capacity(size_type count) noexcept {
            _allocation_begin = static_cast<T*>(operator new(count * sizeof(T)));
            _allocation_end = _allocation_begin + count;
            _begin = _allocation_begin;
            _end = _allocation_begin;
        }

        template<typename InputIt>
        void _assign(InputIt first, InputIt last, std::input_iterator_tag) {
            T* pos = _begin;
            for (;; ++pos, ++first) {
                if (first == last) {
                    std::destroy(pos, _end);
                    _end = pos;
                    return;
                }
                if (pos == _end) {
                    do {
                        push_back(*first);
                        ++first;
                    } while (first != last);
                    return;
                }
                *pos = *first;
            }
        }
        
        template<typename InputIt>
        void _assign(InputIt first, InputIt last, std::random_access_iterator_tag) {
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
                _destruct();
                _construct_with_capacity(count);
                _end = std::uninitialized_copy(first, last, begin());
            }
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
                _begin = b;
                _end = c;
                _allocation_begin = a;
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
                _begin = b;
                _end = c;
                _allocation_begin = a;
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
                _begin = b;
                _end = c;
                _allocation_begin = a;
                _allocation_end = d;
            }
            assert(count <= capacity_front());
        }
                        
                
        template<typename... Args>
        void _emplace_front(Args&&... args) {
            assert(_allocation_begin < _begin);
            std::construct_at(_begin - 1, std::forward<Args>(args)...);
            --_begin;
        }
        
        template<typename... Args>
        void _emplace_back(Args&&... args) {
            assert(_end < _allocation_end);
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
            assert(n <= _allocation_end - _end);
            _end += n;
        }
        
        void _did_read_front(size_t n) {
            assert(n <= _end - _begin);
            _begin += n;
        }
                                
        // buffer interface
        
        size_t can_write_back() {
            return _allocation_end - _end;
        }
        
        T* may_write_back(size_t n) {
            _reserve_back(n);
            return _end;
        }
        
        T* will_write_back(size_t n) {
            _reserve_back(n);
            return exchange(_end, _end + n);
        }
        
        void did_write_back(size_t n) {
            assert(n <= _allocation_end - _end);
            _end += n;
        }
        
        size_t can_read_front() {
            return _end - _begin;
        }
        
        T* may_read_front(size_t n) {
            assert(n <= _end - _begin);
            return _begin;
        }
        
        T* will_read_front(size_t n) {
            assert(n <= _end - _begin);
            return exchange(_begin, _begin + n);
        }
        
        void did_read_front(size_t n) {
            assert(n <= _end - _begin);
            _begin += n;
        }
                
        template<typename I, typename J>
        void append(I first, J last) {
            for (; first != last; ++first) {
                push_back(*first);
            }
        }
        
    }; // struct array<T>
    
    template<typename T>
    void swap(array<T>& a, array<T>& b) {
        a.swap(b);
    }
    
} // namespace wry

#endif /* array_hpp */
