//
//  array.hpp
//  client
//
//  Created by Antony Searle on 19/6/2024.
//

#ifndef array_hpp
#define array_hpp

#include "utility.hpp"
#include "value.hpp"

namespace wry::gc {

    // This HeapArray is notably not real time due to amortized resize
    
    struct HeapArray : Object {
        
        Traced<Value>* _begin;
        Traced<Value>* _end;
        Traced<Value>* _capacity;        
        Traced<IndirectFixedCapacityValueArray*> _manager;
        
        HeapArray();

        Value operator[](size_t) const;
        Traced<Value>& operator[](size_t);

        Value front() const;
        Value back() const;

        bool empty() const;
        size_t size() const;

        void clear();
        Value insert_or_assign(Value key, Value value) const;
        void push_back(Value);
        void pop_back();
                
        Value find(Value key) const;
        
    }; // struct HeapArray
    
    
    inline HeapArray::HeapArray()
    : Object(Class::ARRAY)
    , _begin(nullptr)
    , _end(nullptr)
    , _capacity(nullptr)
    , _manager(nullptr) {
    }
    
    inline Value HeapArray::operator[](size_t i) const {
        return ((i < size())
                ? _begin[i]
                : value_make_error());
    }
    
    inline Traced<Value>& HeapArray::operator[](size_t i) {
        if (i < size())
            return _begin[i];
        abort();
    }
    
    inline Value HeapArray::front() const {
        assert(!empty());
        return *_end;
    }

    inline Value HeapArray::back() const {
        assert(!empty());
        return *(_end - 1);
    }

    inline bool HeapArray::empty() const {
        return _begin == _end;
    }

    inline size_t HeapArray::size() const {
        return _end - _begin;
    }
        
    inline void HeapArray::clear() {
        _end = _begin;
    }
    
    inline Value HeapArray::insert_or_assign(Value key, Value value) const {
        if (_value_is_small_integer(key)) {
            int64_t i = _value_as_small_integer(key);
            if ((0 <= i) && (i < size())) {
                Value y = _begin[i];
                _begin[i] = value;
                return y;
            }
        }
        return value_make_error();
    }
    
    inline void HeapArray::pop_back() {
        assert(!empty());
        --_end;
    }
    
    inline void HeapArray::push_back(Value x) {
        if (_end == _capacity) {
            size_t n = 8;
            if (_manager)
                n = max(n, _manager->_capacity * 2);
            auto p = new IndirectFixedCapacityValueArray(n);
            std::copy(_begin, _end, p->_storage);
            auto m = size();
            _begin = p->_storage;
            _end = p->_storage + m;
            _capacity = p->_storage + p->_capacity;
            _manager = p;
        }
        *_end++ = x;        
    }

    inline Value HeapArray::find(Value key) const {
        if (_value_is_small_integer(key)) {
            int64_t i = _value_as_small_integer(key);
            if ((0 <= i) && (i < size())) {
                return _begin[i];
            }
        }
        return value_make_error();
    }
    
} // namespace wry::gc

#endif /* array_hpp */
