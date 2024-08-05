//
//  wry/RealTimeGarbageCollectedDynamicArray.hpp
//  client
//
//  Created by Antony Searle on 19/6/2024.
//

#ifndef wry_RealTimeGarbageCollectedDynamicArray_hpp
#define wry_RealTimeGarbageCollectedDynamicArray_hpp

#include "debug.hpp"
#include "object.hpp"
#include "utility.hpp"

namespace wry::gc {
    
    // TODO: garbage collected containers are not a good fit with the references
    // and iterators of standard C++ iterators, both because they hold
    // Atomic<T> and need to incrementally copy stuff in the background, making
    // addresses especially unstable
    
    // TODO: when erasing containers we need to have an in interface to cheaply
    // passivate an element
    
    
    
    // Managed fixed sized arrays
    //
    // Indirect via a pointer, to prevent the header from bumping a power of
    // two array size as needed by hash maps

    template<typename T>
    struct GarbageCollectedIndirectStaticArray : Object {
        
        T* const _data;
        const size_t _size;
        
        void _invariant() {
            assert(_data && _size);
        }
        
        explicit GarbageCollectedIndirectStaticArray(size_t elements)
        : _data((T*) calloc(elements, sizeof(T)))
        , _size(elements) {
            assert(_data);
            assert(std::has_single_bit(elements));
        }
                
        virtual ~GarbageCollectedIndirectStaticArray() override {
            free(_data);
        }
        
        size_t size() const { return _size; }
        
        T* data() { return _data; }
        T* begin() { return _data; }
        T* end() { return _data + _size; }

        const T* data() const { return _data; }
        const T* begin() const { return _data; }
        const T* end() const { return _data + _size; }
        
        T& front() { return *_data; }
        T& back() { return _data[_size - 1]; }
        T& operator[](size_t i) const {
            assert(i < _size);
            return _data[i];
        }
        
        virtual void _object_scan() const override {
            for (const T& element : *this)
                object_trace(element);
        }
        
        virtual void _object_debug() const override {
            auto sv = type_name<T>();
            printf("GarbageCollectedIndirectStaticArray<%.*s>(%zd){ ", (int)sv.size(), (const char*)sv.data(), _size);
            for (const T& element : *this) {
                object_debug(element);
                printf(", ");
            }
            printf("}");
        }
        
    };
    
    // Direct via a flexible array member
    //
    // Not efficient for powers of two
    
    template<typename T>
    struct GarbageCollectedFlexibleArrayMemberStaticArray : Object {
        
        static void* operator new(size_t bytes, size_t elements) {
            return Object::operator new(bytes + elements * sizeof(T));
        }
        
        static void* operator new(size_t bytes, void* ptr) {
            return ptr;
        }
        
        static GarbageCollectedFlexibleArrayMemberStaticArray* with_exactly(size_t elements) {
            return new(elements) GarbageCollectedFlexibleArrayMemberStaticArray(elements);
        }
        
        static GarbageCollectedFlexibleArrayMemberStaticArray* with_at_least(size_t elements) {
            size_t bytes = elements * sizeof(T);
            bytes += sizeof(GarbageCollectedFlexibleArrayMemberStaticArray);
            bytes = std::bit_ceil(bytes);
            void* raw = Object::operator new(bytes);
            bytes -= sizeof(GarbageCollectedFlexibleArrayMemberStaticArray);
            elements = bytes / sizeof(T);
            return new(raw) GarbageCollectedFlexibleArrayMemberStaticArray(elements);
        }
        
        const size_t _size;
        T _data[]; // flexible array member
        
        explicit GarbageCollectedFlexibleArrayMemberStaticArray(size_t elements)
        : _size(elements) {
        }

        virtual ~GarbageCollectedFlexibleArrayMemberStaticArray() override {
        }
        
        size_t size() const { return _size; }

        T* data() { return _data; }
        T* begin() { return _data; }
        T* end() { return _data + _size; }

        const T* data() const { return _data; }
        const T* begin() const { return _data; }
        const T* end() const { return _data + _size; }
        
        T& front() { return *_data; }
        T& back() { return _data[_size - 1]; }
        T& operator[](size_t i) const {
            assert(i < _size);
            return _data[i];
        }

        virtual void _object_scan() const override {
            for (const T& element : *this)
                object_trace(element);
        }
        
        virtual void _object_debug() const override {
            auto sv = type_name<T>();
            printf("GarbageCollectedFlexibleArrayMemberStaticArray<%.*s>(%zd){ ", (int)sv.size(), (const char*)sv.data(), _size);
            for (const T& element : *this) {
                object_debug(element);
                printf(", ");
            }
            printf("}");
        }
    };
    

    
    
    
    // Manage a valid subset of an array
        
