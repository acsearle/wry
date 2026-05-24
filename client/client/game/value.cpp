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
#include "utility.hpp"
#include "value.hpp"
#include "HeapString.hpp"
#include "debug.hpp"


namespace wry {
    
    std::string_view _value_as_short_string(const Value& self) {
        assert(_value_is_short_string(self));
        return ((const _short_string_t&)self._data).as_string_view();
    }
    
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
    
    // _value_make_garbage_collected_with was an orphan duplicate of
    // _value_make_with (declared in value.hpp).  Removed.
    
        
    
    
    
    
    
    
    
    
    
    
    
  
    
      
#if 0
    // Aspirational smoke test for the persistent associative-container
    // surface (value_make_table / value_insert_or_assign / value_find /
    // value_erase / value_size / value_contains, plus the operator
    // overloads).  Disabled because:
    //   - HeapArray / HeapTable are attic'd; value_make_table()
    //     currently returns a null Value, so most assertions don't hold.
    //   - The tests use operator== which has been removed in favor of
    //     value_eq(a, b) returning Value-of-bool.
    // Restore as a real define_test once the persistent associative DS
    // and value_eq land.
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
            // assert(hash(v[i]) == hash(Value(v[i])));
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
#endif // 0

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
    
    size_t value_size(Value self) {
        switch (_value_tag(self)) {
            case VALUE_TAG_OBJECT:
                return self._data ? size(_value_as_object(self)) : 0;
            case VALUE_TAG_SHORT_STRING:
                return (self._data >> 4) & 7;
            default:
                return 0;
        }
    }
    
    bool value_contains(Value self, Value key) {
        switch (_value_tag(self)) {
            case VALUE_TAG_OBJECT:
                return self._data && contains(_value_as_object(self), key);
            default:
                return false;
        }
    }
    
    
    Value Value::operator[](Value key) const {
        abort();
    }
    
    
    void object_shade(const Value& value) {
        if (_value_is_object(value)) {
            object_shade(_value_as_object(value));
        }
    }


    HeapInt64::HeapInt64(std::int64_t z)
    : _integer(z) {
    }

    std::int64_t HeapInt64::as_int64_t() const {
        return _integer;
    }
        
    HeapString::HeapString() {
    }
    
    Value value_insert_or_assign(Value self, Value key, Value value) {
        return insert_or_assign(_value_as_object(self), key, value);
    }

    
    Value value_find(Value self, Value key) {
        return find(_value_as_object(self), key);
    }
    
    Value value_erase(Value self, Value key) {
        return erase(_value_as_object(self), key);
    }

        
    void debug(Value self) {
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
    
    Value _value_make_with(const GarbageCollected* p) {
        Value result;
        result._data = (uint64_t)p;
        return result;
    }

    // DEPRECATED.  See declaration in value.hpp.  Returns null so that
    // accidental use (e.g. through io/json.hpp's currently-dormant
    // parse_json_object) produces an obvious zero/error cascade rather
    // than a broken table object.
    Value value_make_table() {
        return Value{};
    }

    bool HeapValue::_value_empty() const { abort(); }
    size_t HeapValue::_value_size() const { return 0; }
    bool HeapValue::_value_contains(Value key) const { return false; }
    Value HeapValue::_value_find(Value key) const { return value_make_error(); }
    Value HeapValue::_value_insert_or_assign(Value key, Value value) const { return value_make_error(); }
    Value HeapValue::_value_erase(Value key) const { return value_make_error(); }
    

    
    Value HeapValue::_value_add(Value right) const { return value_make_error(); }
    Value HeapValue::_value_sub(Value right) const { return value_make_error(); }
    Value HeapValue::_value_mul(Value right) const { return value_make_error(); }
    Value HeapValue::_value_div(Value right) const { return value_make_error(); }
    Value HeapValue::_value_mod(Value right) const { return value_make_error(); }
    Value HeapValue::_value_rshift(Value right) const { return value_make_error(); }
    Value HeapValue::_value_lshift(Value right) const { return value_make_error(); }

//    void HeapInt64::_garbage_collected_shade() const {
//        abort();
//        //Color expected = Color::WHITE;
//        //(void) color.compare_exchange(expected, Color::BLACK);
//    }
    
    void HeapInt64::_garbage_collected_scan() const {
        // fprintf(stderr, "scanned a weak ");
        // _garbage_collected_debug();
        // abort();
    }


} // namespace wry



