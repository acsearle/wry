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


namespace wry::gc {
    
    std::string_view _value_as_short_string(const Value& self) {
        assert(_value_is_short_string(self));
        return ((const _short_string_t&)self._data).as_string_view();
    }
    
    /*

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
            case VALUE_TAG_OBJECT: {\
                const Object* object = _value_as_object(right);\
                if (object) switch (object->_class) {\
                    case Class::INT64:\
                        return value_make_integer_with(left Y ((HeapInt64*)object)->_integer);\
                    default:\
                        break;\
                }\
            } break;\
            case VALUE_TAG_SMALL_INTEGER:\
                return value_make_integer_with(left Y _value_as_small_integer(right));\
            default:\
                break;\
        }\
        return value_make_error();\
    }\
    \
    Value operator Y (const Value& left, const Value& right) {\
        switch (_value_tag(left)) {\
            case VALUE_TAG_OBJECT: {\
                const Object* object = _value_as_object(left);\
                if (object) switch (object->_class) {\
                    case Class::INT64:\
                        return ((HeapInt64*)object)->_integer Y right;\
                    default:\
                        break;\
                }\
            } break;\
            case VALUE_TAG_SMALL_INTEGER:\
                return _value_as_small_integer(left) Y right;\
            default:\
                break;\
        }\
        return value_make_error();\
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
    
     */
    
    
    size_t value_hash(const Value& self) {
        switch (_value_tag(self)) {
            case VALUE_TAG_OBJECT: {
                const Object* object = _value_as_object(self);
                return object ? object_hash(object) : 0;
            }
            case VALUE_TAG_SMALL_INTEGER: {
                std::int64_t a = _value_as_small_integer(self);
                return wry::hash(a);
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
    
    Value _value_make_object_with(const Object* object) {
        Value result;
        result._data = (uint64_t)object;
        assert(_value_is_object(result));
        return result;
    }
    
        
    
    
    
    
    
    
    

    
    
    /*
    String Object::str() const {
        Value a = Value::from_ntbs("HeapValue");
        String b;
        b._string = a._short_string;
        return b;
    };
     */
    
    
    
    
    
    
    
  
    
      
    void foo() {
        
        Value t = value_make_table();
        
        if (!(::rand() % 100)) {
            Value trap = "trapped string should weak rot";
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
            //c = a + b;
            //assert(c == 3);
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
     
    
    
    
    
    Value Traced<Value>::get() const {
        return _atomic_value.load(Ordering::RELAXED);
    }
        
    Traced<Value>::Traced(const Value& value) 
    : _atomic_value(value) {
    }
    
    Traced<Value>::Traced(const Traced<Value>& value)
    : Traced(value.get()) {
    }
        

    Traced<Value>& Traced<Value>::operator=(const Value& desired) {
        Value discovered = this->_atomic_value.exchange(desired, Ordering::RELEASE);
        value_shade(desired);
        value_shade(discovered);
        return *this;
    }

    Traced<Value>& Traced<Value>::operator=(const Traced<Value>& desired) {
        return this->operator=(desired.get());
    }
    
    Traced<Value>::operator bool() const {
        return get().operator bool();
    }
    
    Traced<Value>::operator Value() const {
        return get();
    }
    
   
    
    bool operator==(const Value& a, const Value& b) {
        // POINTER: identity; requires interned bigstrings, bignums etc.
        //    - Containers are by identity
        //    - Identity of empty containers?
        // INLINE: requires that we make padding bits consistent
        return a._data == b._data;
    }
    
    
    /*
    std::size_t Array::size() const {
        return _array->_size;
    }
    
    Value Array::operator[](std::size_t pos) const {
        assert(pos < _array->_size);
        return _array->_storage[pos].get();
    }
     */

    
    
    /*
    std::size_t String::size() const {
        switch (_discriminant()) {
            case VALUE_TAG_POINTER:
                assert(_pointer);
                return _pointer->_size;
            case VALUE_TAG_SHORT_STRING:
                return (_tag >> 4) & 15;
            default:
                abort();
        }
    }
     */

    bool contains(const Object* self, Value key) {
        return self ? self->_value_contains(key) : false;
        /*
        switch (self->_class) {
            case Class::TABLE:
                return ((const HeapTable*) self)->contains(key);
            default:
                return false;
        }
         */
    }

    Value find(const Object* self, Value key) {
        return self ? self->_value_find(key) : value_make_error();
        /*
        switch (self->_class) {
            case Class::ARRAY:
                return ((HeapArray*) self)->find(key);
            case Class::TABLE:
                return ((const HeapTable*) self)->find(key);
            default:
                return value_make_error();
        }
         */
    }

    Value insert_or_assign(Object* self, Value key, Value value) {
        return self ? self->_value_insert_or_assign(key, value) : value_make_error();
        /*
        switch (self->_class) {
            case Class::ARRAY:
                return ((HeapArray*) self)->insert_or_assign(key, value);
            case Class::TABLE:
                return ((const HeapTable*) self)->insert_or_assign(key, value);
            default:
                return value_make_error();
        }
         */
    }

    Value erase(Object* self, Value key) {
        return self ? self->_value_erase(key) : value_make_error();
        /*
        switch (self->_class) {
            case Class::TABLE:
                return ((const HeapTable*) self)->erase(key);
            default:
                return value_make_error();
        }
         */
    }

    size_t size(const Object* self) {
        return self ? self->_value_size() : 0;
        /*
        switch (self->_class) {
            case Class::INDIRECT_FIXED_CAPACITY_VALUE_ARRAY:
                return ((const IndirectFixedCapacityValueArray*) self)->_capacity;
            case Class::ARRAY:
                return ((HeapArray*) self)->size();
            case Class::TABLE:
                return ((const HeapTable*) self)->size();
            case Class::STRING:
                return ((const HeapString*) self)->_size;
            case Class::INT64:
                return 0;
            default:
                abort();
        }*/
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
    
    
    void value_shade(Value value) {
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
        return Object::operator new(self + extra);
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

    
    void value_debug(const Value& self) {
        switch (_value_tag(self)) {
            case VALUE_TAG_BOOLEAN:
                return (void)printf("%s\n", value_as_boolean(self) ? "TRUE" : "FALSE");
            case VALUE_TAG_CHARACTER:
                return (void)printf("\'%lc\'\n", value_as_character(self));
            case VALUE_TAG_ERROR:
                return (void)printf("ERROR\n");
            case VALUE_TAG_OBJECT:
                return object_debug(_value_as_object(self));
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
    
    
    Value _value_make_with(const Object* p) {
        Value result;
        result._data = (uint64_t)p;
        return result;
    }

    IndirectFixedCapacityValueArray::IndirectFixedCapacityValueArray(std::size_t count)
    : _capacity(count)
    , _storage((Traced<Value>*) calloc(count, sizeof(Traced<Value>))) {
    }
    
    IndirectFixedCapacityValueArray::~IndirectFixedCapacityValueArray() {
        // Safety:
        //    Storage is a conventionally managed C++ buffer
        free(_storage);
    }
    
    Value value_make_table() {
        Value result;
        result._data = (uint64_t)(new HeapTable);
        return result;
    }

    Value value_make_array() {
        Value result;
        result._data = (uint64_t)(new HeapArray);
        return result;
    }
    
    
    
    
    
    bool Object::_value_empty() const { abort(); }
    size_t Object::_value_size() const { return 0; }
    bool Object::_value_contains(Value key) const { return false; }
    Value Object::_value_find(Value key) const { return value_make_error(); }
    Value Object::_value_insert_or_assign(Value key, Value value) { return value_make_error(); }
    Value Object::_value_erase(Value key) { return value_make_error(); }
    


} // namespace wry::gc



