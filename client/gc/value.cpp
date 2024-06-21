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

#include "debug.hpp"


namespace wry::gc {
    
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
        switch (self->_class) {
            case Class::TABLE:
                return ((const HeapTable*) self)->contains(key);
            default:
                return false;
        }
    }

    Value find(const Object* self, Value key) {
        switch (self->_class) {
            case Class::ARRAY:
                return ((HeapArray*) self)->find(key);
            case Class::TABLE:
                return ((const HeapTable*) self)->find(key);
            default:
                return value_make_error();
        }
    }

    Value insert_or_assign(const Object* self, Value key, Value value) {
        switch (self->_class) {
            case Class::ARRAY:
                return ((HeapArray*) self)->insert_or_assign(key, value);
            case Class::TABLE:
                return ((const HeapTable*) self)->insert_or_assign(key, value);
            default:
                return value_make_error();
        }
    }

    Value erase(const Object* self, Value key) {
        switch (self->_class) {
            case Class::TABLE:
                return ((const HeapTable*) self)->erase(key);
            default:
                return value_make_error();
        }

    }

    std::size_t size(const Object* self) {
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
        }
    }
    
    std::size_t value_size(const Value& self) {
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
        
    HeapString* HeapString::make(std::string_view view) {
        return make(std::hash<std::string_view>()(view), view);
    }
    
    std::string_view HeapString::as_string_view() const {
        return std::string_view(_bytes, _size);
    }
    
    
    
    
    HeapInt64::HeapInt64(std::int64_t z)
    : Object(Class::INT64)
    , _integer(z) {
    }

    std::int64_t HeapInt64::as_int64_t() const {
        return _integer;
    }
        
    HeapString::HeapString()
    : Object(Class::STRING) {
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
            case VALUE_TAG_ENUMERATION:
                return (void)printf("enum{%lld}\n", value_as_enumeration(self));
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
    : Object(Class::INDIRECT_FIXED_CAPACITY_VALUE_ARRAY)
    , _capacity(count)
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

} // namespace wry::gc






// To support basic math operators we need
// + -> arbitrary precision
// - -> signed
// / -> rational
//
// a more conventional solution is to use floating point, but this is
// problematic for getting consistent results across clients, and generally
// a can of worms
//
// other choices:
// hard limit at some human-friendly decimal value like 1k, 1M?
// expose hardware modulo 2^n?
// expose intger division?
// fixed point?
//
// I don't really want to expose arbitrary math on approximate real numbers,
// like sqrt, sin etc.


// Pointer to a garbage-collected heap representation, or an inline integer
// representation.  Uses invalid (misaligned) pointer values to distinguish
// multiple cases; we can tag 15 kinds of not-pointer with the 4 lsbs, with
// 0b...0000 being a valid aligned address.
//
// Some kinds of Value are exclusively represented on the heap (mutable
// containers) or exclusively on the stack (bool, enumerations), and
// others may switch (immutable containers such as strings and arbitrary
// precision integers keep small, and presumed common, values inline)

// TODO: Is this a Value... or a Variable?
// "strings are immutable", but V*** can change which immutable string they
// reference.  And indeed, the gc::String object can change what it points
// to.  Call these things Variables instead?

/*
 
 
 union {
 int _tag;
 const Object* _pointer;
 std::int64_t _integer;
 _short_string_t _short_string;
 _boolean_t _boolean;
 std::int64_t _enumeration;
 std::uint64_t _raw;
 };
 
 */


/*
 
 // these logical types are always stored inline
 
 // Several types have only a small number of values, we can pack
 // them all into a single tag?
 // true, false, error, tombstone, UTF-32 character
 
 */





/*
 
 struct Array;
 struct Boolean;
 struct Character;
 struct Enumeration;
 struct Number;
 struct String;
 struct Table;
 
 */

/*
 struct HeapArray;
 struct HeapInt64;
 struct HeapNumber;
 struct HeapString;
 struct HeapTable;
 */


/*
 struct Array {
 HeapArray* _array;
 operator Value() const { return reinterpret_cast<const Value&>(*this); };
 std::size_t size() const;
 Value operator[](std::size_t pos) const;
 Traced<Value>& operator[](std::size_t pos);
 };
 
 struct String {
 union {
 int _tag;
 const HeapString* _pointer;
 _short_string_t _string;
 };
 int _discriminant() const { return _tag & VALUE_MASK; }
 bool _is_pointer() const { return _discriminant() == VALUE_TAG_POINTER; }
 bool _is_short_string() const { return _discriminant() == VALUE_TAG_SHORT_STRING; }
 const HeapString* _as_pointer() const {
 assert(_is_pointer());
 return _pointer;
 }
 const HeapString* _as_pointer_else_nullptr() {
 return _is_pointer() ? _pointer : nullptr;
 }
 operator Value() const { return reinterpret_cast<const Value&>(*this); }
 std::string_view as_string_view() const;
 std::size_t size() const;
 };
 
 struct Table {
 const HeapTable* _pointer;
 Table();
 operator Value() const { return reinterpret_cast<const Value&>(*this); };
 Value operator[](Value key) const;
 Traced<Value>& operator[](Value key);
 bool contains(Value key) const;
 std::size_t size() const;
 Value find(Value key) const;
 Value erase(Value key);
 Value insert_or_assign(Value key, Value value);
 };
 
 */









// will hold a container ref and a key, and resolve to something depending
// on how it is used (as a get, a set, or a read-modity-write)

// TODO: for binary operators, we have the multiple dispatch problem
// - elegant solution?
// - symmetric implementation?

// We expose all C++ operators on Values, for convenience and familiarity,
// though some are problematic with the GC syntax.  Note that there is
// no particular requirement that "the game" or "the mod scripting
// language" follow these same semantics.

/*
 
 struct HeapValue : Object {
 
 // to reach this point the (lhs or unary) participaing Value must be
 // a heap-allocated object, but when the operation is mutating it need
 // not remain so; for example, LongInteger *= 0 will replace the Value
 // with an inline 0.
 
 // reflection
 
 const HeapInt64* as_HeapInt64() const;
 const HeapString* as_HeapString() const;
 const HeapArray* as_HeapArray() const;
 const HeapTable* as_HeapTable() const;
 
 // comparison
 
 std::partial_ordering three_way_comparison(Value) const;
 bool equality(Value) const;
 bool logical_not() const;
 
 // pure unary
 Value unary_plus() const;
 Value unary_minus() const;
 Value bitwise_not() const;
 
 // mutating unary
 Value postfix_increment(Value& self) const;
 Value postfix_decrement(Value& self) const;
 void prefix_increment(Value& self) const;
 void prefix_decrement(Value& self) const;
 
 // pure binary
 Value multiplication(Value) const;
 Value division(Value) const;
 Value remainder(Value) const;
 Value addition(Value) const;
 Value subtraction(Value) const;
 Value left_shift(Value) const;
 Value right_shift(Value) const;
 Value bitwise_and(Value) const;
 Value bitwise_xor(Value) const;
 Value bitwise_or(Value) const;
 
 // mutating binary
 
 // since numbers on the heap are immutable, there seems to be no
 // meaningful customization possible here; they will just have tp
 // do the basic operation and replace their handle with it
 void assigned_multiplication(Value&, Value) const;
 void assigned_division(Value&, Value) const;
 void assigned_remainder(Value&, Value) const ;
 void assigned_addition(Value&, Value) const;
 void assigned_subtraction(Value&, Value) const;
 void assigned_left_shift(Value&, Value) const;
 void assigned_right_shift(Value&, Value) const;
 void assigned_bitwise_and(Value&, Value) const;
 void assigned_bitwise_xor(Value&, Value) const;
 void assigned_bitwise_or(Value&, Value) const;
 
 // odd cases
 Value function_call() const;
 // Value subscript_const(Value other) const;
 // DeferredElementAccess subscript_mutable(Value& self, Value other);
 //virtual
 
 // built-in functions
 String str() const;
 
 // common interface
 std::size_t size() const;
 bool contains(Value) const;
 Value find(Value) const;
 Value erase(Value) const;
 Value insert_or_assign(Value, Value) const;
 
 explicit HeapValue(Class Class::) : Object(Class::) {}
 
 };
 
 */







// This is a std::vector-like object that is garbage collected
// It is only "concurrent enough" for GC; it does not support access by
// multiple mutators.
//
// Notably it is only amortized O(1), and has a worst case O(N).  As such
// it is unsuitable for general use in soft real time contexts, but is
// still useful for things that have some kind of moderate bounded size,
// and as a stepping stone to more advanced data structures.





/*
 
 template<typename F>
 auto visit(Value value, F&& f) {
 switch (value._tag & Value::MASK) {
 case Value::POINTER: {
 const HeapValue* a = value._as_pointer();
 const HeapInt64* b = dynamic_cast<const HeapInt64*>(a);
 if (b) {
 return f(b->_integer);
 }
 const HeapString* c = dynamic_cast<const HeapString*>(a);
 if (c) {
 return f(c->as_string_view());
 }
 abort();
 }
 case Value::SMALL_INTEGER: {
 return f(value._as_small_integer());
 }
 case Value::SHORT_STRING: {
 return f(value._as_short_string());
 }
 case Value::BOOLEAN: {
 return f(value.as_boolean());
 }
 default:
 abort();
 }
 }
 
 */


/*
 
 // There's no need for this type until we support non-int64 numbers
 
 struct Number {
 
 Value _value_that_is_a_number;
 
 operator Value() const { // upcast
 return _value_that_is_a_number;
 }
 
 std::int64_t as_int64_t() const {
 switch (_value_that_is_a_number._get_tag()) {
 case Value::POINTER: {
 const Object* a = _value_that_is_a_number._as_object();
 assert(dynamic_cast<const HeapInt64*>(a));
 const HeapInt64* b = static_cast<const HeapInt64*>(a);
 return b->as_int64_t();
 }
 case Value::INTEGER: {
 return _value_that_is_a_number._as_integer();
 }
 default:
 abort();
 }
 }
 
 };
 */




/*
 std::string_view String::as_string_view() const {
 switch (_discriminant()) {
 case VALUE_TAG_POINTER: {
 return _pointer->as_string_view();
 }
 case VALUE_TAG_SHORT_STRING: {
 return _string.as_string_view();
 }
 default:
 abort();
 }
 }
 */


