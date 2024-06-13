//
//  value.cpp
//  client
//
//  Created by Antony Searle on 31/5/2024.
//

#include <algorithm>
#include <numeric>
#include <random>

#include "debug.hpp"
#include "value.hpp"


namespace gc {
    
   

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
    
    
    int _value_tag(const Value& self) { return self._data & VALUE_MASK; }
    bool _value_is_small_integer(const Value& self) { return _value_tag(self) == VALUE_TAG_SMALL_INTEGER; }
    bool _value_is_object(const Value& self) { return _value_tag(self) == VALUE_TAG_POINTER; }
    bool _value_is_short_string(const Value& self) { return _value_tag(self) == VALUE_TAG_SHORT_STRING; }
    bool _value_is_tombstone(const Value& self) { return _value_tag(self) == VALUE_TAG_TOMBSTONE; }
    
    // these logical types are always stored inline
    bool value_is_enumeration(const Value& self) { return _value_tag(self) == VALUE_TAG_ENUMERATION; }
    bool value_is_null(const Value& self) { return !self._data; }
    bool value_is_error(const Value& self) { return _value_tag(self) == VALUE_TAG_ERROR; }
    bool value_is_boolean(const Value& self) { return _value_tag(self) == VALUE_TAG_BOOLEAN; }
    bool value_is_char(const Value& self) { return _value_tag(self) == VALUE_TAG_CHARACTER; }
    
    const Object* _value_as_object(const Value& self) {
        assert(_value_is_object(self));
        return (Object*)self._data;
    }
    
    const Object* _as_pointer_or_nullptr(const Value& self) {
        return _value_is_object(self) ? _value_as_object(self) : nullptr;
    }
    
    int64_t _value_as_small_integer(const Value& self) {
        assert(_value_is_small_integer(self));
        return (int64_t)self._data >> VALUE_SHIFT;
    }
    
    std::string_view _value_as_short_string(const Value& self) {
        assert(_value_is_short_string(self));
        return ((const _short_string_t&)self._data).as_string_view();
    }
    
    bool value_as_boolean(const Value& self) {
        assert(value_is_boolean(self));
        return self._data >> VALUE_SHIFT;
    }
    