    template<typename T>
    struct GarbageCollectedArrayC {
        
        T* _begin;
        T* _end;
        T* _capacity;
        Traced<GarbageCollectedFlexibleArrayMemberStaticArray<T>*> _storage;
        
        GarbageCollectedArrayC()
        : _begin(nullptr)
        , _end(nullptr)
        , _capacity(nullptr)
        , _storage(nullptr) {}
        
        GarbageCollectedArrayC(GarbageCollectedArrayC&& other)
        : _begin(exchange(other._begin, nullptr))
        , _end(exchange(other._end, nullptr))
        , _capacity(exchange(other._capacity, nullptr))
        , _storage(std::move(other._storage)) {
        }
        
        void swap(GarbageCollectedArrayC& other) {
            using std::swap;
            swap(_begin, other._begin);
            swap(_end, other._end);
            swap(_capacity, other._capacity);
            swap(_storage, other._storage);
        }
        
        GarbageCollectedArrayC& operator=(GarbageCollectedArrayC&& other) {
            _begin = exchange(other._begin, nullptr);
            _end = exchange(other._end, nullptr);
            _capacity = exchange(other._capacity, nullptr);
            // TODO: atomic exchange?
            _storage = std::move(other._storage);
        }
        
        bool empty() const { return _begin == _end; }
        size_t size() const { return _end - _begin; }
        size_t capacity() const { return _capacity - _begin; }

        bool full() const { return _end == _capacity; }
        size_t grace() const { return _capacity - _end(); }
        
        const T* begin() const { return _begin; }
        const T* end() const { return _end; }
        T* begin() { return _begin; }
        T* end() { return _end; }
        
        const T& front() const { return *_begin; }
        const T& back() const { return *(_end - 1); }
        T& front() { return *_begin; }
        T& back() { return *(_end - 1); }

        /*
        void _install(GarbageCollectedFlexibleArrayMemberStaticArray<T>* new_) {
            _end = std::move(begin(), end(), new_->begin());
            _begin = new_->begin();
            _capacity = new_->end();
            _storage = new_;
        }
        
        void _grow() {
            assert(_end == _capacity);
            // with_at_least will grow the backing allocation to the next power
            // of two bytes. thus will slightly more than double the capacity,
            // unless the existing state is not a power of two as the result of
            // a shrink_to_fit or other explicit sizing, in which case the
            // "early" copy is billed to that call's O(N) cost
            _install(GarbageCollectedFlexibleArrayMemberStaticArray<T>::with_at_least(capacity() + 1));
            assert(_end != _capacity);
        }
         */
        
        void _clear_and_reserve(size_t new_capacity) {
            auto* p = GarbageCollectedFlexibleArrayMemberStaticArray<T>::with_at_least(new_capacity);
            _begin = _end = p->_data;
            _capacity = _begin + p->_size;
            _storage = p;
        }
        
        template<typename V>
        void push_back(V&& value) {
            assert(!full());
            *_end++ = std::forward<V>(value);
        }
        
        void pop_back() {
            assert(!empty());
            --_end;
            // TODO: passivate?
            // *_end = T();
        }
        
        void _pop_front() {
            assert(!empty());
            // TODO: passivate?
            ++_begin;
        }
        
        template<typename V>
        void _push_front(V&& value) {
            assert(_storage && (_begin != _storage->begin()));
            *--_begin = std::forward<V>(value);
        }
                
        void clear() {
            // for (auto& element : *this)
                // element = T();
            _end = _begin;
        }
        
        // void shrink_to_fit() {
            // _install(GarbageCollectedFlexibleArrayMemberStaticArray<T>::with_exactly(size()));
        //}
        
        //void reserve(size_t n) {
            //if (n > capacity()) {
               // _install(GarbageCollectedFlexibleArrayMemberStaticArray<T>::with_at_least(n));
            //}
        //}
        
        
    };
    
    template<typename T>
    void swap(GarbageCollectedArrayC<T>& a, GarbageCollectedArrayC<T>& b) {
        a.swap(b);
    }
    
    template<typename T>
    void object_shade(const GarbageCollectedArrayC<T>& self) {
        object_shade(self._storage);
    }

    template<typename T>
    void object_trace(const GarbageCollectedArrayC<T>& self) {
        object_trace(self._storage);
    }

    template<typename T>
    void object_debug(const GarbageCollectedArrayC<T>& self) {
        printf("(GarbageCollectedDynamicArray)");
        object_debug(self._storage);
    }

