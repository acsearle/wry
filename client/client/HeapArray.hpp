//
//  wry/HeapArray.hpp
//  client
//
//  Created by Antony Searle on 19/6/2024.
//

#ifndef wry_HeapArray_hpp
#define wry_HeapArray_hpp

#include <bit>

#include "assert.hpp"
#include "garbage_collected.hpp"
#include "memory.hpp"
#include "Scan.hpp"
#include "utility.hpp"
//#include "adl.hpp"

namespace wry {
    
    template<typename T> // requires(std::has_single_bit(sizeof(T)))
    struct ArrayStaticIndirect : GarbageCollected {
        
        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = value_type*;
        using const_pointer = const value_type*;

        T* const _data;
        size_t const _size;
        
        void _invariant() {
            assert(_data);
            assert(std::has_single_bit(_size));
        }
        
        explicit ArrayStaticIndirect(size_t n)
        : _data((T*) calloc(n, sizeof(T)))
        , _size(n) {
            std::uninitialized_default_construct_n(_data, n);
            _invariant();
        }
        
        virtual ~ArrayStaticIndirect() override {
            // std::string_view sv = name_of<T>;
            // printf("~ArrayStaticIndirect<%.*s>(%zd)\n", (int)sv.size(), (const char*)sv.data(), _size);
            // std::destroy_n(_data, _size);
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
        const T& front() const { return *_data; }
        
        T& back() { return _data[_size - 1]; }
        const T& back() const { return _data[_size - 1]; }
        
        T& operator[](size_t i) {
            assert(i < _size);
            return _data[i];
        }
        
        const T& operator[](size_t i) const {
            assert(i < _size);
            return _data[i];
        }
        
        virtual void _garbage_collected_scan() const override {
            for (const T& element : *this) {
                garbage_collected_scan(element);
            }
        }
        
        virtual void _garbage_collected_debug() const override {
            std::string_view sv = name_of<T>;
            printf("ArrayStaticIndirect<%.*s>(%zd){ ", (int)sv.size(), (const char*)sv.data(), _size);
            for (const T& element : *this) {
                any_debug(element);
                printf(", ");
            }
            printf("}");
        }
        
    };
    
    template<typename T>
    struct RingBufferView {
        
        T* _data;
        size_t _capacity;
        
        RingBufferView()
        : _data(nullptr)
        , _capacity(0) {
        }
        
        RingBufferView(const RingBufferView&) = delete;
        
        RingBufferView(RingBufferView&& other)
        : _data(exchange(other._data, nullptr))
        , _capacity(exchange(other._capacity, 0)) {
        }
        
        ~RingBufferView() = default;
        
        RingBufferView& operator=(const RingBufferView&) = delete;

        RingBufferView& operator=(RingBufferView&& other) {
            _data = exchange(other._data, nullptr);
            _capacity = exchange(other._capacity, 0);
            return *this;
        }
        
        size_t _mask(size_t i) const {
            return i & (_capacity - 1);
        }
        
        size_t capacity() const {
            return _capacity;
        }
                
        T& operator[](size_t i) {
            return _data[_mask(i)];
        }
        
        const T& operator[](size_t i) const {
            return _data[_mask(i)];
        }

    };

    template<typename T>
    struct RingDequeStatic {
        RingBufferView<T> _inner;
        size_t _begin = 0;
        size_t _end = 0;
        Scan<ArrayStaticIndirect<T>*> _storage;
        
        RingDequeStatic() 
        : _inner()
        , _begin(0)
        , _end(0)
        , _storage(nullptr) {
        }
        
        RingDequeStatic(RingDequeStatic&& other)
        : _inner(std::move(other._inner))
        , _begin(exchange(other._begin, 0))
        , _end(exchange(other._end, 0)) 
        , _storage(std::move(other._storage)) {
        }
        
        RingDequeStatic& operator=(RingDequeStatic&& other) {
            _inner = std::move(other._inner);
            _begin = exchange(other._begin, 0);
            _end = exchange(other._end, 0);
            _storage = std::move(other._storage);
            return *this;
        }
                
        size_t capacity() const {
            return _inner._capacity;
        }
                
        size_t size() const {
            return _end - _begin;
        }

        bool empty() const {
            return _begin == _end;
        }

        bool full() const {
            return _end - _begin == capacity();
        }

