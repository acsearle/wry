//
//  value.hpp
//  client
//
//  Created by Antony Searle on 31/5/2024.
//

#ifndef value_hpp
#define value_hpp

#include <cinttypes>

#include "gc.hpp"

namespace gc {
    
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

    struct Value;
    
    // For convenience and efficiency in C++, we can constrain the type of a
    // Value to its logical kind
    struct Array;
    struct Boolean;
    struct Enumeration;
    struct Error;
    struct Number;
    struct String;
    
    struct HeapValue;
    // The superclassclass of all Heap representations of Objects
    // - is a subclass of gc::Object
    // - provides a large abstract interface and default implementations
    //   - operators
    //   - built-in functions
    //   - reflection
    
    // Fixed size integer
    struct HeapInt64;
    // Immutable string (not yet interned but will be)
    struct HeapString;
    // Dynamic array (like std::vector<Value>)
    struct HeapArray;
    // Hash table (what kind of keys are allowed?)
    struct HeapTable;
    // Pointer-pointer seems kind of useful, such as allowing shared access to
    // a
    struct HeapIndirection;

    
    
    

    // TODO: operator[]() is quite a pig

    struct DeferredElementAccess;
    // will hold a container ref and a key, and resolve to something depending
    // on how it is used (as a get, a set, or a read-modity-write)
    
    // TODO: for binary operators, we have the multiple dispatch problem
    // - elegant solution?
    // - symmetric implementation?
    
    // We expose all C++ operators on Values, for convenience and familiarity,
    // though some are problematic with the GC syntax.  Note that there is
    // no particular requirement that "the game" or "the mod scripting
    // language" follow these same semantics.
    
    
    struct HeapValue : Object {
        
        // to reach this point the (lhs or unary) participaing Value must be
        // a heap-allocated object, but when the operation is mutating it need
        // not remain so; for example, LongInteger *= 0 will replace the Value
        // with an inline 0.
        
        // reflection
        
        virtual const HeapInt64* as_HeapInt64() const;
        virtual const HeapString* as_HeapString() const;
        virtual const HeapArray* as_HeapArray() const;

        // comparison
        
        virtual std::partial_ordering three_way_comparison(Value) const;
        virtual bool equality(Value) const;
        virtual bool logical_not() const;

        // pure unary
        virtual Value unary_plus() const;
        virtual Value unary_minus() const;
        virtual Value bitwise_not() const;
        
        // mutating unary
        virtual Value postfix_increment(Value& self) const;
        virtual Value postfix_decrement(Value& self) const;
        virtual void prefix_increment(Value& self) const;
        virtual void prefix_decrement(Value& self) const;
        
        // pure binary
        virtual Value multiplication(Value) const;
        virtual Value division(Value) const;
        virtual Value remainder(Value) const;
        virtual Value addition(Value) const;
        virtual Value subtraction(Value) const;
        virtual Value left_shift(Value) const;
        virtual Value right_shift(Value) const;
        virtual Value bitwise_and(Value) const;
        virtual Value bitwise_xor(Value) const;
        virtual Value bitwise_or(Value) const;
        
        // mutating binary
        
        // since numbers on the heap are immutable, there seems to be no
        // meaningful customization possible here; they will just have tp
        // do the basic operation and replace their handle with it
        virtual void assigned_multiplication(Value&, Value) const;
        virtual void assigned_division(Value&, Value) const;
        virtual void assigned_remainder(Value&, Value) const ;
        virtual void assigned_addition(Value&, Value) const;
        virtual void assigned_subtraction(Value&, Value) const;
        virtual void assigned_left_shift(Value&, Value) const;
        virtual void assigned_right_shift(Value&, Value) const;
        virtual void assigned_bitwise_and(Value&, Value) const;
        virtual void assigned_bitwise_xor(Value&, Value) const;
        virtual void assigned_bitwise_or(Value&, Value) const;

        // odd cases
        virtual Value function_call() const;
        virtual Value subscript_const(Value other) const;
        virtual DeferredElementAccess subscript_mutable(Value& self, Value other);
        
        // built-in functions
        virtual String str() const;
        
    };
    
