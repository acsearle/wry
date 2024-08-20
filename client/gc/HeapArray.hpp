//
//  wry/RealTimeGarbageCollectedDynamicArray.hpp
//  client
//
//  Created by Antony Searle on 19/6/2024.
//

#ifndef wry_HeapArray_hpp
#define wry_HeapArray_hpp

#include "debug.hpp"
#include "object.hpp"
#include "traced.hpp"
#include "utility.hpp"

namespace wry::gc {
    
    // The garbage collector thread will
    // access .color
    // call object_scan
    //     call object_trace
    // call object_sweep
    //
    // All but object_trace imply first-class objects
    // object_trace will be called on embedded sub-objects
    // it must touch only const or atomic things
    //
    // We have Arrays of several different kinds of thing:
    //
    // Trivial types (plain old data, including nonowning pointers to GC objects)
    // Nontrivial types (invariants, copy constructors, destructors)
    // Trivial types requiring tracing ( Values, owning pointers to GC objects, tuples including such)
    // Nontrivial types requiring tracing ( containers, general )
    //
    // Notably, Array acquires its "requires tracing" state from its entries
    // But the brute type of those entries is not always enough to decide if
    // they need tracing.  Consider Array<Entity*> and Array<Traced<Entity*>>
    //
    // If elements are not traced, then the Array can directly manage its memory
    //
    // If something is traced, they must contain fields that are atomic (or
    // const) and can't be directly accessed as T& in the standard C++ container
    // way.
    //
    // We can thus have Array<Traced<Value>> that gives access to Traced<Value>&
    // but not Value&.  Array<Value> is weak or leaky, but still useful for
    // short-lived things (that do not cross the mutator boundary).  Seems like
    // the wrong default though.  Traced is not some kind of annotation but
    // fundamentally changes the semantics (to rust::Cell?)
    //
    // Likewise Array<Array<Traced<Value>>.
    //
    // These objects are heavyweight in some sense.  Copying is strange.
    // Lifetime is strange.  Because the mutator is walking the storage, we
    // cannot construct or destroy them as they move in and out of the logical
    // array; they must always be in a valid state, and erasure can at best
    // passivate them in some object-dependent way (nulling pointers etc.),
    // not actually run their destructors.
    //
    // The traditional GC language solution is to box everything, so we are
    // always worst case.  For us this would be Array<Traced<T*>> or some
    // casting wrapper around a single ArrayValue implementation.
    
    
    
    
    

    
    
    
    // There are a variety of things presenting as an array with different
    // and sometimes incompatible backend behaviors:
    //
    // - contiguous storage and pointer arithmetic
    // - resizable
    // - amortized constant time operations ("throughput")
    // - constant time bounded ("latency")
    // - exact storage sizing ("footprint")
    // - garbage collected elements (Array<Traced<T>>)
    // - garbage collected self (Box<Array<T>>)
    // - efficient operations at ends: stack, or queue, or deque
    //
    // Array<T>         - real-time
    // Array<Traced<T>> - real-time, garbage-collected elements
    // Box<Array<T>>    - real time,
    
    // Incremental resize rules out contiguous storage.
    // We can then have circular buffer storage
    // Circular buffers imply power of two storage, and tracking both ends
    // This gives us efficient deque operations for free
    // It also gives us absolute indexing if we want it
    //
    // Garbage collected rules out direct access to elements
    // Not contiguous makes iterators less attractive
    
    
    template<typename T>
    struct CircularBuffer {
        
        T* _data;
        ptrdiff_t _capacity;
        
        void _invariant() {
            assert(_data || _capacity == 0);
            assert(std::has_single_bit(_capacity) || _capacity == 0);
        }
                
        constexpr size_t capacity() const {
            return _capacity;
        }
        
        constexpr ptrdiff_t mask(ptrdiff_t i) const {
            return i & (_capacity - 1);
        }
        
        T& operator[](ptrdiff_t i) const {
            return _data[mask(i)];
        }

    };
    
    template<typename T>
    struct StaticCircularDeque {
      
        CircularBuffer<T> _inner;
        ptrdiff_t _begin;
        ptrdiff_t _end;
        
        void _invariant() const {
            assert(_inner.invariant());
            assert(_end - _begin <= _inner._capacity);
        }
        
        constexpr size_t capacity() const { return _inner.capacity(); }
        constexpr bool empty() const { return _begin == _end; }
        constexpr bool size() const { return _end - _begin; }
        constexpr bool full() const { return size() == capacity(); }
        
        T& front() const {
            assert(!empty());
            return _inner[_begin];
        }
        
        T& back() const {
            assert(!empty());
            return _inner[_end - 1];
        }
        
        void push_back(T x) {
            assert(!full());
            _inner[_end++] = std::move(x);
        }
        
        void push_front(T x) {
            assert(!full());
            _inner[--_begin] = std::move(x);
        }
        
