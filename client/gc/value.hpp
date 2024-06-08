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
    struct Enumeration;
    struct Error;
    struct Number;
    struct String;
    struct Table;

    struct HeapValue;
    struct HeapInt64;
    struct HeapString;
    struct HeapArray;
    struct HeapTable;
    struct HeapIndirection;
    
    struct alignas(8) Value {
        
        // these are storage types, not logical types;
        // e.g. a short string that is inlined is tag STRING,
        // but a long string that lives on the heap is tag POINTER -> HeapString
        
        enum {
            POINTER = 0,
            INTEGER = 1,
            STRING = 3,
            BOOLEAN = 4,
            ENUMERATION = 5,
            ERROR = 6,
            CHARACTER = 7,
            TOMBSTONE = 15,
        };
                        
        struct _string_t {
            char _tag_size;
            char _chars[7];
            int size() const {
                return _tag_size >> 4;
            }
            std::string_view as_string_view() const {
                return std::string_view(_chars, size());
            }
        };
        
        struct _character_t {
            int _tag;
            int32_t _character; // UTF-32
        };
        
        struct _boolean_t {
            int _tag;
            bool boolean;
        };
        
        union {
            int _tag;
            const HeapValue* _pointer;
            std::int64_t _integer;
            _string_t _string;
            _boolean_t _boolean;
            std::uint64_t _enumeration;
        };
                
        int _masked_tag() const { return _tag & 15; }
        bool _is_pointer() const { return _masked_tag() == POINTER; }
        bool _is_integer() const { return _masked_tag() == INTEGER; }
        bool _is_string() const { return _masked_tag() == STRING; }
        bool _is_boolean() const { return _masked_tag() == BOOLEAN; }
        bool _is_enumeration() const { return _masked_tag() == ENUMERATION; }
        bool _is_error() const { return _masked_tag() == ERROR; }

        bool _is_empty() const { return !_pointer; }
        bool _is_tombstone() const { return _masked_tag() == TOMBSTONE; }

        
        Value() = default;
        
        // implicit copy and move constructors
        
        Value(std::nullptr_t) : _enumeration(0) {}
        Value(const HeapValue* p) : _pointer(p) {}
        Value(const char* ntbs) { *this = Value::from_ntbs(ntbs); }
        Value(int i) : _integer(((std::int64_t)i << 4) | INTEGER) {}
        
        // implicit destructor

        // implicit copy and move assignment operators
        
        const HeapValue* _as_pointer() const {
            assert(_is_pointer());
            return _pointer;
        }
        
        const HeapValue* as_pointer_or_nullptr() {
            return _is_pointer() ? _pointer : nullptr;
        }
        
        std::int64_t _as_integer() const {
            assert(_is_integer());
            return _integer >> 4;
        }
        
        std::string_view _as_string() const {
            assert(_is_string());
            return _string.as_string_view();
        }
        
        bool _as_boolean() const {
            return _boolean.boolean;
        }
        
        
        static Value from_object(const HeapValue* object);
        static Value from_int64(std::int64_t z);
        static Value from_ntbs(const char* ntbs);
        static Value from_boolean(bool flag);

        // monostates
        static Value make_error();
        static Value make_empty() { Value result; result._enumeration = 0; return result; }
        static Value make_tombstone() { Value result; result._enumeration = TOMBSTONE; return result; }
                
        // these operators must be defined inline
        Value operator()() const;
        Value operator[](Value) const;
        Value& operator[](Value);
        explicit operator bool() const;
        
        // common interface functions
        std::size_t hash() const;
        
        std::size_t size() const;
        bool contains(Value key) const;
        Value find(Value key) const;
        
        void resize(Value);
        Value insert_or_assign(Value key, Value value);
        Value erase(Value key);
        

        
    }; // Value
        
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
    
    void trace(Value a);
    
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
    
    inline void trace(const Traced<Value>& x) {
        trace(x._value.load(std::memory_order_acquire));
    }
    
    struct String {
        
        union {
            int _tag;
            const HeapString* _pointer;
            Value::_string_t _string;
        };
        
        
        int _masked_tag() const { return _tag & 15; }
        bool _is_pointer() const { return _masked_tag() == Value::POINTER; }
        bool _is_string() const { return _masked_tag() == Value::STRING; }
        
        const HeapString* _as_pointer() const {
            assert(_is_pointer());
            return _pointer;
        }
        
        const HeapString* as_pointer_else_nullptr() {
            return _is_pointer() ? _pointer : nullptr;
        }
        
        operator Value() const { // upcast
            Value result;
            result._string = _string;
            return result;
        }
        
        std::string_view as_string_view() const;
        
    };
    
    struct Table {
        
        const HeapTable* _pointer;
        
        operator Value() const {
            Value result;
            result._enumeration = (std::uint64_t)_pointer;
            return result;
        };
        
        std::size_t size() const;
        bool contains(Value key) const;
        Value find(Value key) const;
        Value erase(Value key);
        Value insert_or_assign(Value key, Value value);
        
    };
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
   

    
    
    

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
    
    
    
    
    inline void shade(Value value) {
        if (value._is_pointer()) {
            shade(value._as_pointer());
        }
    }

    
    
    
    
    
    
    
    
    
    struct LeafValueInterface : HeapValue {
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
        switch (value._masked_tag()) {
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
            case Value::INTEGER: {
                return f(value._as_integer());
            }
            case Value::STRING: {
                return f(value._as_string());
            }
            case Value::BOOLEAN: {
                return f(value._as_boolean());
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

} // namespace gc

#endif /* value_hpp */