    struct LeafValueInterface : Object {
        void _gc_shade() const override final {
            _gc_shade_for_leaf(&this->_gc_color);
        }
        void gc_enumerate() const override final {
        }
    };
    
    // TODO: upgrade to array of limbs of arbitrary precision integer
    struct HeapInt64 : LeafValueInterface {
        
        std::int64_t _integer;
        
        std::size_t gc_hash() const {
            return std::hash<std::int64_t>()(_integer);
        }
        
        std::size_t gc_bytes() const {
            return sizeof(HeapInt64);
        }
        
        explicit HeapInt64(std::int64_t z)
        : _integer(z) {
            printf("%p new %" PRId64 "\n", this, _integer);
        }
        
        std::int64_t as_int64_t() const {
            return _integer;
        }
        
        virtual ~HeapInt64() {
            printf("%p del %" PRId64 "\n", this, _integer);
        }
        
    };
    
    struct HeapString : LeafValueInterface {
        
        std::size_t _hash;
        std::size_t _size;
        char _bytes[0];

        static void* operator new(std::size_t count, std::size_t extra) {
            return allocate(count + extra);
        }
        
        static HeapString* make(const char* first,
                                  const char* last,
                                  std::size_t hash) {
            std::ptrdiff_t n = last - first;
            HeapString* p = new(n) HeapString;
            p->_hash = std::hash<std::string_view>()(std::string_view(first, n));
            p->_size = n;
            std::memcpy(p->_bytes, first, n);
            printf("%p new \"%.*s\"\n", p, (int)p->_size, p->_bytes);
            return p;
        }
        
        std::size_t gc_bytes() const override final {
            return sizeof(HeapString) + _size;
        }
        
        std::size_t gc_hash() const override final {
            return _hash;
        }

        std::string_view as_string_view() const {
            return std::string_view(_bytes, _size);
        }
        
        virtual ~HeapString() {
            printf("%p del \"%.*s\"\n", this, (int)_size, _bytes);
        }
        
    }; // struct ObjectString
        
    
    
    
    
    struct alignas(8) Value {
        
        // these are storage types, not logical types;
        // e.g. a short string that is packed into a word is tag STRING,
        // but a long string that lives on the heap is tag OBJECT -> ObjectString
        
        enum : std::uintptr_t {
            OBJECT = 0,
            INTEGER = 1,
            STRING = 3,
            BOOLEAN = 4,
            ENUMERATION = 5,
            ERROR = 6, // ?
            // enum as (type code, value code) ?
            // null?
            
            TAG_MASK = 0x000000000000000F,
            PTR_MASK = 0x00007FFFFFFFFFF0,
            
        };
        
        unsigned char _raw[8];
        
        int _get_tag() const {
            return _raw[0] & TAG_MASK;
        }
        
        int _size() const {
            return _raw[0] >> 4;
        }
        
        bool _is_object() const {
            return _get_tag() == OBJECT;
        }
        
        bool _is_integer() const {
            return _get_tag() == INTEGER;
        }
        
        bool _is_string() const {
            return _get_tag() == STRING;
        }
        
        bool _is_boolean() const {
            return _get_tag() == BOOLEAN;
        }
        
        const HeapValue* _as_object() const {
            assert(_is_object());
            const HeapValue* a;
            std::memcpy(&a, _raw, 8);
            return a;
        }
                
        std::int32_t _as_integer() const {
            assert(_is_integer());
            std::int32_t a;
            std::memcpy(&a, _raw + 4, 4);
            return a;
        }
        
        std::string_view _as_string() const {
            assert(_is_string());
            return std::string_view(reinterpret_cast<const char*>(_raw + 1),
                                    _size());
        }
        
        bool _as_boolean() const {
            return _raw[4];
        }

        std::size_t hash() {
            switch (_get_tag()) {
                case OBJECT: {
                    const HeapValue* a = _as_object();
                    return a ? a->gc_hash() : 0;
                }
                case INTEGER: {
                    std::int64_t a = _as_integer();
                    return std::hash<std::int64_t>()(a);
                }
                case STRING: {
                    return std::hash<std::string_view>()(_as_string());
                }
                case BOOLEAN: {
                    return std::hash<bool>()(_as_boolean());
                }
                default:
                    abort();
            }
        }
        
