//
//  raw_vector.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef raw_vector_hpp
#define raw_vector_hpp

#include <cstdlib>
#include <utility>

#ifdef __APPLE__
#include <malloc/malloc.h>
#endif


#include "common.hpp"
#include "utility.hpp"
#include "vector_view.hpp"


namespace wry {
    
    // an array of trivial types
        
    template<typename T>
    struct allocation {
        
        static_assert(std::is_trivial_v<T>);
        
        T* _begin;
        T* _end;
                        
        explicit allocation(std::size_t count) noexcept
        : _begin(static_cast<T*>(operator new(count * sizeof(T))))
        , _end(_begin + count) {
            assert(_begin || !count);
        }
        
        ~allocation() noexcept {
            operator delete(_begin);
        }

        allocation() noexcept
        : _begin(nullptr)
        , _end(nullptr) {
        }
        
        allocation(const allocation& other) noexcept
        : allocation(other.size()) {
            std::memcpy(_begin, other._begin, other.size() * sizeof(T));
        }
        
        allocation(allocation&& other) noexcept
        : _begin(exchange(other._begin, nullptr))
        , _end(exchange(other._end, nullptr)) {
        }
        
        allocation& operator=(const allocation& other) noexcept {
            assert(this != std::addressof(other));
            std::size_t count = other.size() * sizeof(T);
            if (other.size() != size())
                operator delete(exchange(_begin, static_cast<T*>(operator new(count))));
            T* _end = _begin + other.size();
            std::memcpy(_begin, other._begin, count);
            return *this;
        }
        
        allocation& operator=(allocation&& other) noexcept {
            assert(this != std::addressof(other));
            operator delete(exchange(_begin, other._begin, nullptr));
            exchange(_end, other._end, nullptr);
            return *this;
        }
                        
        const T& operator[](std::size_t pos) const noexcept {
            assert(pos < size());
            return _begin[pos];
        }

        T& operator[](std::size_t pos) noexcept {
            assert(pos < size());
            return _begin[pos];
        }
        
        const T& front() const {
            assert(!empty());
            return *_begin;
        }

        T& front() {
            assert(!empty());
            return *_begin;
        }
        
        T& back() {
            assert(!empty());
            return *(_end - 1);
        }
        
        const T& back() const {
            assert(!empty());
            return *(_end - 1);
        }

        T* data() noexcept {
            return _begin;
        }

        const T* data() const noexcept {
            return _begin;
        }
        
        T* begin() noexcept {
            return _begin;
        }

        T* end() noexcept {
            return _end;
        }
        
        bool empty() const {
            return _end != _begin;
        }
        
        std::size_t size() const noexcept {
            std::ptrdiff_t count = _end - _begin;
            assert(count >= 0);
            return static_cast<std::size_t>(count);
        }

        void resize(std::size_t count) {
            if (count > size()) {
                std::size_t n = count * sizeof(T);
                T* begin2 = static_cast<T*>(operator new(n));
                std::memcpy(begin2, _begin, n);
                operator delete(exchange(_begin, begin2));
            }
            _end = _begin + count;
        }

        void swap(allocation& other) {
            using std::swap;
            swap(_begin, other._begin);
            swap(_end, other._end);
        }
        
        void clear() {
            operator delete(exchange(_begin, nullptr));
            _end = nullptr;
        }
        
    };
    
    template<typename T>
    void swap(allocation<T>& a, allocation<T>& b) {
        a.swap(b);
    }
    
        
    
    
    // raw_vector manages a slab of callocated raw memory.  It will free the memory
    // on destruction, but will not attempt to construct or destruct any objects in
    // the memory.  It must be combined with some external management to determine
    // which slots are occupied; a std::vector maintains a _size, partioning
    // occupied and unoccupied slots, for example.

    
    template<typename T>
    struct raw_vector {
        
        T* _allocation;
        isize _capacity;
        
        raw_vector() : _allocation(nullptr), _capacity(0) {}
        
        raw_vector(const raw_vector&) = delete;
        raw_vector(raw_vector&& v) : raw_vector() { swap(v); }
        explicit raw_vector(isize capacity) {
#if defined _WIN64
            HANDLE heap = GetProcessHeap();
            _allocation = (T*) HeapAlloc(heap, HEAP_ZERO_MEMORY, capacity * sizeof(T));
            _capacity = HeapSize(heap, _allocation) / sizeof(T);
#elif defined __APPLE__
            _allocation = (T*) std::calloc(capacity, sizeof(T));
            _capacity = malloc_size(_allocation) / sizeof(T);
            assert(_capacity >= capacity);
#else
            _allocation = (T*) std::calloc(capacity, sizeof(T));
            _capacity = capacity;
#endif
        }
        raw_vector(T* ptr, isize n) : _allocation(ptr), _capacity(n) {}
        
        ~raw_vector() {
#if defined _WIN64
            HeapFree(GetProcessHeap(), _allocation);
#else
            free(_allocation);
#endif
        }
        
        raw_vector& operator=(const raw_vector&) = delete;
        raw_vector& operator=(raw_vector<T>&&);
        
        void swap(raw_vector& v) {
            using std::swap;
            swap(_allocation, v._allocation);
            swap(_capacity, v._capacity);
        }
        
        isize capacity() const { return _capacity; }
        
        // Unsafe methods
        
        isize size() const { return _capacity; }
        T* begin() const { return _allocation; }
        T* end() const { return _allocation + _capacity; }
        T& operator[](isize i) const { return _allocation[i]; }
        
    };
    
    template<typename T>
    struct raw_vector<const T>;
    
    template<typename T>
    void swap(raw_vector<T>& a, raw_vector<T>& b) {
        a.swap(b);
    }
    
    template<typename T>
    raw_vector<T>& raw_vector<T>::operator=(raw_vector<T>&& v) {
        raw_vector<T>(std::move(v)).swap(*this);
        return *this;
    }
    
} // namespace manic

#endif /* raw_vector_hpp */