        void pop_back() {
            assert(!empty());
            _inner[--_end] = T();
        }
        
        void pop_front() {
            assert(!empty());
            _inner[_begin++] = T();
        }

    };
    
    
    
    
    template<typename T>
    struct ArrayFixedSize {
        
        T* _data;
        size_t _capacity;
        
        void _invariant() const {
            assert(_data || _capacity == 0);
            assert(std::has_single_bit(_capacity) || _capacity == 0);
        }
        
        ArrayFixedSize() : _data(nullptr), _capacity(0) {}
        ArrayFixedSize(T* data_, size_t capacity_)
        
        : _data(data_)
        , _capacity(capacity_) {}
        
        T* data() { return _data; }
        const T* data() const { return _data; }
        
        constexpr size_t capacity() const { return _capacity; }
        constexpr size_t mask(size_t i) const { return i & (_capacity - 1); }
        
        const T& operator[](size_t i) const { return _data[mask(i)]; }
        
    };
    
    template<typename T>
    struct ArrayStatic {
        T* _data;
        ptrdiff_t _capacity;
        ptrdiff_t _begin;
        ptrdiff_t _end;
        
        constexpr ptrdiff_t size() const {
            return _end - _begin;
        }

        constexpr ptrdiff_t capacity() const {
            return _capacity;
        }
        
        constexpr ptrdiff_t grace() const {
            return capacity() - size();
        }
        
        constexpr bool empty() const {
            return _begin == _end;
        }
        
        constexpr bool full() const {
            return _begin + _capacity == _end;
        }
        
        constexpr ptrdiff_t _mask(ptrdiff_t i) const {
            return i & (_capacity - 1);
        }
                
        void _invariant() const {
            assert(std::has_single_bit(_capacity) || _capacity == 0);
            assert(_data || _capacity == 0);
            assert(_begin <= _end);
            assert(_end <= _begin + _capacity);
        }

        ArrayStatic(size_t capacity_)
        : _data((T*) calloc(capacity_, sizeof(T)))
        , _capacity(capacity_)
        , _begin(0)
        , _end(0) {
            _invariant();
        }
        
        template<typename U>
        void push_front(U&& u) {
            _invariant();
            assert(!full());
            _data[_mask(--_begin)] = std::forward<U>(u);
        }
                
        template<typename U>
        void push_back(U&& u) {
            _invariant();
            assert(!full());
            _data[_mask(_end++)] = std::forward<U>(u);
        }
                
        T& front() {
            _invariant();
            assert(!empty());
            return _data[_begin];
        }
        
        T& back() {
            _invariant();
            assert(!empty());
            return _data[_end - 1];
        }
        
        void pop_front() {
            _invariant();
            assert(!empty());
            ++_begin;
        }
        
        void pop_back() {
            _invariant();
            assert(!empty());
            --_end;
        }
        
        T& operator[](ptrdiff_t i) {
            _invariant();
            assert(0 <= i);
            ptrdiff_t j = _begin + i;
            assert(i < _end);
            return _data[j];
        }
        
        void clear() {
            _invariant();
            _begin = _end;
        }
        
        void destructive_reserve(size_t new_capacity) {
            _invariant();
            assert(std::has_single_bit(new_capacity));
            T* new_data = calloc(new_capacity, sizeof(T));
            assert(new_data);
            free(exchange(_data, new_data));
            _capacity = new_capacity;
        }
        
        void swap(ArrayStatic& other) {
            _invariant();
            using std::swap;
            swap(_data, other._data);
            swap(_capacity, other._capacity);
            swap(_begin, other._begin);
            swap(_end, other._end);
        }
                        
    };
    
    template<typename T>
    void swap(ArrayStatic<T>& left, ArrayStatic<T>& right) {
        left.swap(right);
    }
    
    // Real-time dynamic array
    
    template<typename T>
    struct ArrayIncremental {
        ArrayIncremental<T> _alpha;
        ArrayIncremental<T> _beta;
        
        void _invariant() {
            _alpha._invariant();
            _beta._invariant();
            if (!_beta.empty()) {
                assert(_alpha._begin <= _beta._begin);
                assert(_beta._end <= _alpha._end);
            }
        }
        
        void _tax_front() {
            _invariant();
            if (!_beta.empty()) {
                _alpha._data[_alpha._mask(_beta._begin)] = std::move(_beta.front());
                _beta.pop_front();
            }
        }

        void _tax_back() {
            _invariant();
            if (!_beta.empty()) {
                _alpha._data[_alpha._mask(_beta._end - 1)] = std::move(_beta.back());
                _beta.pop_back();
            }
        }

        void _ensure_not_full() {
            _invariant();
            if (_alpha.full()) {
                assert(_beta.empty());
                _alpha.swap(_beta);
                _alpha.destructive_resize(min(8, _beta.capacity() << 1));
                _alpha._begin = _beta._begin;
                _alpha._end = _beta._end;
            }
        }