    // We can hoist DynamicArray itself into a collection with Box
    //
    // Box<DynamicArray>
    
    
    
    
    
    
    // Maintains two DynamicArrays internally and carefully manages their state
    // to not trigger O(N) resizes.  Growth is accomplished by
    // taxing push_back to also pop_back the old array and push_front the new,
    // so by the time the next resize is required the old array will have been
    // completely copied over
    
    template<typename T>
    struct RealTimeGarbageCollectedDynamicArray {
        
        GarbageCollectedArrayC<T> _alpha;
        GarbageCollectedArrayC<T> _beta;
        
        enum State {
            INITIAL,
            NORMAL,
            RESIZING,
        };
        State _state;
        
        RealTimeGarbageCollectedDynamicArray()
        : _state(INITIAL) {
        }
                
        template<typename V>
        void push_back(V&& value) {
            for (;;) {
                switch (_state) {
                    case INITIAL: {
                        assert(_alpha.empty());
                        assert(_beta.empty());
                        _alpha._clear_and_reserve(1);
                        _state = NORMAL;
                        break;
                    }
                    case NORMAL: {
                        if (!_alpha.full()) {
                            _alpha.push_back(std::move(value));
                            return;
                        }
                        assert(_beta.empty());
                        _beta._clear_and_reserve(_alpha.size() * 2);
                        _beta._begin += _alpha.size();
                        _beta._end = _beta._begin;
                        _state = RESIZING;
                        break;
                    }
                    case RESIZING: {
                        assert(!_beta.full());
                        _beta.push_back(std::move(value));
                        assert(!_alpha.empty());
                        _beta._push_front(std::move(_alpha.back()));
                        _alpha.pop_back();
                        if (_alpha.empty()) {
                            _alpha.swap(_beta);
                            _state = NORMAL;
                        }
                        return;
                    }
                }
            }
        }
        
        void pop_back() {
            switch (_state) {
                case INITIAL: {
                    abort();
                }
                case NORMAL: {
                    _alpha.pop_back();
                    break;
                }
                case RESIZING: {
                    assert(!_alpha.empty());
                    assert(!_beta.empty());
                    _beta.pop_back();
                    _beta._push_front(std::move(_alpha.back()));
                    _alpha.pop_back();
                    if (_alpha.empty()) {
                        _alpha.swap(_beta);
                        _state = NORMAL;
                    }
                    break;
                }
            }
        }
        
        const T& operator[](size_t i) const {
            if (i < _alpha.size())
                return _alpha._begin[i];
            i -= _alpha.size();
            if (i < _beta.size())
                return _beta._begin[i];
            abort();
        }
        
        T& operator[](size_t i) {
            if (i < _alpha.size())
                return _alpha._begin[i];
            i -= _alpha.size();
            if (i < _beta.size())
                return _beta._begin[i];
            abort();
        }
        
        T& front() {
            return _alpha.front();
        }
        
        const T& front() const {
            return _alpha.front();
        }
        
        const T& back() const {
            switch (_state) {
                case INITIAL:
                    abort();
                case NORMAL:
                    return _alpha.back();
                case RESIZING:
                    return _beta.back();
            }
        }
        T& back() {
            switch (_state) {
                case INITIAL:
                    abort();
                case NORMAL:
                    return _alpha.back();
                case RESIZING:
                    return _beta.back();
            }
        }
        
        bool empty() const {
            return _alpha.empty();
        }
        
        size_t size() const {
            return _alpha.size() + _beta.size();
        }
        
        void clear() {
            _alpha.clear();
            _beta.clear();
        }
                        
    }; // struct RealTimeGarbageCollectedDynamicArray<T>
    
        
    template<typename T>
    void swap(RealTimeGarbageCollectedDynamicArray<T>& a,
              RealTimeGarbageCollectedDynamicArray<T>& b) {
        a.swap(b);
    }
    
    template<typename T>
    void object_shade(const RealTimeGarbageCollectedDynamicArray<T>& self) {
        object_shade(self._alpha);
        object_shade(self._beta);
    }
    
    template<typename T>
    void object_trace(const RealTimeGarbageCollectedDynamicArray<T>& self) {
        object_trace(self._alpha);
        object_trace(self._beta);
    }
    
    template<typename T>
    void object_debug(const RealTimeGarbageCollectedDynamicArray<T>& self) {
        printf("(RealTimeGarbageCollectedDynamicArray)");
        object_debug(self._alpha);
        object_debug(self._beta);
    }

    
    


    
    

    
} // namespace wry::gc

#endif /* wry_RealTimeGarbageCollectedDynamicArray_hpp */
