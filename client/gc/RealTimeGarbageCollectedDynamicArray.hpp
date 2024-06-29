//
//  wry/RealTimeGarbageCollectedDynamicArray.hpp
//  client
//
//  Created by Antony Searle on 19/6/2024.
//

#ifndef wry_RealTimeGarbageCollectedDynamicArray_hpp
#define wry_RealTimeGarbageCollectedDynamicArray_hpp

#include "utility.hpp"
#include "object.hpp"

namespace wry::gc {
    
    // A managed allocation, presented as an array
        
    template<typename T>
    struct GarbageCollectedIndirectStaticArray : Object {
        
        T* const _data;
        const size_t _size;
        
        explicit GarbageCollectedIndirectStaticArray(size_t elements)
        : _data(calloc(elements, sizeof(T)))
        , _size(elements) {
            assert(_data);
            assert(std::has_single_bit(elements));
            // std::uninitialized_default_construct_n(_data, _size);
        }
        
        explicit GarbageCollectedIndirectStaticArray(size_t elements, auto&& first)
        : _data(calloc(elements, sizeof(T)))
        , _size(elements) {
            assert(std::has_single_bit(elements));
            // std::uninitialized_copy_n(first, _size, _data);
        }
        
        virtual ~GarbageCollectedIndirectStaticArray() override {
            std::destroy_n(_data, _size);
            Object::operator delete(_data);
            auto sv = type_name<T>();
            printf("~GarbageCollectedIndirectStaticArray<%.*s>[%zd]\n", (int)sv.size(), (const char*)sv.data(), _size);
        }
        
        size_t size() const { return _size; }
        
        const T* begin() const { return _data; }
        T* begin() { return _data; }
        
        const T* end() const { return _data + _size; }
        T* end() { return _data + _size; }
        
        virtual void _object_scan() const override {
            for (const T& element : _data)
                object_trace(element);
        }
        
    };
    
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
            // std::uninitialized_default_construct_n(_data, _size);
        }

        explicit GarbageCollectedFlexibleArrayMemberStaticArray(size_t elements, auto&& first)
        : _size(elements) {
            // std::uninitialized_copy_n(first, _size, _data);
        }

        virtual ~GarbageCollectedFlexibleArrayMemberStaticArray() override {
            // std::destroy_n(_data, _size);
            auto sv = type_name<T>();
            printf("~GarbageCollectedFlexibleArrayMemberStaticArray<%.*s>[%zd]\n", (int)sv.size(), (const char*)sv.data(), _size);
        }
        
        size_t size() const { return _size; }
        const T* begin() const { return _data; }
        T* begin() { return _data; }
        const T* end() const { return _data + _size; }
        T* end() { return _data + _size; }
        
        virtual void _object_scan() const override {
            for (const T& element : *this)
                object_trace(element);
        }
        
    };
    

    // DynamicArray is a garbage-collected version of the conventional
    // std::vector
    //
    // Like std::vector, some operations are amortized O(1) and it is thus not
    // suitable for soft real time.
    
    template<typename T>
    struct GarbageCollectedDynamicArray {
        
        T* _begin;
        T* _end;
        T* _capacity;
        Traced<GarbageCollectedFlexibleArrayMemberStaticArray<T>*> _storage;
        
        GarbageCollectedDynamicArray()
        : _begin(nullptr)
        , _end(nullptr)
        , _capacity(nullptr)
        , _storage(nullptr) {}
        
        GarbageCollectedDynamicArray(GarbageCollectedDynamicArray&& other)
        : _begin(exchange(other._begin, nullptr))
        , _end(exchange(other._end, nullptr))
        , _capacity(exchange(other._capacity, nullptr))
        , _storage(std::move(other._storage)) {
        }
        
        void swap(GarbageCollectedDynamicArray& other) {
            using std::swap;
            swap(_begin, other._begin);
            swap(_end, other._end);
            swap(_capacity, other._capacity);
            swap(_storage, other._storage);
        }
        
        GarbageCollectedDynamicArray& operator=(GarbageCollectedDynamicArray&& other) {
            _begin = exchange(other._begin, nullptr);
            _end = exchange(other._end, nullptr);
            _capacity = exchange(other._capacity, nullptr);
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
        
        template<typename V>
        void push_back(V&& value) {
            if (full())
                _grow();
            *_end++ = std::forward<V>(value);
        }
        
        void pop_back() {
            if (empty())
                abort();
            --_end;
            *_end = T();
        }
        
        void _pop_front() {
            if (empty())
                abort();
            *_begin = T();
            ++_begin;
        }
        
        template<typename V>
        void _push_front(V&& value) {
            if (!_storage || _begin == _storage->_begin)
                abort();
            *--_begin = std::forward<V>(value);
        }
                
        void clear() {
            for (auto& element : *this)
                element = T();
            _end = _begin;
        }
        
        void shrink_to_fit() {
            _install(GarbageCollectedFlexibleArrayMemberStaticArray<T>::with_exactly(size()));
        }
        
        void reserve(size_t n) {
            if (n > capacity()) {
                _install(GarbageCollectedFlexibleArrayMemberStaticArray<T>::with_at_least(n));
            }
        }
        
    };
    
    template<typename T>
    void swap(GarbageCollectedDynamicArray<T>& a, GarbageCollectedDynamicArray<T>& b) {
        a.swap(b);
    }
    
    template<typename T>
    void object_shade(const GarbageCollectedDynamicArray<T>& self) {
        object_shade(self._storage);
    }

    template<typename T>
    void object_trace(const GarbageCollectedDynamicArray<T>& self) {
        object_trace(self._storage);
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
        
        enum State {
            INITIAL,
            NORMAL,
            RESIZING,
        };

        GarbageCollectedDynamicArray<T> _alpha;
        GarbageCollectedDynamicArray<T> _beta;
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
                        _alpha.reserve(1);
                        _state = NORMAL;
                        break;
                    }
                    case NORMAL: {
                        if (!_alpha.full()) {
                            _alpha.push_back(std::move(value));
                            return;
                        }
                        assert(_beta.empty());
                        _beta.reserve(_alpha.size() * 2);
                        _beta._begin += _alpha.size();
                        _beta._end = _beta._begin;
                        _state = RESIZING;
                        break;
                    }
                    case RESIZING: {
                        assert(!_beta.full());
                        _alpha.push_back(std::move(value));
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
    
    


    
    

    
} // namespace wry::gc

#endif /* wry_RealTimeGarbageCollectedDynamicArray_hpp */
