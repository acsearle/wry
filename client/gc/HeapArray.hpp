//
//  wry/gc/HeapArray.hpp
//  client
//
//  Created by Antony Searle on 19/6/2024.
//

#ifndef wry_gc_HeapArray_hpp
#define wry_gc_HeapArray_hpp

#include "utility.hpp"
#include "value.hpp"

namespace wry::gc {
    
    struct HeapArray : Object {
        
        struct InnerArray {
            Traced<Value>* _begin;
            Traced<Value>* _end;
            Traced<Value>* _capacity;
            Traced<IndirectFixedCapacityValueArray*> _manager;
            InnerArray();
            InnerArray(InnerArray&&);
            InnerArray& operator=(InnerArray&&);
            
            bool full() const;
            bool empty() const;
            size_t size() const;
            Traced<Value>& back();
            const Traced<Value>& back() const;
            Traced<Value>& front();
            const Traced<Value>& front() const;
            void push_back(Value);
            void pop_back();
            void push_front(Value);
            void clear();

        };
        
        InnerArray _alpha;
        InnerArray _beta;
                
        HeapArray();

        const Traced<Value>& operator[](size_t) const;
        Traced<Value>& operator[](size_t);

        Traced<Value>& front();
        const Traced<Value>& front() const;
        Traced<Value>& back();
        const Traced<Value>& back() const;

        bool empty() const;
        size_t size() const;

        void clear();
        Value insert_or_assign(Value key, Value value);
        void push_back(Value);
        void pop_back();
                
        Value find(Value key) const;
        
        void _tax();
        
    }; // struct HeapArray
    
    
    inline HeapArray::InnerArray::InnerArray()
    : _begin(nullptr)
    , _end(nullptr)
    , _capacity(nullptr)
    , _manager(nullptr) {
    }

    inline HeapArray::InnerArray::InnerArray(InnerArray&& other)
    : _begin(exchange(other._begin, nullptr))
    , _end(exchange(other._end, nullptr))
    , _capacity(exchange(other._capacity, nullptr))
    , _manager(exchange(other._manager, nullptr)) {
    }
    
    inline HeapArray::InnerArray& HeapArray::InnerArray::operator=(InnerArray&& other) {
        _begin = exchange(other._begin, nullptr);
        _end = exchange(other._end, nullptr);
        _capacity = exchange(other._capacity, nullptr);
        _manager = exchange(other._manager, nullptr);
        return *this;
    }
    
    inline bool HeapArray::InnerArray::empty() const {
        return _begin == _end;
    }

    inline bool HeapArray::InnerArray::full() const {
        return _end == _capacity;
    }
    
    inline size_t HeapArray::InnerArray::size() const {
        return _end - _begin;
    }
    
    inline const Traced<Value>& HeapArray::InnerArray::back() const {
        assert(!empty());
        return *(_end - 1);
    }

    inline Traced<Value>& HeapArray::InnerArray::back() {
        assert(!empty());
        return *(_end - 1);
    }
    
    inline const Traced<Value>& HeapArray::InnerArray::front() const {
        assert(!empty());
        return *_begin;
    }

    inline Traced<Value>& HeapArray::InnerArray::front() {
        assert(!empty());
        return *_begin;
    }


    inline void HeapArray::InnerArray::pop_back() {
        assert(!empty());
        if (full())
            --_capacity; // full is sticky
        *--_end = value_make_null();
    }
            
    inline void HeapArray::InnerArray::push_back(Value x) {
        assert(!full());
        *_end++ = x;
    }
    
    inline void HeapArray::InnerArray::push_front(Value x) {
        assert(_begin != _manager->_storage);
        *--_begin = x;
    }

    inline void HeapArray::InnerArray::clear() {
        _begin = nullptr;
        _end = nullptr;
        _capacity = nullptr;
        _manager = nullptr;
    }


    inline HeapArray::HeapArray()
    : Object(Class::ARRAY) {
    }

    inline void HeapArray::_tax() {
        assert(!_beta.full());
        if (!_alpha.empty()) {
            Value x = _alpha.back();
            _beta.push_front(x);
            _alpha.pop_back();
        }
        if (_alpha.empty())
            _alpha = std::move(_beta);
    }
    
    inline void HeapArray::pop_back() {
        if (_beta._begin) {
            _beta.pop_back();
            _tax();
        } else {
            _alpha.pop_back();
        }
    }
    
    inline void HeapArray::push_back(Value x) {
        if (!_alpha.full()) {
            _alpha.push_back(x);
            return;
        }
        if (!_beta._begin) {
            size_t n = 8;
            if (_alpha._manager)
                n = max(n, _alpha._manager->_capacity * 2);
            _beta._manager = new IndirectFixedCapacityValueArray(n);
            _beta._begin = _beta._manager->_storage;
            _beta._end = _beta._begin + _alpha.size();
            _beta._capacity = _beta._begin + _beta._manager->_capacity;
            _beta._begin = _beta._end;
        }
        _beta.push_back(x);
        _tax();
    }
    
    inline const Traced<Value>& HeapArray::operator[](size_t i) const {
        if (i < _alpha.size())
            return _alpha._begin[i];
        i -= _alpha.size();
        if (i < _beta.size())
            return _beta._begin[i];
        abort();
    }

    inline Traced<Value>& HeapArray::operator[](size_t i) {
        if (i < _alpha.size())
            return _alpha._begin[i];
        i -= _alpha.size();
        if (i < _beta.size())
            return _beta._begin[i];
        abort();
    }

    inline Traced<Value>& HeapArray::front() {
        if (!_alpha.empty())
            return _alpha.front();
        return _beta.front();
    }

    inline const Traced<Value>& HeapArray::front() const {
        if (!_alpha.empty())
            return _alpha.front();
        return _beta.front();
    }
    
    inline const Traced<Value>& HeapArray::back() const {
        if (_beta._begin)
            return _beta.back();
        return _alpha.back();
    }
    inline Traced<Value>& HeapArray::back() {
        if (_beta._begin)
            return _beta.back();
        return _alpha.back();
    }

    inline bool HeapArray::empty() const {
        return _alpha.empty();
    }

    inline size_t HeapArray::size() const {
        return _alpha.size() + _beta.size();
    }
            
    inline Value HeapArray::insert_or_assign(Value key, Value value) {
        if (_value_is_small_integer(key)) {
            int64_t i = _value_as_small_integer(key);
            operator[](i) = value;
        }
        return value_make_error();
    }
    
    inline Value HeapArray::find(Value key) const {
        if (_value_is_small_integer(key)) {
            int64_t i = _value_as_small_integer(key);
            if ((0 <= i) && (i < size())) {
                return operator[](i);
            }
        }
        return value_make_error();
    }
    
    inline void HeapArray::clear() {
        _alpha.clear();
        _beta.clear();
    }
    
} // namespace wry::gc

#endif /* wry_gc_HeapArray_hpp */