    int64_t value_as_enumeration(const Value& self) {
        assert(value_is_enumeration(self));
        return (int64_t)self._data >> VALUE_SHIFT;
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
    Value operator Y (const Value& self, const Value& other) {\
        return value_make_error();\
    }\
    \
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
    
    
    
    
    
    
    size_t value_hash(const Value& self) {
        switch (_value_tag(self)) {
            case VALUE_TAG_POINTER: {
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
    
    Value value_make_integer(std::int64_t z) {
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
    
    Value value_make_boolean_with(bool flag) {
        Value result;
        result._data = ((uint64_t)flag << VALUE_SHIFT) | VALUE_TAG_BOOLEAN;
        assert(value_is_boolean(result));
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
    
    
    
    
    
    
    // This strange object exists so that we can have buffers of a power of two
    // number of pointers that use a power of two allocation, without having
    // their first words taken by headers, or the headers making the allocation
    // slightly more than a power of two (!)
    //
    // The size and identity of the buffer are immutable, but the buffer's
    // contents are mutable, atomic and should be subject to the write barrier
    //
    // The buffer is full of Values, but we only care if they can be
    // interpreted as Object*.
    
    //
    // This object is not needed for resizable arrays of not-potential-gc-
    // pointers, which do not need to be scanned at all and are just held and
    // managed directly perhaps in a std::vector, or small arrays of fixed
    // size, which can use the fma pattern

    
    /*
    struct HeapArray : HeapValue {
        
        mutable std::size_t _size;
        mutable std::size_t _capacity;
        mutable Traced<Value>* _storage; // TODO: type?
        mutable Traced<const IndirectFixedCapacityValueArray*> _storage_manager;
        
        HeapArray()
        : _size(0)
        , _capacity(0)
        , _storage(0)
        , _storage_manager(nullptr) {
            // printf("%p new Value[]\n", this);
        }
        
        virtual ~HeapArray() override {
            // printf("%p del Value[%zd]\n", this, _size);
        }
        
        virtual std::size_t gc_bytes() const override {
            return sizeof(HeapArray);
        }
        
        virtual void gc_enumerate() const override {
            trace(_storage_manager);
        }
        
        void push_back(Value value) const {
            if (_size == _capacity) {
                auto a = std::max<std::size_t>(16, _capacity * 2);
                auto b = new IndirectFixedCapacityValueArray(a);
                auto c = b->_storage;
                std::memcpy(c, _storage, _size * 8);
                _capacity = a;
                _storage = b->_storage;
                _storage_manager = b;
            }
            _storage[_size++] = value;
        }
        
        bool empty() const {
            return !_size;
        }
        
        void pop_back() const {
            assert(_size);
            _storage[--_size] = value_make_null();
        }
        
        std::size_t size() const override {
            return _size;
        }
        
        virtual bool contains(Value key) const override {
            if (key._is_small_integer()) {
                auto pos = key._as_small_integer();
                if (0 <= pos && pos < _size)
                    return true;
            }
            return false;
        }
        
        virtual Value find(Value key) const override {
            if (key._is_small_integer()) {
                auto pos = key._as_small_integer();
                if (0 <= pos && pos < _size)
                    return _storage[pos];
            }
            return value_make_null();
        }
        
    };
    
    */


    
    
   
    /*
    std::size_t Table::size() const {
        return _pointer->size();
    }
    
    bool Table::contains(Value key) const {
        return _pointer->contains(key);
    }
    
    Value Table::find(Value key) const {
        return _pointer->find(key);
    }
    
    Value Table::erase(Value key) {
        return _pointer->erase(key);
    }
    
    Value Table::insert_or_assign(Value key, Value value) {
        return _pointer->insert_or_assign(key, value);
    }

    
    Value Table::operator[](Value key) const {
        return _pointer->find(key);
    }
    
    Traced<Value>& Table::operator[](Value key) {
        return _pointer->find_or_insert_null(key);
        
    }
*/

    
  
    
      
    void foo() {
        
        Value t;
        t._data = (uint64_t)new HeapTable;
        
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



        /*
        
        // the heap-allocated objects will live until the next handshake so
        // they will live beyond the end of this function even without being
        // marked-as-roots anywhere
        
        Value a = Value::from_ntbs("hello"); // short string
        Value b = Value::from_ntbs("long kiss goodbye"); // long string
        
        assert(a._is_string()); // packed into value
        assert(b._is_pointer()); // on the heap
        
        // hack type interrogation
        String c; c._string = a._string;
        String d; d._string = b._string;
        
        auto e = c.as_string_view();
        printf("%.*s\n", (int)e.size(), e.data());
        auto f = d.as_string_view();
        printf("%.*s\n", (int)f.size(), f.data());
        
        Value z = Value::from_int64(-7);
        Value y = Value::from_int64(-777777777777777);
        
        //Number x; x._value_that_is_a_number = z;
        //Number w; w._value_that_is_a_number = y;
        
        //printf("%" PRId64 "\n", x.as_int64_t());
        //printf("%" PRId64 "\n", w.as_int64_t());
        
        auto m = [](auto x) {
            printf("visited with %s\n", __PRETTY_FUNCTION__);
        };
        
        visit(a, m);
        visit(b, m);
        visit(z, m);
        visit(y, m);
        
        auto v = new HeapArray();
        v->push_back(a);
        v->push_back(b);
        v->push_back(c);
        v->push_back(d);
        v->push_back(z);
        v->push_back(y);
        //v->push_back(w);
        //v->push_back(x);
        
         */
        
        
    }
     
    
    
    
    
    Value Traced<Value>::get() const {
        return _atomic_value.load(Order::RELAXED);
    }
        
    Traced<Value>::Traced(const Value& value) 
    : _atomic_value(value) {
    }
    
    Traced<Value>::Traced(const Traced<Value>& value)
    : Traced(value.get()) {
    }
        

    Traced<Value>& Traced<Value>::operator=(const Value& desired) {
        Value discovered = this->_atomic_value.exchange(desired, Order::RELEASE);
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
    
    Value::operator bool() const {
        // POINTER: nonnull
        //    - All containers are true, even if empty
        // INTEGER: nonzero
        // STRING: nonempty
        // ENUMERATION: nonzero
        // BOOLEAN: nonzero
        // ERROR: always false
        // TOMBSTONE: always false
        return _data >> 4;
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
            case CLASS_TABLE:
                return ((const HeapTable*) self)->contains(key);
            default:
                return false;
        }
    }

    Value find(const Object* self, Value key) {
        switch (self->_class) {
            case CLASS_TABLE:
                return ((const HeapTable*) self)->find(key);
            default:
                return value_make_error();
        }
    }

    Value insert_or_assign(const Object* self, Value key, Value value) {
        switch (self->_class) {
            case CLASS_TABLE:
                return ((const HeapTable*) self)->insert_or_assign(key, value);
            default:
                return value_make_error();
        }
    }

    Value erase(const Object* self, Value key) {
        switch (self->_class) {
            case CLASS_TABLE:
                return ((const HeapTable*) self)->erase(key);
            default:
                return value_make_error();
        }

    }

    std::size_t size(const Object* self) {
        switch (self->_class) {
            case CLASS_INDIRECT_FIXED_CAPACITY_VALUE_ARRAY:
                return ((const IndirectFixedCapacityValueArray*) self)->_capacity;
            case CLASS_TABLE:
                return ((const HeapTable*) self)->size();
            case CLASS_STRING:
                return ((const HeapString*) self)->_size;
            case CLASS_INT64:
                return 0;
            default:
                abort();
        }
    }
    
    std::size_t value_size(const Value& self) {
        switch (_value_tag(self)) {
            case VALUE_TAG_POINTER:
                return self._data ? size(_value_as_object(self)) : 0;
            case VALUE_TAG_SHORT_STRING:
                return (self._data >> 4) & 7;
            default:
                return 0;
        }
    }
    
    bool value_contains(const Value& self, Value key) {
        switch (_value_tag(self)) {
            case VALUE_TAG_POINTER:
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

    
    Value value_make_error() { Value result; result._data = VALUE_TAG_ERROR; return result; }
    Value value_make_null() { Value result; result._data = 0; return result; }
    Value value_make_tombstone() { Value result; result._data = VALUE_TAG_TOMBSTONE; return result; }

    
   

    
    
    void* HeapString::operator new(std::size_t count, std::size_t extra) {
        return object_allocate(count + extra);
    }
    
    HeapString* HeapString::make(std::string_view v,
                            std::size_t hash) {
        HeapString* p = new(v.size()) HeapString;
        p->_hash = hash;
        p->_size = v.size();
        std::memcpy(p->_bytes, v.data(), v.size());
        //            printf("%p new \"%.*s\"%s\n",
        //                   p,
        //                   std::min((int)p->_size, 48), p->_bytes,
        //                   ((p->_size > 48) ? "..." : ""));
        return p;
    }
    
    HeapString* HeapString::make(std::string_view v) {
        return make(v, std::hash<std::string_view>()(v));
    }
    
    std::string_view HeapString::as_string_view() const {
        return std::string_view(_bytes, _size);
    }
    
    
    
    
    HeapInt64::HeapInt64(std::int64_t z)
    : Object(CLASS_INT64)
    , _integer(z) {
    }

    std::int64_t HeapInt64::as_int64_t() const {
        return _integer;
    }
        
    HeapString::HeapString()
    : Object(CLASS_STRING) {
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

    
} // namespace gc






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
 
 explicit HeapValue(Class class_) : Object(class_) {}
 
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


