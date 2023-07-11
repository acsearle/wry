//
//  array.hpp
//  client
//
//  Created by Antony Searle on 25/6/2023.
//

#ifndef array_hpp
#define array_hpp

#include <algorithm>
#include "utility.hpp"

namespace wry {
    
    template<typename T>
    struct array_view {
        
        T* _b;
        T* _c;
        
    };
    
    // # Array
    //
    // A contiguous double-ended queue with amortized O(1) operations on the
    // ends
    //
    // + O(1) push_ and pop_front
    // + Contiguous layout and iterators are pointers
    // - Higher memory usage
    // - Higher constant factor
    // - No custom allocator
    
    template<typename T>
    struct array {
                
        using value_type = T;
        using size_type = std::ptrdiff_t;
        using difference_type = std::ptrdiff_t;
        using reference = T&;
        using const_reference = const T&;
        using pointer = T*;
        using const_pointer = const T*;
        using iterator = T*;
        using const_iterator = const T*;
         
        
        
        T* _a;
        T* _b;
        T* _c;
        T* _d;
        
        void _destruct() {
            std::destroy(_b, _c);
            deallocate(_a);
        }
        
        bool _invariant() const {
            return (_a <= _b) && (_b <= _c) && (_c <= _d) && ((_a == nullptr) == (_d == nullptr));
        }
        
        array(T* a, T* b, T* c, T* d)
        : _a(a)
        , _b(b)
        , _c(c)
        , _d(d) {
            assert(_invariant());
        }

        
        
        array()
        : _a(nullptr)
        , _b(nullptr)
        , _c(nullptr)
        , _d(nullptr) {
        }
        
        explicit array(size_type count) {
            size_type n = count;
            _a = allocate<T>(n);
            _d = _a + n;
            _b = _a + ((n - count) >> 1);
            _c = _b;
            _c = std::uninitialized_value_construct_n(_b, count);
        }

        array(size_type count, const value_type& value) {
            size_type n = count;
            _a = allocate<T>(n);
            _d = _a + n;
            _b = _a + ((n - count) >> 1);
            _c = _b;
            _c = std::uninitialized_fill_n(_b, count, value);
        }

        template<typename InputIterator>
        array(InputIterator first, InputIterator last);
        
        array(const array& other) {
            size_type n = other._c - other._b;
            size_type m = n;
            _a = m ? allocate<T>(m) : nullptr;
            _b = _a + ((m - n) >> 1);
            _c = _b;
            _d = _a + m;
            _c = std::uninitialized_copy(_c, other._b, other._c);
        }
        
        array(array&& other)
        : _a(exchange(other._a, nullptr))
        , _b(exchange(other._b, nullptr))
        , _c(exchange(other._c, nullptr))
        , _d(exchange(other._d, nullptr)) {
        }
                
        ~array() {
            _destruct();
        }
                        
        array& operator=(const array& other);
        
        array& operator=(array&& other) {
            _destruct();
            _a = exchange(other._a, nullptr);
            _b = exchange(other._b, nullptr);
            _c = exchange(other._c, nullptr);
            _d = exchange(other._d, nullptr);
            return *this;
        }
        
        template<typename InputIterator>
        void assign(InputIterator first, InputIterator last) {
            T* b = _b;
            for (;;) {
                if (b == _c) {
                    do {
                        push_back(*first);
                        ++first;
                    } while (first != last);
                    return;
                }
                if (first == last) {
                    std::destroy(b, _c);
                    _c = b;
                    return;
                }
                *b = *first;
                ++b;
                ++first;
            }
        }
        
        reference at(size_type pos) {
            if (!(pos < size()))
                throw std::out_of_range("array::at");
            return _b[pos];
        }
        
        const_reference at(size_type pos) const {
            if (!(pos < size()))
                throw std::out_of_range("array::at");
            return _b[pos];
        }
        
        reference operator[](size_type pos) {
            assert(pos < size());
            return _b[pos];
        }
        
        const_reference operator[](size_type pos) const {
            assert(pos < size());
            return _b[pos];
        }
        
        reference front() {
            assert(!empty());
            return *_b;
        }
        
        const_reference front() const {
            assert(!empty());
            return *_b;
        }
        
        reference back() {
            assert(!empty());
            return _c[-1];
        }
        const_reference back() const {
            assert(!empty());
            return _c[-1];
        }
        
        pointer data() {
            // assert(!empty());
            return _b;
        }

        const_pointer data() const {
            assert(!empty());
            return _b;
        }