        T& front() {
            assert(!empty());
            return _inner[_begin];
        }
        
        T& back() {
            assert(!empty());
            return _inner[_end - 1];
        }
        
        T& operator[](size_t i) {
            assert(_begin <= i);
            assert(i < _end);
            return _inner[i];
        }
        
        void pop_front() {
            assert(!empty());
            garbage_collected_passivate(front());
            ++_begin;
        }
        
        void pop_back() {
            assert(!empty());
            garbage_collected_passivate(back());
            --_end;
        }
        
        template<typename U>
        void push_front(U&& value) {
            assert(!full());
            _inner[_begin - 1] = std::forward<U>(value);
            --_begin;
        }

        template<typename U>
        void push_back(U&& value) {
            assert(!full());
            _inner[_end] = std::forward<U>(value);
            ++_end;
        }

    };
   
    template<typename T>
    void garbage_collected_scan(const RingDequeStatic<T>& self) {
        garbage_collected_scan(self._storage);
    }

    template<typename T>
    void garbage_collected_shade(const RingDequeStatic<T>& self) {
        garbage_collected_shade(self._storage);
    }

    
    
    template<typename T>
    struct GCArray {
        mutable RingDequeStatic<T> _alpha;
        mutable RingDequeStatic<T> _beta;
                
        void _tax_front() const {
            if (!_beta.empty()) {
                _alpha[_beta._begin] = std::move(_beta.front());
                _beta.pop_front();
                if (_beta.empty())
                    _beta._storage = nullptr;
            }
        }
        
        void _tax_back() const {
            if (!_beta.empty()) {
                _alpha[_beta._end - 1] = std::move(_beta.back());
                _beta.pop_back();
                if (_beta.empty())
                    _beta._storage = nullptr;
            }
        }
        
        void _ensure_nonfull() const{
            if (_alpha.full()) {
                assert(_beta.empty());
                _beta._inner._data = _alpha._inner._data;
                _beta._inner._capacity = _alpha._inner._capacity;
                _beta._storage = std::move(_alpha._storage);
                _beta._begin = _alpha._begin;
                _beta._end = _alpha._end;
                _alpha._inner._capacity = max(_alpha._inner._capacity << 1, 1);
                auto p = new ArrayStaticIndirect<T>(_alpha._inner._capacity);
                _alpha._inner._data = p->_data;
                _alpha._storage = p;
            }
        }
        
        bool empty() const {
            return _alpha.empty();
        }
        
        size_t size() const {
            return _alpha.size();
        }
        
        T& front() const {
            _tax_front();
            return _alpha.front();
        }
        
        T& back() const {
            _tax_back();
            return _alpha.back();
        }
        
        void pop_front() {
            _tax_front();
            _alpha.pop_front();
        }
        
        void pop_back() {
            _tax_back();
            _alpha.pop_back();
        }
        
        template<typename U>
        void push_front(U&& value) {
            _tax_back();
            _ensure_nonfull();
            _alpha.push_front(std::forward<U>(value));
        }

        template<typename U>
        void push_back(U&& value) {
            _tax_front();
            _ensure_nonfull();
            _alpha.push_back(std::forward<U>(value));
        }
        
        T& operator[](size_t i) {
            _tax_front();
            assert(_alpha._begin <= i);
            assert(i < _alpha._end);
            if (i < _beta._begin || i >= _beta._end)
                return _alpha[i];
            else
                return _beta[i];
        }

        const T& operator[](size_t i) const {
            _tax_front();
            assert(_alpha._begin <= i);
            assert(i < _alpha._end);
            if (i < _beta._begin || i >= _beta._end)
                return _alpha[i];
            else
                return _beta[i];
        }

        void clear() {
            _alpha._begin = _alpha._end;
            _beta._begin = _beta._end;
            _beta._storage = nullptr;
        }

    };
    
    template<typename T>
    void garbage_collected_scan(const GCArray<T>& self) {
        garbage_collected_scan(self._alpha);
        garbage_collected_scan(self._beta);
    }

    template<typename T>
    void garbage_collected_shade(const GCArray<T>& self) {
        garbage_collected_shade(self._alpha);
        garbage_collected_shade(self._beta);
    }

    
    static_assert(std::is_move_assignable_v<GCArray<Scan<GarbageCollected*>>>);
        
} // namespace wry

#endif /* wry_HeapArray_hpp */
