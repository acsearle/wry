//
//  value.cpp
//  client
//
//  Created by Antony Searle on 31/5/2024.
//

#include <cinttypes>
#include <cstdlib>

#include <algorithm>
#include <numeric>
#include <random>

#include "hash.hpp"
#include "HeapArray.hpp"
#include "HeapTable.hpp"
#include "utility.hpp"
#include "value.hpp"
#include "HeapString.hpp"
#include "debug.hpp"


namespace wry {
    
    std::string_view _value_as_short_string(const Value& self) {
        assert(_value_is_short_string(self));
        return ((const _short_string_t&)self._data).as_string_view();
    }
    
    Value& operator++(Value& self) {
        self += 1;
        return self;
    }

    Value& operator--(Value& self) {
        self -= 1;
        return self;
    }

    Value operator++(Value& self, int) {
        Value old = self;
        ++self;
        return old;
    }

    Value operator--(Value& self, int) {
        Value old = self;
        ++self;
        return old;
    }

#define X(Y)\
    Value& operator Y##=(Value& self, const Value& other) {\
        return self = self Y other;\
    }
    
    X(*)
    X(/)
    X(%)
    X(-)
    X(+)
    X(&)
    X(^)
    X(|)
    X(<<)
    X(>>)
    
#undef X
    
#define X(Y)\
    Value operator Y (int64_t left, const Value& right) {\
        switch (_value_tag(right)) {\
            case VALUE_TAG_SMALL_INTEGER:\
                return value_make_integer_with(left Y _value_as_small_integer(right));\
            default:\
                return value_make_error();\
        }\
    }\
    \
    Value operator Y (const Value& left, const Value& right) {\
        switch (_value_tag(left)) {\
            case VALUE_TAG_SMALL_INTEGER:\
                return _value_as_small_integer(left) Y right;\
            default:\
                return value_make_error();\
        }\
    }
        
    X(*)
    X(/)
    X(%)
    X(-)
    X(+)
    X(&)
    X(^)
    X(|)
    X(<<)
    X(>>)
    
