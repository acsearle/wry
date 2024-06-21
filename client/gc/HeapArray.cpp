//
//  wry/gc/array.cpp
//  client
//
//  Created by Antony Searle on 19/6/2024.
//

#include "HeapArray.hpp"

#include "gc.hpp"
#include "test.hpp"

namespace wry::gc {
    
    
    HeapArray::InnerArray::InnerArray()
    : _begin(nullptr)
    , _end(nullptr)
    , _capacity(nullptr)
    , _manager(nullptr) {
    }
    
    HeapArray::InnerArray::InnerArray(InnerArray&& other)
    : _begin(exchange(other._begin, nullptr))
    , _end(exchange(other._end, nullptr))
    , _capacity(exchange(other._capacity, nullptr))
    , _manager(exchange(other._manager, nullptr)) {
    }
    
    void HeapArray::InnerArray::swap(InnerArray& other) {
        using std::swap;
        swap(_begin, other._begin);
        swap(_end, other._end);
        swap(_capacity, other._capacity);
        swap(_manager, other._manager);
    }
    
    HeapArray::InnerArray& HeapArray::InnerArray::operator=(InnerArray&& other) {
        _begin = exchange(other._begin, nullptr);
        _end = exchange(other._end, nullptr);
        _capacity = exchange(other._capacity, nullptr);
        _manager = exchange(other._manager, nullptr);
        return *this;
    }
    
    bool HeapArray::InnerArray::empty() const {
        return _begin == _end;
    }
    
    bool HeapArray::InnerArray::full() const {
        return _end == _capacity;
    }
    
    size_t HeapArray::InnerArray::size() const {
        return _end - _begin;
    }
    
    const Traced<Value>& HeapArray::InnerArray::back() const {
        assert(!empty());
        return *(_end - 1);
    }
    
    Traced<Value>& HeapArray::InnerArray::back() {
        assert(!empty());
        return *(_end - 1);
    }
    
    const Traced<Value>& HeapArray::InnerArray::front() const {
        assert(!empty());
        return *_begin;
    }
    
    Traced<Value>& HeapArray::InnerArray::front() {
        assert(!empty());
        return *_begin;
    }
    
    
    void HeapArray::InnerArray::pop_back() {
        assert(!empty());
        // We overwrite to prevent floating garbage
        *--_end = value_make_null();
    }
    
    void HeapArray::InnerArray::push_back(Value x) {
        assert(!full());
        *_end++ = x;
    }
    
    void HeapArray::InnerArray::push_front(Value x) {
        assert(_begin != _manager->_storage);
        *--_begin = x;
    }
    
    void HeapArray::InnerArray::clear() {
        _begin = nullptr;
        _end = nullptr;
        _capacity = nullptr;
        _manager = nullptr;
    }
    
    void HeapArray::InnerArray::reserve(size_t n) {
        assert(empty() && n);
        if (!_manager || _manager->_capacity < n) {
            _manager = new IndirectFixedCapacityValueArray(n);
        }
        _begin = _manager->_storage;
        _end = _begin;
        _capacity = _begin + _manager->_capacity;
    }
    
    
    HeapArray::HeapArray()
    : Object(Class::ARRAY)
    , _state(INITIAL) {
    }
    
    
    void HeapArray::push_back(Value x) {
        for (;;) {
            switch (_state) {
                case INITIAL: {
                    assert(_alpha.empty());
                    assert(_beta.empty());
                    _alpha.reserve(16);
                    _state = NORMAL;
                    break;
                }
                case NORMAL: {
                    if (!_alpha.full()) {
                        _alpha.push_back(x);
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
                    _beta.push_back(x);
                    assert(!_alpha.empty());
                    Value y = _alpha.back();
                    _alpha.pop_back();
                    _beta.push_front(y);
                    if (_alpha.empty()) {
                        _alpha.swap(_beta);
                        _state = NORMAL;
                    }
                    return;
                }
            }
        }
    }
    
    void HeapArray::pop_back() {
        switch (_state) {
            case INITIAL:
                abort();
            case NORMAL:
                _alpha.pop_back();
                break;
            case RESIZING:
                assert(!_alpha.empty());
                assert(!_beta.empty());
                _beta.pop_back();
                Value y = _alpha.back();
                _alpha.pop_back();
                _beta.push_front(y);
                if (_alpha.empty()) {
                    _alpha.swap(_beta);
                    _state = NORMAL;
                }
                break;
        }
    }
    

    const Traced<Value>& HeapArray::operator[](size_t i) const {
        if (i < _alpha.size())
            return _alpha._begin[i];
        i -= _alpha.size();
        if (i < _beta.size())
            return _beta._begin[i];
        abort();
    }
    
    Traced<Value>& HeapArray::operator[](size_t i) {
        if (i < _alpha.size())
            return _alpha._begin[i];
        i -= _alpha.size();
        if (i < _beta.size())
            return _beta._begin[i];
        abort();
    }
    
    Traced<Value>& HeapArray::front() {
        return _alpha.front();
    }
    
    const Traced<Value>& HeapArray::front() const {
        return _alpha.front();
    }
    
    const Traced<Value>& HeapArray::back() const {
        switch (_state) {
            case INITIAL:
                abort();
            case NORMAL:
                return _alpha.back();
            case RESIZING:
                return _beta.back();
        }
    }
    Traced<Value>& HeapArray::back() {
        switch (_state) {
            case INITIAL:
                abort();
            case NORMAL:
                return _alpha.back();
            case RESIZING:
                return _beta.back();
        }
    }
    
    bool HeapArray::empty() const {
        return _alpha.empty();
    }
    
    size_t HeapArray::size() const {
        return _alpha.size() + _beta.size();
    }
    
    Value HeapArray::insert_or_assign(Value key, Value value) {
        if (_value_is_small_integer(key)) {
            int64_t i = _value_as_small_integer(key);
            operator[](i) = value;
        }
        return value_make_error();
    }
    
    Value HeapArray::find(Value key) const {
        if (_value_is_small_integer(key)) {
            int64_t i = _value_as_small_integer(key);
            if ((0 <= i) && (i < size())) {
                return operator[](i);
            }
        }
        return value_make_error();
    }
    
    void HeapArray::clear() {
        _alpha.clear();
        _beta.clear();
    }
    
    define_test("HeapArray") {
        
        mutator_enter();
        
        HeapArray* a = new HeapArray();
        assert(a->empty() == true);
        assert(a->size() == 0);
        
        for (int i = 0; i != 100; ++i) {
            assert(a->empty() == !i);
            assert(a->size() == i);
            a->push_back(i);
            assert(a->size() == i + 1);
            assert(a->back() == i);
            assert(a->front() == 0);
        }
        
        for (int i = 100; i--;) {
            assert(a->empty() == false);
            assert(a->size() == i+1);
            assert(a->back() == i);
            assert(a->front() == 0);
            a->pop_back();
            assert(a->size() == i);
        }
        assert(a->empty() == true);
        assert(a->size() == 0);

        mutator_leave();
        
    };
    
}

