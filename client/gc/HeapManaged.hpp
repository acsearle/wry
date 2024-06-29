//
//  HeapManaged.hpp
//  client
//
//  Created by Antony Searle on 22/6/2024.
//

#ifndef wry_gc_Heapmanaged_hpp
#define wry_gc_Heapmanaged_hpp

#include "value.hpp"

#include "debug.hpp"

namespace wry::gc {
    
    // Backing flat storage for more complex structures
    //
    // Manages the elements of the array for garbage collection, but otherwise
    // provides no services.
    //
    // Elements must support object_trace(...), must be happy to be
    // zero-initialized, and must not be destroyed in place.  For tracing, they
    // must be immutable after publication, or atomic.  Elements may be tuples
    // of mixed traced/non-traced things.  If T is not traced at all, prefer
    // a basic T* finalized by its owner.
    
    // Indirect to permit power-of-two chunks for hash table.  The
    // An alternative would be to use a header-flexible-array struct as in
    // HeapString
    
    // Terrible names
    
    
    template<typename T>
    struct HeapManaged : Object {
        std::size_t _capacity;
        T* const _storage;
        explicit HeapManaged(size_t);
        virtual ~HeapManaged() override;
        virtual void _object_scan() const override;
    };
    
    template<typename T>
    HeapManaged<T>::HeapManaged(size_t n)
    : _capacity(n)
    , _storage((T*) calloc(n, sizeof(T))) {
        assert(_capacity && _storage);
        // TODO:
        // operator new, malloc, calloc, or std::uninitialized_default_construct?
    }

    template<typename T>
    HeapManaged<T>::~HeapManaged() {
        assert(_capacity && _storage);
        // TODO:
        // operator new, malloc, calloc, or std::uninitialized_default_construct?
        free(_storage);
        // auto sv = type_name<T>();
        // printf("~HeapManaged<%.*s>[%zd]\n", (int)sv.size(), (const char*)sv.data(), _capacity);
    }

    
    template<typename T>
    void HeapManaged<T>::_object_scan() const {
        assert(_capacity && _storage);
        T* first = _storage;
        T* last = _storage + _capacity;
        for (; first != last; ++first)
            object_trace(*first);
    }
    
    
    
    
    
} // namespace wry::gc

#endif /* wry_gc_Heapmanaged_hpp */
