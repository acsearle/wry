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
    
    struct Value;

    struct Array;
    struct Boolean;
    struct Character;
    struct Enumeration;
    struct Number;
    struct String;
    struct Table;

    struct HeapValue;

    struct HeapArray;
    struct HeapInt64;
    struct HeapNumber;
    struct HeapString;
    struct HeapTable;
    
    struct _deferred_subscript_t;
    
    struct alignas(8) Value {
        
        // these are storage types, not logical types;
        // e.g. a short string that is inlined is tag STRING,
        // but a long string that lives on the heap is tag POINTER -> HeapString
        
        enum {
            POINTER = 0,
            SMALL_INTEGER = 1,
            SHORT_STRING = 3,
            BOOLEAN = 4,
            ENUMERATION = 5,
            ERROR = 6,
            CHARACTER = 7,
            TOMBSTONE = 15,
        };
        
        enum {
            SHIFT = 4,
            MASK = 15,
        };
        
        enum : std::uint64_t {
            VALUE_NULL = 0,
            VALUE_ZERO = SMALL_INTEGER,
            VALUE_EMPTY_STRING = SHORT_STRING,
            VALUE_FALSE = BOOLEAN,
            VALUE_TRUE = BOOLEAN | (1l << 32),
            VALUE_ERROR = ERROR,
            VALUE_TOMBSTONE = TOMBSTONE,
        };
             
        struct _boolean_t {
            int _tag;
            bool boolean;
        };
        
        struct _character_t {
            int _tag;
            int32_t _character; // UTF-32
        };

        struct _short_string_t {
            char _tag_and_len;
            char _chars[7];
            char* data() { return _chars; }
            constexpr std::size_t size() const {
                assert((_tag_and_len & MASK) == SHORT_STRING);
                return _tag_and_len >> SHIFT;
            }
            constexpr std::string_view as_string_view() const {
                return std::string_view(_chars, size());
            }
            std::size_t hash() const {
                return std::hash<std::string_view>()(as_string_view());
            }
        };
        
        union {
            int _tag;
            const HeapValue* _pointer;
            std::int64_t _integer;
            _short_string_t _short_string;
            _boolean_t _boolean;
            std::int64_t _enumeration;
            std::uint64_t _raw;
        };
        
        int _discriminant() const { return _tag & MASK; }
        bool _is_small_integer() const { return (_tag & MASK) == SMALL_INTEGER; }
        bool _is_pointer() const { return (_tag & MASK) == POINTER; }
        bool _is_short_string() const { return (_tag & MASK) == SHORT_STRING; }
        bool _is_tombstone() const { return (_tag & MASK) == TOMBSTONE; }

        // these logical types are always stored inline
        bool is_enumeration() const { return (_tag & MASK) == ENUMERATION; }
        bool is_null() const { return !_pointer; }
        bool is_error() const { return (_tag & MASK) == ERROR; }
        bool is_boolean() const { return (_tag & MASK) == BOOLEAN; }
        bool is_character() const { return _discriminant() == CHARACTER; }
        
        // Several types have only a small number of values, we can pack
        // them all into a single tag?
        // true, false, error, tombstone, UTF-32 character
        
        constexpr Value() = default;
        
        // implicit copy and move constructors
        
        constexpr Value(std::nullptr_t) : _raw(0) {}
        constexpr Value(bool flag) : _integer(BOOLEAN) { _boolean.boolean = flag; }
        Value(const char* ntbs) { *this = Value::from_ntbs(ntbs); }
        template<std::size_t N, typename = std::enable_if_t<(N > 0)>> constexpr Value(const char (&ntbs)[N]);
        Value(int i) : _integer(((std::int64_t)i << SHIFT) | SMALL_INTEGER) {}
        
        // implicit destructor

        // implicit copy and move assignment operators
        
        const HeapValue* _as_pointer() const {
            assert(_is_pointer());
            return _pointer;
        }
        
        const HeapValue* _as_pointer_or_nullptr() {
            return _is_pointer() ? _pointer : nullptr;
        }
        
        std::int64_t _as_small_integer() const {
            assert(_is_small_integer());
            return _integer >> SHIFT;
        }
        
        std::string_view _as_short_string() const {
            assert(_is_short_string());
            return _short_string.as_string_view();
        }
        
        bool as_boolean() const {
            assert(is_boolean());
            return _boolean.boolean;
        }
        
        std::int64_t as_enumeration() const {
            assert(is_enumeration());
            return _enumeration >> 4;
        }
        
        
        static Value _from_object(const HeapValue* object);
        static Value from_int64(std::int64_t z);
        static Value from_ntbs(const char* ntbs);
        static Value from_bool(bool flag);

        // monostates
        constexpr static Value make_error();
        constexpr static Value make_null();
        constexpr static Value make_tombstone();
                        
        // common interface functions
        std::size_t hash() const;
        
        std::size_t size() const;
        bool contains(Value key) const;
        Value find(Value key) const;
        
        void resize(Value);
        Value insert_or_assign(Value key, Value value);
        Value erase(Value key);

        // these operators must be defined inline
        Value operator()() const;
        Value operator[](Value) const;
        explicit operator bool() const;
        
        _deferred_subscript_t operator[](Value);

    }; // Value
    
    struct _deferred_subscript_t {
        Value& container;
        Value key;
        operator Value() &&;
        _deferred_subscript_t&& operator=(Value desired) &&;
        _deferred_subscript_t&& operator=(_deferred_subscript_t&& desired) &&;
    };

    
    
    
    
    
        

    template<>
    struct Traced<Value> {
        
        std::atomic<Value> _value;

        Traced() = default;
        Traced(const Traced& other);
        ~Traced() = default;
        Traced& operator=(const Traced& other);
        explicit Traced(const Value& other);
        Traced& operator=(const Value& other);
        explicit operator bool() const;
        operator Value() const;
        bool operator==(const Traced& other) const;
        auto operator<=>(const Traced& other) const;
        Value get() const;
    };
    
    template<>
    struct Traced<Atomic<Value>> {
        // ...
    };
    
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
            Value::_short_string_t _string;
        };
        int _discriminant() const { return _tag & Value::MASK; }
        bool _is_pointer() const { return _discriminant() == Value::POINTER; }
        bool _is_short_string() const { return _discriminant() == Value::SHORT_STRING; }
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
    
    
    
    
    
    
    
    
    void trace(Value a);

    
    
    
    
    
   

    
    
    


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
        virtual const HeapTable* as_HeapTable() const;

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
        // virtual Value subscript_const(Value other) const;
        // virtual DeferredElementAccess subscript_mutable(Value& self, Value other);
        //virtual
        
        // built-in functions
        virtual String str() const;
                
        // common interface
        virtual std::size_t size() const;
        virtual bool contains(Value) const;
        virtual Value find(Value) const;
        virtual Value erase(Value) const;
        virtual Value insert_or_assign(Value, Value) const;

    };
    
    
    struct HeapLeaf : HeapValue {
        void _gc_shade() const override final {
            _gc_shade_for_leaf(&this->_gc_color);
        }
        void gc_enumerate() const override final {
            // leaf has no traceable children
        }
    };
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    // TODO: upgrade to array of limbs of arbitrary precision integer
    struct HeapInt64 : HeapLeaf {
        
        std::int64_t _integer;
        
        std::size_t gc_hash() const {
            return std::hash<std::int64_t>()(_integer);
        }
        
        std::size_t gc_bytes() const {
            return sizeof(HeapInt64);
        }
        
        explicit HeapInt64(std::int64_t z)
        : _integer(z) {
            // printf("%p new %" PRId64 "\n", this, _integer);
        }
        
        std::int64_t as_int64_t() const {
            return _integer;
        }
        
        virtual ~HeapInt64() {
            // printf("%p del %" PRId64 "\n", this, _integer);
        }
        
    };
    
    struct HeapString : HeapLeaf {
        
        std::size_t _hash;
        std::size_t _size;
        char _bytes[0];

        static void* operator new(std::size_t count, std::size_t extra) {
            return allocate(count + extra);
        }
        
        static HeapString* make(std::string_view v,
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
        
        static HeapString* make(std::string_view v) {
            return make(v, std::hash<std::string_view>()(v));
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
            // printf("%p del \"%.*s\"\n", this, (int)_size, _bytes);
        }
        
    }; // struct ObjectString
        
    
    
    
    
        
   
        
    
    
    // This is a std::vector-like object that is garbage collected
    // It is only "concurrent enough" for GC; it does not support access by
    // multiple mutators.
    //
    // Notably it is only amortized O(1), and has a worst case O(N).  As such
    // it is unsuitable for general use in soft real time contexts, but is
    // still useful for things that have some kind of moderate bounded size,
    // and as a stepping stone to more advanced data structures.
    

    
    
    
    struct DeferredElementAccess {
        Value& self;
        Value pos;
    };
    
    
    
    
    
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
    
    
    
    
    
    void foo();
   
    

    

    
    
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
    
    
    
    
    
    inline void trace(Value a) {
        if (a._is_pointer())
            trace(a._as_pointer());
    }

    
    Value operator++(Value&, int);
    Value operator--(Value&, int);
    Value& operator++(Value&);
    Value& operator--(Value&);
    Value operator+(const Value&) ;
    Value operator-(const Value&) ;
    bool operator!(const Value&) ;
    Value operator~(const Value&) ;
    
    Value operator*(const Value&, const Value&) ;
    Value operator/(const Value&, const Value&) ;
    Value operator%(const Value&, const Value&) ;
    
    Value operator+(const Value&, const Value&) ;
    Value operator-(const Value&, const Value&) ;
    
    Value operator<<(const Value&, const Value&) ;
    Value operator>>(const Value&, const Value&) ;
    
    std::partial_ordering operator<=>(const Value&, const Value&) ;
    
    bool operator==(const Value&, const Value&) ;
    
    Value operator&(const Value&, const Value&) ;
    Value operator^(const Value&, const Value&) ;
    Value operator|(const Value&, const Value&) ;
    
    Value& operator+=(Value&, const Value&);
    Value& operator-=(Value&, const Value&);
    Value& operator*=(Value&, const Value&);
    Value& operator/=(Value&, const Value&);
    Value& operator%=(Value&, const Value&);
    Value& operator<<=(Value&, const Value&);
    Value& operator>>=(Value&, const Value&);
    Value& operator&=(Value&, const Value&);
    Value& operator^=(Value&, const Value&);
    Value& operator|=(Value&, const Value&);
    
    
    
    inline void trace(const Traced<Value>& x) {
        trace(x._value.load(std::memory_order_acquire));
    }

    
    template<std::size_t N, typename>
    constexpr Value::Value(const char (&ntbs)[N]) {
        const std::size_t M = N - 1;
        assert(ntbs[M] == '\0');
        if (M < 8) {
            _short_string._tag_and_len = (M << 4) | SHORT_STRING;
            __builtin_memcpy(_short_string._chars, ntbs, M);
        } else {
            _pointer = HeapString::make(ntbs);
        }
    }
    
    // user defined literals
    
    String operator""_v(const char* s, std::size_t n);
    
    void shade(Value value);
       
} // namespace gc

#endif /* value_hpp */
