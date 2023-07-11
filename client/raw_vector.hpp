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
#include "vector_view.hpp"


namespace wry {
    
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