        static Value from_object(const HeapValue* object) {
            Value result;
            std::memcpy(result._raw, &object, 8);
            assert(result._is_object());
            return result;
        }
        
        static Value from_int64(std::int64_t z) {
            Value result;
            std::int32_t y = (std::int32_t)z;
            if (y == z) {
                std::int32_t a[2] = { INTEGER, y };
                std::memcpy(result._raw, &a, 8);
            } else {
                HeapInt64* a = new HeapInt64(z);
                std::memcpy(result._raw, &a, 8);
            }
            return result;
        }

        static Value from_ntbs(const char* ntbs) {
            Value result;
            std::size_t n = std::strlen(ntbs);
            if (n < 8) {
                result._raw[0] = STRING | (n << 4);
                std::memcpy(result._raw + 1, ntbs, n);
                assert(result._is_string());
            } else {
                std::size_t hash = std::hash<std::string_view>()(std::string_view(ntbs, n));
                HeapString* a = HeapString::make(ntbs, ntbs + n, hash);
                std::memcpy(result._raw, &a, 8);
                assert(result._is_object());
            }
            return result;
        }
        
        static Value from_boolean(bool flag) {
            Value result;
            unsigned char a[8] = { BOOLEAN, 0, 0, 0, flag, 0, 0, 0 };
            std::memcpy(result._raw, a, 8);
            assert(result._is_boolean());
            return result;
        }
        
        static Value make_error() {
            __builtin_trap();
            std::uint64_t a = ERROR;
            Value result;
            std::memcpy(&result, &a, 8);
            return result;
        }


        Value operator++(int);
        Value operator--(int);
        Value operator()() const;
        Value operator[](Value) const;
        Value& operator[](Value);

        Value& operator++();
        Value& operator--();
        Value operator+() const;
        Value operator-() const;
        bool operator!() const;
        Value operator~() const;
        explicit operator bool() const;
        
        Value operator*(Value) const;
        Value operator/(Value) const;
        Value operator%(Value) const;

        Value operator+(Value) const;
        Value operator-(Value) const;

        Value operator<<(Value) const;
        Value operator>>(Value) const;
        
        std::partial_ordering operator<=>(Value) const;
        
        bool operator==(Value) const;
        
        Value operator&(Value) const;
        Value operator^(Value) const;
        Value operator|(Value) const;

        Value& operator+=(Value);
        Value& operator-=(Value);
        Value& operator*=(Value);
        Value& operator/=(Value);
        Value& operator%=(Value);
        Value& operator<<=(Value);
        Value& operator>>=(Value);
        Value& operator&=(Value);
        Value& operator^=(Value);
        Value& operator|=(Value);

    };
    
    inline void shade(Value value) {
        if (value._is_object()) {
            shade(value._as_object());
        }
    }
    
    struct Number {
        
        Value _value_that_is_a_number;
        
        operator Value() const { // upcast
            return _value_that_is_a_number;
        }
        