        iterator begin() { return _b; }
        const_iterator begin() const { return _b; }
        const_iterator cbegin() const { return _b; }

        iterator end() { return _c; }
        const_iterator end() const { return _c; }
        const_iterator cend() const { return _c; }
        bool empty() const { return _b == _c; }
        size_type size() const { return _c - _b; }
        
        size_type max_size() const {
            return std::numeric_limits<std::ptrdiff_t>::max();
        }

        void reserve(size_type count) {
            if (count > (_d - _b))
                _reserve_back(count - size());
        }

        size_type capacity() const { return _d - _b; }
        
        void shrink_to_fit() const {
            size_type n = size();
            size_type m = n;
            T* a = allocate<T>(m);
            T* b = a + ((m - n) >> 1);
            std::memcpy(b, _b, n * sizeof(T));
            deallocate(_a);
            _a = a;
            _b = b;
            _c = b + n;
            _d = a + m;
        }
        
        void clear() {
            std::destroy(_b, _c);
            _c = _b = _a + ((_d - _a) >> 1);
        }

        
        iterator insert(const_iterator pos, const T& value) {
            T* q = _insert_uninitialized_n(pos, 1);
            return std::construct_at(q, value);
        }
        
        iterator insert(const_iterator pos, T&& value) {
            T* q = _insert_uninitialized_n(pos, 1);
            return std::construct_at(q, std::move(value));
        }
        
        iterator insert(const_iterator pos, size_type count, const T& value) {
            T* q = _insert_uninitialized_n(pos, count);
            std::uninitialized_fill_n(q, count, value);
            return q;
        }
        
        iterator insert(const_iterator pos, const_iterator first, const_iterator last) {
            T* q = _insert_uninitialized_n(pos, last - first);
            std::uninitialized_copy(first, last, q);
            return q;
        }
        
        template<typename InputIterator>
        iterator insert(const_iterator pos, InputIterator first, InputIterator last);

        template<typename... Args>
        iterator emplace(const_iterator pos, Args&&... args) {
            T* q = _insert_uninitialized_n(pos, 1);
            return std::construct_at(q, std::forward<Args>(args)...);
        }

        iterator erase(const_iterator pos) {
            std::destroy_at(pos);
            return _erase_uninitialized_n(pos, 1);
        }
        
        iterator erase(const_iterator first, const_iterator last) {
            std::destroy(first, last);
            return _erase_uninitialized_n(first, last - first);
        }

        void push_back(const T& value) {
            _reserve_back(1);
            _emplace_back(value);
        }
        
        void push_back(T&& value) {
            _reserve_back(1);
            _emplace_back(std::move(value));
        }
        
        template<typename... Args>
        reference emplace_back(Args&&... args) {
            _reserve_back(1);
            _emplace_back(std::forward<Args>(args)...);
            return _c[-1];
        }

        void pop_back() noexcept {
            assert(!is_empty());
            std::destroy_at(--_c);
        }
        
        void push_front(const T& value) {
            _reserve_front(1);
            _emplace_front(value);
        }
        
        void push_front(T&& value) {
            _reserve_front(1);
            _emplace_front(std::move(value));
        }
        
        template<typename... Args>
        reference emplace_front(Args&&... args) {
            _reserve_front(1);
            _emplace_front(std::forward<Args>(args)...);
            return front();
        }
        
        void pop_front() noexcept {
            assert(!is_empty());
            std::destroy_at(_b++);
        }
        
        void resize(size_t count) {
            if (count > size()) {
                auto n = count - size();
                auto p = _insert_uninitialized_n(n);
                std::uninitialized_value_construct_n(p, n);
            } else {
                auto c2 = _b + count;
                std::destroy(c2, _c);
                c2 = _c;
            }
        }
        
        void resize(size_t count, T value) {
            if (count > size()) {
                auto n = count - size();
                auto p = _insert_uninitialized_n(_c, n);
                std::uninitialized_fill_n(p, n, value);
            } else {
                auto c2 = _b + count;
                std::destroy(c2, _c);
                c2 = _c;
            }
        }
        
        void swap(array& other) {
            using std::swap;
            swap(_a, other._a);
            swap(_b, other._b);
            swap(_c, other._c);
            swap(_d, other._d);
        }


        
        
        
        
        
        
                

        
        bool operator==(const array& other) const {
            return std::equal(_b, _c, other._b, other._c);
        }
        
        
    
                
       
        iterator to(size_type n) {
            assert((0 <= n) && (n <= size()));
            return _b + n;
        }