#undef X
    
    size_t hash(const Value& self) {
        switch (_value_tag(self)) {
            case VALUE_TAG_OBJECT: {
                const GarbageCollected* object = _value_as_object(self);
                return object ? hash(object) : 0;
            }
            case VALUE_TAG_SMALL_INTEGER: {
                std::int64_t a = _value_as_small_integer(self);
                return hash(a);
            }
            case VALUE_TAG_SHORT_STRING: {
                return std::hash<std::string_view>()(_value_as_short_string(self));
            }
            case VALUE_TAG_BOOLEAN: {
                return std::hash<bool>()(value_as_boolean(self));
            }
            default:
                abort();
        }
    }
    
    Value value_make_integer_with(std::int64_t z) {
        Value result;
        std::int64_t y = z << 4;
        if ((y >> 4) == z) {
            result._data = y | VALUE_TAG_SMALL_INTEGER;
        } else {
            result._data = (uint64_t)new HeapInt64(z);
        }
        return result;
    }
    
    Value value_make_zero() {
        return value_make_integer_with(0);
    }

    Value value_make_one() {
        return value_make_integer_with(1);
    }

    Value value_make_string_with(const char* ntbs) {
        Value result;
        std::size_t n = std::strlen(ntbs);
        if (n < 8) {
            _short_string_t s = {};
            s._tag_and_len = (n << 4) | VALUE_TAG_SHORT_STRING;
            __builtin_memcpy(s._chars, ntbs, n);
            __builtin_memcpy(&result, &s, 8);
            assert(_value_is_short_string(result));
        } else {
            result._data = (uint64_t)HeapString::make(std::string_view(ntbs, n));
            assert(_value_is_object(result));
        }
        return result;
    }
    
    Value _value_make_garbage_collected_with(const GarbageCollected* object) {
        Value result;
        result._data = (uint64_t)object;
        assert(_value_is_object(result));
        return result;
    }
    
        
    
    
    
    
    
    
    
    
    
    
    
  
    
      
    void foo() {
        
        Value t = value_make_table();
        
        if (!(::rand() % 100)) {
            Value("This weak-cached string should be occasionally collected");
        }
        
        assert(value_size(t) == 0);
        assert(!value_contains(t, "a"));
        assert(value_find(t, "a") == value_make_null());
        value_insert_or_assign(t, "a", "A");
        assert(value_size(t) == 1);
        assert(value_contains(t, "a"));
        assert(value_find(t, "a") == "A");
        assert(value_insert_or_assign(t, "a", "A2") == "A");
        assert(value_size(t) == 1);
        assert(value_contains(t, "a"));
        assert(value_find(t, "a") == "A2");
        value_erase(t, "a");
        assert(value_size(t) == 0);
        assert(!value_contains(t, "a"));
        assert(value_find(t, "a") == value_make_null());
        
        {
            Value k = "very long key";
            Value v = "very long value";
            Value k2 = "another very long key";
            assert(!value_contains(t, k));
            assert(value_find(t, k) == value_make_null());
            assert(value_insert_or_assign(t, k, v) == value_make_null());
            assert(value_contains(t, k));
            assert(value_find(t, k) == v);
            assert(value_erase(t, k));
        }
        
        {
            Value a;
            a = 1;
            assert(a == 1);
            Value b;
            b = 2;
            assert(b == 2);
            Value c;
            c = a + b;
            assert(c == 3);
            /*
            value_debug(a);
            value_debug(b);
            value_debug(c);
             */
        }
        
        
        std::vector<int> v(100);
        std::iota(v.begin(), v.end(), 0);
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(v.begin(), v.end(), g);
        
        for (int i = 0; i != 100; ++i) {
            assert(value_size(t) == i);
            assert(!value_contains(t, v[i]));
            assert(value_is_null(value_find(t, v[i])));
            assert(hash(v[i]) == hash(Value(v[i])));
            assert(value_is_null(value_insert_or_assign(t, v[i], v[i])));
            assert(value_size(t) == i + 1);
            assert(value_contains(t, v[i]));
            assert(value_find(t, v[i]) == v[i]);
        }
        
        std::shuffle(v.begin(), v.end(), g);
        for (int i = 0; i != 100; ++i) {
            assert(value_contains(t, v[i]));
            assert(value_find(t, v[i]) == v[i]);
            assert(!value_contains(t, v[i] + 100));
            assert(value_is_null(value_find(t, v[i] + 100)));
        }

        for (int i = 0; i != 100; ++i) {
            assert(value_contains(t, v[i]));
            assert(value_find(t, v[i]) == v[i]);
            assert(value_erase(t, v[i])== v[i]);
            assert(value_contains(t, v[i]) == false);
            assert(value_is_null(value_find(t, v[i])));
        }
        
        assert(value_size(t) == 0);

        std::shuffle(v.begin(), v.end(), g);
        
        Value s = t;
        for (int i = 0; i != 100; ++i) {
            assert(value_size(s) == i);
            assert(!value_contains(s, v[i]));
            assert(s[v[i]] == value_make_null());
            // t._pointer->_invariant();
            s[v[i]] = v[i];
            // t._pointer->_invariant();
            assert(value_size(t) == i + 1);
            assert(value_contains(t, v[i]));
            assert(s[v[i]] == v[i]);
        }

    }
     
    
    
    
    
    Value Scan<Value>::get() const {
        return _atomic_value.load(Ordering::RELAXED);
    }
        
    Scan<Value>::Scan(const Value& value) 
    : _atomic_value(value) {
    }
    
    Scan<Value>::Scan(const Scan<Value>& value)
    : Scan(value.get()) {
    }
        

    Scan<Value>& Scan<Value>::operator=(const Value& desired) {
        Value discovered = this->_atomic_value.exchange(desired, Ordering::RELEASE);
        garbage_collected_shade(desired);
        garbage_collected_shade(discovered);
        return *this;
    }

    Scan<Value>& Scan<Value>::operator=(const Scan<Value>& desired) {
        return this->operator=(desired.get());
    }
    
    Scan<Value>::operator bool() const {
        return get().operator bool();
    }
    
    Scan<Value>::operator Value() const {
        return get();
    }
    
   
    
    bool operator==(const Value& a, const Value& b) {
        // POINTER: identity; requires interned bigstrings, bignums etc.
        //    - Containers are by identity
        //    - Identity of empty containers?
        // INLINE: requires that we make padding bits consistent
        return a._data == b._data;
    }
    
    
   

    bool contains(const HeapValue* self, Value key) {
        return self ? self->_value_contains(key) : false;
    }

    Value find(const HeapValue* self, Value key) {
        return self ? self->_value_find(key) : value_make_error();
    }

    Value insert_or_assign(HeapValue* self, Value key, Value value) {
        return self ? self->_value_insert_or_assign(key, value) : value_make_error();
    }

    Value erase(HeapValue* self, Value key) {
        return self ? self->_value_erase(key) : value_make_error();
    }

    size_t size(const HeapValue* self) {
        return self ? self->_value_size() : 0;
    }
    
    size_t value_size(const Value& self) {
        switch (_value_tag(self)) {
            case VALUE_TAG_OBJECT:
                return self._data ? size(_value_as_object(self)) : 0;
            case VALUE_TAG_SHORT_STRING:
                return (self._data >> 4) & 7;
            default:
                return 0;
        }
    }
    
    bool value_contains(const Value& self, Value key) {
        switch (_value_tag(self)) {
            case VALUE_TAG_OBJECT:
                return self._data && contains(_value_as_object(self), key);
            default:
                return false;
        }
    }
    
    
    _value_subscript_result_t Value::operator[](Value key) {
        return {*this, key};
    }
    
    
    void object_shade(const Value& value) {
        if (_value_is_object(value)) {
            object_shade(_value_as_object(value));
        }
    }
    
    
    
    _value_subscript_result_t::operator Value() && {
        return find(_value_as_object(container), key);
    }
    
    _value_subscript_result_t&& _value_subscript_result_t::operator=(Value desired) && {
        insert_or_assign(_value_as_object(container), key, desired);
        return std::move(*this);
    }
    
    _value_subscript_result_t&& _value_subscript_result_t::operator=(_value_subscript_result_t&& desired) && {
        return std::move(*this).operator=((Value)std::move(desired));
    }

    

    
   

    
    
    void* HeapString::operator new(std::size_t self, std::size_t extra) {
        return GarbageCollected::operator new(self + extra);
    }
        
    const HeapString* HeapString::make(std::string_view view) {
        return make(std::hash<std::string_view>()(view), view);
    }
    
    std::string_view HeapString::as_string_view() const {
        return std::string_view(_bytes, _size);
    }
    
    
    
    
    HeapInt64::HeapInt64(std::int64_t z)
    : _integer(z) {
    }

    std::int64_t HeapInt64::as_int64_t() const {
        return _integer;
    }
        
    HeapString::HeapString() {
    }
    
    Value value_insert_or_assign(Value& self, Value key, Value value) {
        return insert_or_assign(_value_as_object(self), key, value);
    }

    
    Value value_find(const Value& self, Value key) {
        return find(_value_as_object(self), key);
    }
    
    Value value_erase(Value& self, Value key) {
        return erase(_value_as_object(self), key);
    }

        
    void debug(const Value& self) {
        switch (_value_tag(self)) {
            case VALUE_TAG_BOOLEAN:
                return (void)printf("%s\n", value_as_boolean(self) ? "TRUE" : "FALSE");
            case VALUE_TAG_CHARACTER:
                return (void)printf("\'%lc\'\n", value_as_character(self));
            case VALUE_TAG_ERROR:
                return (void)printf("ERROR\n");
            case VALUE_TAG_OBJECT:
                return debug(_value_as_object(self));
            case VALUE_TAG_ENUMERATION: {
                auto [meta, code] = value_as_enum(self);
                return (void)printf("enum{%d, %d}\n", meta, code);
            }
            case VALUE_TAG_SHORT_STRING: {
                auto v = _value_as_short_string(self);
                return (void)printf("\"%.*s\"\n", (int)v.size(), v.data());
            }
            case VALUE_TAG_SMALL_INTEGER:
                return (void)printf("%lld\n", _value_as_small_integer(self));
            case VALUE_TAG_SPECIAL:
                switch (self._data) {
                    case VALUE_DATA_TOMBSTONE:
                        return (void)printf("TOMBSTONE\n");
                    case VALUE_DATA_OK:
                        return (void)printf("OK\n");
                    case VALUE_DATA_NOTFOUND:
                        return (void)printf("NOTFOUND\n");
                    case VALUE_DATA_RESTART:
                        return (void)printf("RESTART\n");
                    default:
                        return (void)printf("SPECIAL{%llx}\n", self._data);
                }
            default:
                return (void)printf("Value{%#0." PRIx64 "}\n", self._data);
        }
    }
    
    void debug(const Scan<Value>& self) {
        debug(self._atomic_value.load(Ordering::ACQUIRE));
    }

    
    
    Value _value_make_with(const GarbageCollected* p) {
        Value result;
        result._data = (uint64_t)p;
        return result;
    }
    
    Value value_make_table() {
        Value result;
        result._data = (uint64_t)(new HeapHashMap);
        return result;
    }
    
    
    
    struct ValueArray : HeapValue {
        
        GCArray<Scan<Value>> _inner;
        
        virtual void _garbage_collected_scan() const { garbage_collected_scan(_inner); }
        virtual void _garbage_collected_debug() const { any_debug(_inner); }
        
        virtual bool _value_empty() const { return _inner.empty(); }
        virtual size_t _value_size() const { return _inner.size(); }

        // bad interface for arrays, but bad enough?
        // Lua notably avoids arrays to provide a table interface for everything
        // deques without indexing may be what we actually want
        virtual Value _value_insert_or_assign(Value key, Value value) {
            int64_t i = key.as_int64_t();
            Value old = value_make_null();
            if (0 <= i && i < _inner.size()) {
                old = _inner[i];
                _inner[i] = value;
            } else if (i == _inner.size()) {
                _inner.push_back(value);
            }
            return old;
        }
        virtual bool _value_contains(Value key) const { return key.as_int64_t() < _inner.size(); }
        virtual Value _value_find(Value key) const { abort(); }
        
    };
    

    Value value_make_array() {
        Value result;
        result._data = (uint64_t)(new ValueArray);
        return result;
    }
    
    
    
    
    
    bool HeapValue::_value_empty() const { abort(); }
    size_t HeapValue::_value_size() const { return 0; }
    bool HeapValue::_value_contains(Value key) const { return false; }
    Value HeapValue::_value_find(Value key) const { return value_make_error(); }
    Value HeapValue::_value_insert_or_assign(Value key, Value value) { return value_make_error(); }
    Value HeapValue::_value_erase(Value key) { return value_make_error(); }
    

    
    Value HeapValue::_value_add(Value right) const { return value_make_error(); }
    Value HeapValue::_value_sub(Value right) const { return value_make_error(); }
    Value HeapValue::_value_mul(Value right) const { return value_make_error(); }
    Value HeapValue::_value_div(Value right) const { return value_make_error(); }
    Value HeapValue::_value_mod(Value right) const { return value_make_error(); }
    Value HeapValue::_value_rshift(Value right) const { return value_make_error(); }
    Value HeapValue::_value_lshift(Value right) const { return value_make_error(); }

    void HeapInt64::_garbage_collected_shade() const {
        abort();
        //Color expected = Color::WHITE;
        //(void) color.compare_exchange(expected, Color::BLACK);
    }
    
    void HeapInt64::_garbage_collected_scan() const {
        fprintf(stderr, "scanned a weak ");
        _garbage_collected_debug();
        abort();
    }


} // namespace wry



