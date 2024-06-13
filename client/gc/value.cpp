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
            case TAG_POINTER: {
                return _pointer->as_string_view();
            }
            case TAG_SHORT_STRING: {
                return _string.as_string_view();
            }
            default:
                abort();
        }
    }
    */
    
    
    int Value::_discriminant() const { return _data & VALUE_MASK; }
    bool Value::_is_small_integer() const { return _discriminant() == TAG_SMALL_INTEGER; }
    bool Value::_is_pointer() const { return _discriminant() == TAG_POINTER; }
    bool Value::_is_short_string() const { return _discriminant() == TAG_SHORT_STRING; }
    bool Value::_is_tombstone() const { return _discriminant() == TAG_TOMBSTONE; }
    
    // these logical types are always stored inline
    bool Value::is_enumeration() const { return _discriminant() == TAG_ENUMERATION; }
    bool Value::is_null() const { return !_data; }
    bool Value::is_error() const { return _discriminant() == TAG_ERROR; }
    bool Value::is_boolean() const { return _discriminant() == TAG_BOOLEAN; }
    bool Value::is_character() const { return _discriminant() == TAG_CHARACTER; }
    
    const Object* Value::_as_pointer() const {
        assert(_is_pointer());
        return (Object*)_data;
    }
    
    const Object* Value::_as_pointer_or_nullptr() const {
        return _is_pointer() ? _as_pointer() : nullptr;
    }
    
    int64_t Value::_as_small_integer() const {
        assert(_is_small_integer());
        return (int64_t)_data >> VALUE_SHIFT;
    }
    
    std::string_view Value::_as_short_string() const {
        assert(_is_short_string());
        return ((const _short_string_t&)_data).as_string_view();
    }
    
    bool Value::as_boolean() const {
        assert(is_boolean());
        return ((const _boolean_t&)_data).boolean;
    }
    
    int64_t Value::as_enumeration() const {
        assert(is_enumeration());
        return (int64_t)_data >> 4;
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
        return Value::make_error();\
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
    
    
    
    
    
    
    std::size_t Value::hash() const {
        switch (_discriminant()) {
            case TAG_POINTER: {
                const Object* a = _as_pointer();
                return a ? gc_hash(a) : 0;
            }
            case TAG_SMALL_INTEGER: {
                std::int64_t a = _as_small_integer();
                // std::hash<std::int64_t> is trivial (on libc++)
                // return std::hash<std::int64_t>()(a);
                return wry::hash(a);
            }
            case TAG_SHORT_STRING: {
                return std::hash<std::string_view>()(_as_short_string());
            }
            case TAG_BOOLEAN: {
                return std::hash<bool>()(as_boolean());
            }
            default:
                abort();
        }
    }
    
    Value Value::from_int64(std::int64_t z) {
        Value result;
        std::int64_t y = z << 4;
        if ((y >> 4) == z) {
            result._data = y | TAG_SMALL_INTEGER;
        } else {
            result._data = (uint64_t)new HeapInt64(z);
        }
        return result;
    }
    
    Value Value::from_ntbs(const char* ntbs) {
        Value result;
        std::size_t n = std::strlen(ntbs);
        if (n < 8) {
            _short_string_t s = {};
            s._tag_and_len = (n << 4) | TAG_SHORT_STRING;
            __builtin_memcpy(s._chars, ntbs, n);
            __builtin_memcpy(&result, &s, 8);
            assert(result._is_short_string());
        } else {
            result._data = (uint64_t)HeapString::make(std::string_view(ntbs, n));
            assert(result._is_pointer());
        }
        return result;
    }
    
    Value Value::_from_object(const Object* object) {
        Value result;
        result._data = (uint64_t)object;
        assert(result._is_pointer());
        return result;
    }
    
    Value Value::from_bool(bool flag) {
        Value result;
        _boolean_t b;
        b._tag = TAG_BOOLEAN;
        b.boolean = flag;
        __builtin_memcpy(&result, &b, 8);
        assert(result.is_boolean());
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
            _storage[--_size] = Value::make_null();
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
            return Value::make_null();
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
        
        assert(t.size() == 0);
        assert(!t.contains("a"));
        assert(t.find("a") == Value::make_null());
        t.insert_or_assign("a", "A");
        assert(t.size() == 1);
        assert(t.contains("a"));
        assert(t.find("a") == "A");
        assert(t.insert_or_assign("a", "A2") == "A");
        assert(t.size() == 1);
        assert(t.contains("a"));
        assert(t.find("a") == "A2");
        t.erase("a");
        assert(t.size() == 0);
        assert(!t.contains("a"));
        assert(t.find("a") == Value::make_null());
        
        
        std::vector<int> v(100);
        std::iota(v.begin(), v.end(), 0);
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(v.begin(), v.end(), g);
        
        for (int i = 0; i != 100; ++i) {
            assert(t.size() == i);
            assert(!t.contains(v[i]));
            assert(t.find(v[i]).is_null());
            assert(t.insert_or_assign(v[i], v[i]).is_null());
            assert(t.size() == i + 1);
            assert(t.contains(v[i]));
            assert(t.find(v[i]) == v[i]);
        }
        
        std::shuffle(v.begin(), v.end(), g);
        for (int i = 0; i != 100; ++i) {
            assert(t.contains(v[i]));
            assert(t.find(v[i]) == v[i]);
            assert(!t.contains(v[i] + 100));
            assert(t.find(v[i] + 100).is_null());
        }

        for (int i = 0; i != 100; ++i) {
            assert(t.contains(v[i]));
            assert(t.find(v[i]) == v[i]);
            assert(t.erase(v[i])== v[i]);
            assert(t.contains(v[i]) == false);
            assert(t.find(v[i]).is_null());
        }
        
        assert(t.size() == 0);

        std::shuffle(v.begin(), v.end(), g);
        
        Value s = t;
        for (int i = 0; i != 100; ++i) {
            assert(s.size() == i);
            assert(!s.contains(v[i]));
            assert(s[v[i]] == Value::make_null());
            // t._pointer->_invariant();
            s[v[i]] = v[i];
            // t._pointer->_invariant();
            assert(t.size() == i + 1);
            assert(t.contains(v[i]));
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
        return _value.load(Order::RELAXED);
    }
        
    Traced<Value>::Traced(const Value& value) 
    : _value(value) {
    }
    
    Traced<Value>::Traced(const Traced<Value>& value)
    : Traced(value.get()) {
    }
    
    void shade(Value a, Value b) {
        shade(a);
        shade(b);
    }
    

    Traced<Value>& Traced<Value>::operator=(const Value& desired) {
        Value discovered = this->_value.exchange(desired, Order::RELEASE);
        shade(desired, discovered);
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
            case TAG_POINTER:
                assert(_pointer);
                return _pointer->_size;
            case TAG_SHORT_STRING:
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
                return Value::make_error();
        }
    }

    Value insert_or_assign(const Object* self, Value key, Value value) {
        switch (self->_class) {
            case CLASS_TABLE:
                return ((const HeapTable*) self)->insert_or_assign(key, value);
            default:
                return Value::make_error();
        }
    }

    Value erase(const Object* self, Value key) {
        switch (self->_class) {
            case CLASS_TABLE:
                return ((const HeapTable*) self)->erase(key);
            default:
                return Value::make_error();
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

    
    //Table::Table() : _pointer(new HeapTable()) {}

    
    std::size_t Value::size() const {
        using gc::size;
        switch (_discriminant()) {
            case TAG_POINTER:
                return _data ? size(_as_pointer()) : 0;
            case TAG_SHORT_STRING:
                return (_data >> 4) & 7;
            default:
                return 0;
        }
    }
    
    bool Value::contains(Value key) const {
        using gc::contains;
        switch (_discriminant()) {
            case TAG_POINTER:
                return _data && contains(_as_pointer(), key);
            default:
                return false;
        }
    }
    
    
    _deferred_subscript_t Value::operator[](Value key) {
        return {*this, key};
    }
    
    
    void shade(Value value) {
        if (value._is_pointer()) {
            object_shade(value._as_pointer());
        }
    }
    
    
    
    _deferred_subscript_t::operator Value() && {
        return find(container._as_pointer(), key);
    }
    
    _deferred_subscript_t&& _deferred_subscript_t::operator=(Value desired) && {
        insert_or_assign(container._as_pointer(), key, desired);
        return std::move(*this);
    }
    
    _deferred_subscript_t&& _deferred_subscript_t::operator=(_deferred_subscript_t&& desired) && {
        return std::move(*this).operator=((Value)std::move(desired));
    }

    
    Value Value::make_error() { Value result; result._data = TAG_ERROR; return result; }
    Value Value::make_null() { Value result; result._data = 0; return result; }
    Value Value::make_tombstone() { Value result; result._data = TAG_TOMBSTONE; return result; }

    
   

    
    
    void* HeapString::operator new(std::size_t count, std::size_t extra) {
        return allocate(count + extra);
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
    
    Value Value::insert_or_assign(Value key, Value value) {
        using gc::insert_or_assign;
        return insert_or_assign(_as_pointer(), key, value);
    }

    
    Value Value::find(Value key) const {
        using gc::find;
        return find(_as_pointer(), key);
    }
    
    Value Value::erase(Value key) {
        using gc::erase;
        return erase(_as_pointer(), key);
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