        const_iterator to(size_type n) const {
            assert((0 <= n) && (n <= size()));
            return _b + n;
        }
        
        const_iterator cto(size_type n) {
            assert((0 <= n) && (n <= size()));
            return _b + n;
        }

        
        explicit operator bool() const {
            return _b != _c;
        }
        
        bool operator!() const {
            return _b == _c;
        }
        
        
        T* _insert_uninitialized_n(const T* pos, size_type count) {
            size_type h = _b - _a;
            size_type i = pos - _b;
            size_type j = _c - pos;
            size_type k = _d - _c;
            if ((j <= i) && (k >= count)) {
                relocate_backward_n(j, _c, _c + count);
                _c += count;
            } else if ((i <= j) && (h >= count)) {
                relocate_n(_b, i, _b - count);
                _b -= count;
            } else {
                size_type n = _c - _b;
                size_type m = 3 * n + count;
                T* a = allocate<T>(m);
                T* b = a + ((m - n - count) >> 1);
                T* c = b + n + count;
                T* d = a + m;
                relocate_n(_b, i, b);
                relocate_backward_n(j, _c, c);
                deallocate(_a);
                _a = a;
                _b = b;
                _c = c;
                _d = d;
            }
            return _b + i;
        }
                
        T* _erase_uninitialized_n(const T* pos, size_type count) {
            size_type i = pos - _b;
            size_type j = _c - pos - count;
            if (i <= j) {
                relocate_n(_b, i, _b + count);
                _b += count;
            } else {
                relocate_backward_n(j, _c, _c - count);
                _c -= count;
            }
            return _b + i;
        }
        
        T* erase_n(const T* first, size_type count) {
            std::destroy_n(first, count);
            return _erase_uninitialized_n(first, count);
        }
                        
        void _reserve_back(size_type count) {
            if (count > capacity_back()) {
                size_t n = _c - _b;
                size_t m = 3 * n + count;
                T* a = allocate<T>(m);
                T* b = a + ((m - n - count) >> 1);
                T* c = b + n;
                T* d = a + m;
                std::memcpy(b, _b, n * sizeof(T));
                deallocate(_a);
                _a = a;
                _b = b;
                _c = c;
                _d = d;
            }
            assert(count <= capacity_back());
        }
        
        void _reserve_front(size_type count) {
            if (count > capacity_front()) {
                size_t n = _c - _b;
                size_t m = 3 * n + count;
                T* a = allocate<T>(m);
                T* b = a + ((m - n + count) >> 1);
                T* c = b + n;
                T* d = a + m;
                std::memcpy(b, _b, n * sizeof(T));
                deallocate(_a);
                _a = a;
                _b = b;
                _c = c;
                _d = d;
            }
            assert(count <= capacity_front());
        }
        
        bool is_empty() const {
            return _b == _c;
        }
                
                
        template<typename... Args>
        void _emplace_front(Args&&... args) {
            assert(_a < _b);
            std::construct_at(_b - 1, std::forward<Args>(args)...);
            --_b;
        }
        
        template<typename... Args>
        void _emplace_back(Args&&... args) {
            assert(_c < _d);
            std::construct_at(_c, std::forward<Args>(args)...);
            ++_c;
        }
        
        size_t capacity_back() const {
            return _d - _c;
        }
        
        size_t capacity_front() const {
            return _b - _a;
        }
                
        void _did_write_back(size_t n) {
            assert(n <= _d - _c);
            _c += n;
        }
        
        void _did_read_front(size_t n) {
            assert(n <= _c - _b);
            _b += n;
        }
                                
        // buffer interface
        
        size_t can_write_back() {
            return _d - _c;
        }
        
        T* may_write_back(size_t n) {
            _reserve_back(n);
            return _c;
        }
        
        T* will_write_back(size_t n) {
            _reserve_back(n);
            return exchange(_c, _c + n);
        }
        
        void did_write_back(size_t n) {
            assert(n <= _d - _c);
            _c += n;
        }
        
        size_t can_read_front() {
            return _c - _b;
        }
        
        T* may_read_front(size_t n) {
            assert(n <= _c - _b);
            return _b;
        }
        
        T* will_read_front(size_t n) {
            assert(n <= _c - _b);
            return exchange(_b, _b + n);
        }
        
        void did_read_front(size_t n) {
            assert(n <= _c - _b);
            _b += n;
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
        using std::swap;
        swap(a._a, b._a);
        swap(a._b, b._b);
        swap(a._c, b._c);
        swap(a._d, b._d);
    }
    
    
} // namespace wry

#endif /* array_hpp */