        std::int64_t as_int64_t() const {
            switch (_value_that_is_a_number._get_tag()) {
                case Value::OBJECT: {
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
    
    struct String {
        
        Value _value_that_is_a_string; // nasty
        
        operator Value() const { // upcast
            return _value_that_is_a_string;
        }
        
        std::string_view as_string_view() const {
            switch (_value_that_is_a_string._get_tag()) {
                case Value::OBJECT: {
                    const Object* a = _value_that_is_a_string._as_object();
                    assert(dynamic_cast<const HeapString*>(a));
                    const HeapString* b = static_cast<const HeapString*>(a);
                    return b->as_string_view();
                }
                case Value::STRING: {
                    return _value_that_is_a_string._as_string();
                }
                default:
                    abort();
            }
        }
        
    };
    
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
    // This object is not needed for resiable arrays of not-potential-gc-
    // pointers, which do not need to be scanned at all and are just held and
    // managed directly perhaps in a std::vector, or small arrays of fixed
    // size, which can use the fma pattern
    struct IndirectFixedCapacityArray : Object {
        
        std::size_t _size;
        std::atomic<std::uintptr_t>* _storage; // TODO: type?
        
        explicit IndirectFixedCapacityArray(std::size_t count)
        : _size(count)
        , _storage((std::atomic<std::uintptr_t>*) calloc(count, 8)) {
            printf("%p new byte[%zd]\n", this, _size * 8);
        }
        
        virtual ~IndirectFixedCapacityArray() override {
            free(_storage);
            printf("%p del byte[%zd]\n", this, _size * 8);
        }
        
        virtual std::size_t gc_bytes() const override {
            return sizeof(IndirectFixedCapacityArray) + _size * 8;
        }
        
        virtual void gc_enumerate() const override {
            auto first = _storage;
            auto last = first + _size;
            for (; first != last; ++first) {
                std::uintptr_t a = first->load(std::memory_order_acquire);
                if (!(a & 0x000000000000000F)) {
                    // the value encodes a pointer
                    trace(reinterpret_cast<const Object*>(a));
                }
            }
        }
        
    };
    
    // This is a std::vector-like object that is garbage collected
    // It is only "concurrent enough" for GC; it does not support access by
    // multiple mutators.
    //
    // Notably it is only amortized O(1), and has a worst case O(N).  As such
    // it is unsuitable for general use in soft real time contexts, but is
    // still useful for things that have some kind of moderate bounded size,
    // and as a stepping stone to more advanced data structures.
    
    struct HeapArray : HeapValue {
        mutable std::size_t _size;
        mutable std::size_t _capacity;
        mutable std::atomic<std::uintptr_t>* _storage; // TODO: type?
        mutable Pointer<IndirectFixedCapacityArray> _storage_manager;
        
        HeapArray() 
        : _size(0)
        , _capacity(0)
        , _storage(0)
        , _storage_manager(nullptr) {
            printf("%p new Value[]\n", this);
        }
        
        virtual ~HeapArray() override {
            printf("%p del Value[%zd]\n", this, _size);
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
                auto b = new IndirectFixedCapacityArray(a);
                auto c = b->_storage;
                std::memcpy(c, _storage, _size * 8);
                _capacity = a;
                _storage = b->_storage;
                _storage_manager = b;
            }
            std::uintptr_t d;
            std::memcpy(&d, value._raw, 8);
            // Safety: we don't have to execute the write barrier here I think
            // - either it is new
            // - or it came from another container that held it at some point
            // in this cycle
            _storage[_size++].store(d, std::memory_order_release);
        }
        
        bool empty() const {
            return !_size;
        }
        
        void pop_back() const {
            assert(_size);
            std::uintptr_t a = _storage[--_size].exchange(0, std::memory_order_relaxed);
            if (!(a & 0x000000000000000F)) {
                shade(reinterpret_cast<const Object*>(a));
            }
        }
        
        std::size_t size() const {
            return _size;
        }
        
    };
    
    
    struct Array {
        
        // not a Value because there is only one representation possible (so far)
        HeapArray* _array;
        
        std::size_t size() const {
            return _array->_size;
        }
        
        Value operator[](std::size_t pos) const {
            assert(pos < _array->_size);
            std::uintptr_t a = _array->_storage[pos].load(std::memory_order_relaxed);
            Value b;
            std::memcpy(&b, &a, 8);
            return b;
        }
        
        
    };

    
    
    
    struct DeferredElementAccess {
        Value& self;
        Value pos;
    };
    
    
    
    
    
    template<typename F>
    auto visit(Value value, F&& f) {
        switch (value._get_tag()) {
            case Value::OBJECT: {
                const Object* a = value._as_object();
                const HeapInt64* b = dynamic_cast<const HeapInt64*>(a);
                if (b) {
                    Number c;
                    c._value_that_is_a_number = value;
                    return f(c);
                }
                const HeapString* c = dynamic_cast<const HeapString*>(a);
                if (c) {
                    String d;
                    d._value_that_is_a_string = value;
                    return f(d);
                }
                abort();
            }
            case Value::INTEGER: {
                Number a;
                a._value_that_is_a_number = value;
                return f(a);
            }
            case Value::STRING: {
                String a;
                a._value_that_is_a_string = value;
                return f(a);
            }
            default:
                abort();
        }
    }
    
    
    inline void foo() {
        
        // the heap-allocated objects will live until the next handshake so
        // they will live beyond the end of this function even without being
        // marked-as-roots anywhere
        
        Value a = Value::from_ntbs("hello"); // short string
        Value b = Value::from_ntbs("long kiss goodbye"); // long string
        
        assert(a._is_string()); // packed into value
        assert(b._is_object()); // on the heap

        // hack type interrogation
        String c; c._value_that_is_a_string = a;
        String d; d._value_that_is_a_string = b;
        
        auto e = c.as_string_view();
        printf("%.*s\n", (int)e.size(), e.data());
        auto f = d.as_string_view();
        printf("%.*s\n", (int)f.size(), f.data());
        
        Value z = Value::from_int64(-7);
        Value y = Value::from_int64(-777777777777777);
        
        Number x; x._value_that_is_a_number = z;
        Number w; w._value_that_is_a_number = y;
        
        printf("%" PRId64 "\n", x.as_int64_t());
        printf("%" PRId64 "\n", w.as_int64_t());
        
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
        v->push_back(w);
        v->push_back(x);



    }
   
   
    

    

    
    
    
    inline Value& Value::operator++() {
        switch (_get_tag()) {
            case OBJECT: {
                _as_object()->prefix_increment(*this);
                break;
            }
            case INTEGER: {
                int64_t b = _as_integer();
                *this = from_int64(_as_integer() + 1);
                break;
            }
            default:
                *this = make_error();
                break;
        }
        return *this;
    }
    
    inline Value Value::operator++(int) {
        Value old = *this;
        switch (_get_tag()) {
            case OBJECT: {
                _as_object()->postfix_increment(*this);
                break;
            }
            case INTEGER: {
                *this = from_int64(_as_integer() + 1);
                break;
            }
            default: {
                *this = Value::make_error();
                break;
            }
        }
        return old;
    }
    
    inline Value& Value::operator+=(Value other) {
        switch (_get_tag()) {
            case OBJECT: {
                _as_object()->assigned_addition(*this, other);
                break;
            }
            case INTEGER: {
                if (other._get_tag() == INTEGER) {
                    *this = Value::from_int64((std::int64_t)_as_integer()
                                              + (std::int64_t)other._as_integer());
                } else {
                    // int32 + ??? -> need more functions
                    abort();
                }
                break;
            }
            default: {
                *this = Value::make_error();
                break;
            }
        }
        return *this;
    }
    
    inline Value Value::operator+(Value other) const {
        switch (_get_tag()) {
            case OBJECT: {
                return _as_object()->addition(other);
            }
            case INTEGER: {
                if (other._get_tag() == INTEGER) {
                    return Value::from_int64((std::int64_t)_as_integer()
                                             + (std::int64_t)other._as_integer());
                } else {
                    // int32 + ??? -> need more functions
                    abort();
                }
            }
            default: {
                return Value::make_error();
            }
        }
    }
    
    inline Value Value::operator*(Value) const { abort(); }
    inline Value Value::operator/(Value) const { abort(); }
    inline Value Value::operator%(Value) const { abort(); }
    inline Value Value::operator-(Value) const { abort(); }
    inline Value Value::operator<<(Value) const { abort(); }
    inline Value Value::operator>>(Value) const { abort(); }
    inline Value Value::operator&(Value) const { abort(); }
    inline Value Value::operator^(Value) const { abort(); }
    inline Value Value::operator|(Value) const { abort(); }

    inline Value& Value::operator*=(Value) { abort(); }
    inline Value& Value::operator/=(Value) { abort(); }
    inline Value& Value::operator%=(Value) { abort(); }
    inline Value& Value::operator-=(Value) { abort(); }
    inline Value& Value::operator&=(Value) { abort(); }
    inline Value& Value::operator^=(Value) { abort(); }
    inline Value& Value::operator|=(Value) { abort(); }
    inline Value& Value::operator<<=(Value) { abort(); }
    inline Value& Value::operator>>=(Value) { abort(); }
    
    inline Value& Value::operator--() { abort(); }
    inline Value Value::operator--(int) { abort(); }

    
    
    
} // namespace gc

#endif /* value_hpp */