        template<typename U>
        void push_front(U&& u) {
            _invariant();
            _tax_front();
            _ensure_not_full();
            _alpha.push_front(std::forward<U>(u));
        }

        template<typename U>
        void push_back(U&& u) {
            _invariant();
            _tax_back();
            _ensure_not_full();
            _alpha.push_back(std::forward<U>(u));
        }
        
        void pop_back() {
            _invariant();
            _tax_back();
            assert(!_alpha.empty());
            --_alpha._back;
        }

        void pop_front() {
            _invariant();
            _tax_front();
            assert(!_alpha.empty());
            ++_alpha._begin;
        }

        T& front() {
            _tax_front();
            assert(!_alpha.empty());
            return _alpha.front();
        }
        
        T& back() {
            _tax_back();
            return _alpha.back();
        }
        
        T& operator[](ptrdiff_t i) {
            _tax_back();
            assert(0 <= i);
            ptrdiff_t j = _alpha._begin + i;
            assert(j < _alpha._end);
            if ((j < _beta._begin) || (_beta._end <= j))
                return _alpha._data[_alpha._mask(j)];
            else
                return _beta._data[_beta._mask(j)];
        }

    };
    
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

    template<ObjectTrait T>
    struct ArrayStaticIndirect : Object {
        
        T* const _data;
        const size_t _size;
        
        void _invariant() {
            assert(_data && _size);
        }
        
        explicit ArrayStaticIndirect(size_t elements)
        : _data((T*) calloc(elements, sizeof(T)))
        , _size(elements) {
            assert(_data);
            assert(std::has_single_bit(elements));
        }
                
        virtual ~ArrayStaticIndirect() override {
            free(_data);
        }
        
        size_t size() const { return _size; }
        
        T* data() { return _data; }
        const T* data() const { return _data; }

        T* begin() { return _data; }
        const T* begin() const { return _data; }

        T* end() { return _data + _size; }
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
            printf("IndirectStaticArray<%.*s>(%zd){ ", (int)sv.size(), (const char*)sv.data(), _size);
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
    
    template<ObjectTrait T>
    struct ArrayStaticFlexibleArrayMember : Object {
        
        static void* operator new(size_t bytes, size_t elements) {
            return Object::operator new(bytes + elements * sizeof(T));
        }
        
        static void* operator new(size_t bytes, void* ptr) {
            return ptr;
        }
        
        static ArrayStaticFlexibleArrayMember* with_exactly(size_t elements) {
            return new(elements) ArrayStaticFlexibleArrayMember(elements);
        }
        
        static ArrayStaticFlexibleArrayMember* with_at_least(size_t elements) {
            size_t bytes = elements * sizeof(T);
            bytes += sizeof(ArrayStaticFlexibleArrayMember);
            bytes = std::bit_ceil(bytes);
            void* raw = Object::operator new(bytes);
            bytes -= sizeof(ArrayStaticFlexibleArrayMember);
            elements = bytes / sizeof(T);
            return new(raw) ArrayStaticFlexibleArrayMember(elements);
        }
        
        const size_t _size;
        T _data[]; // flexible array member
        
        explicit ArrayStaticFlexibleArrayMember(size_t elements)
        : _size(elements) {
        }

        virtual ~ArrayStaticFlexibleArrayMember() override {
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
            printf("ArrayStaticFlexibleArrayMember<%.*s>(%zd){ ", (int)sv.size(), (const char*)sv.data(), _size);
            for (const T& element : *this) {
                object_debug(element);
                printf(", ");
            }
            printf("}");
        }
    };
    

    
    
    
    // Manage a valid subset of an array
        
    template<ObjectTrait T>
    struct GarbageCollectedArrayC {
        
        T* _begin;
        T* _end;
        T* _capacity;
        Traced<ArrayStaticFlexibleArrayMember<T>*> _storage;
        
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
            auto* p = ArrayStaticFlexibleArrayMember<T>::with_at_least(new_capacity);
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
    
    template<ObjectTrait T>
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
    void object_trace_weak(const RealTimeGarbageCollectedDynamicArray<T>& self) {
        object_trace(self);
    }

    template<typename T>
    void object_debug(const RealTimeGarbageCollectedDynamicArray<T>& self) {
        printf("(RealTimeGarbageCollectedDynamicArray)");
        object_debug(self._alpha);
        object_debug(self._beta);
    }


    template<typename T>
    size_t object_hash(const RealTimeGarbageCollectedDynamicArray<T>& self) {
        abort();
    }

    template<typename T>
    void object_passivate(RealTimeGarbageCollectedDynamicArray<T>& self) {
        abort();
    }
    
} // namespace wry::gc

#endif /* wry_HeapArray_hpp */
