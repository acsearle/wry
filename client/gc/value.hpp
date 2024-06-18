//
//  value.hpp
//  client
//
//  Created by Antony Searle on 31/5/2024.
//

#ifndef value_hpp
#define value_hpp

#include <cassert>
#include <string_view>

#include "atomic.hpp"
#include "object.hpp"
#include "traced.hpp"

namespace wry::gc {
    
    using std::string_view;
        
    struct _value_subscript_result_t;
    
    struct Value {
        
        uint64_t _data;
                        
        constexpr Value() = default;
        constexpr Value(nullptr_t);
        constexpr Value(bool);
        constexpr Value(int);
        constexpr Value(int64_t);
        constexpr Value(const char*);
        template<std::size_t N, typename = std::enable_if_t<(N > 0)>>
        constexpr Value(const char (&ntbs)[N]);
        Value(string_view);
                        
        Value operator()(/* args type? */) const;
        Value operator[](Value) const;
        explicit operator bool() const;
        
        _value_subscript_result_t operator[](Value);
        
        bool is_opcode() const;
        int as_opcode() const;
        
        bool is_int64_t() const;
        int64_t as_int64_t() const;
        
        bool is_Empty() const;

    }; // Value

    // gc methods
    
    void value_debug(const Value&);
    void value_shade(Value value);
    void value_trace(Value);


    Value value_make_boolean_with(bool flag);
    Value value_make_character_with(int utf32);
    Value value_make_enumeration_with(int64_t);
    Value value_make_error();
    Value value_make_error_with(const char*);
    Value value_make_false();
    Value value_make_integer_with(int64_t z);
    Value value_make_null();
    Value value_make_string_with(const char* ntbs);
    Value value_make_string_with(string_view);
    Value value_make_table();
    Value value_make_true();
    Value value_make_zero();
    Value value_make_one();
    constexpr Value value_make_opcode(int);

    Value value_make_deep_copy(const Value&);
    
    bool value_is_boolean(const Value& self);
    bool value_is_character(const Value& self);
    bool value_is_enumeration(const Value& self);
    bool value_is_error(const Value& self);
    bool value_is_null(const Value& self);
        
    bool value_as_boolean(const Value& self);
    bool value_as_boolean_else(const Value& self, bool);
    int  value_as_character(const Value& self);
    int  value_as_character_else(const Value& self, int);
    int64_t value_as_enumeration(const Value& self);
    int64_t value_as_enumeration_else(const Value& self, int64_t);
    int64_t value_as_int64_t(const Value& self);
    int64_t value_as_int64_t_else(const Value& self, int64_t);
    string_view value_as_string_view(const Value& self);
    string_view value_as_string_view_else(const Value& self, string_view);
    int value_as_opcode(const Value& self);

    bool value_contains(const Value& self, Value key);
    void value_resize(Value& self, Value count);
    Value value_find(const Value& self, Value key);
    Value value_insert_or_assign(Value& self, Value key, Value value);
    Value value_erase(Value& self, Value key);
    size_t value_hash(const Value&);
    size_t value_size(const Value&);

    // non-member operator overloads
    
    Value operator++(Value&, int);
    Value operator--(Value&, int);
    Value& operator++(Value&);
    Value& operator--(Value&);
    Value operator+(const Value&) ;
    Value operator-(const Value&) ;
    // bool operator!(const Value&) ;
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
    
    
    
    template<>
    struct Traced<Value> {
        
        Atomic<Value> _atomic_value;

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
    
    void value_trace(const Traced<Value>& self);
    
    template<>
    struct Traced<Atomic<Value>> {
        
        Atomic<Value> _atomic_value;
        
        constexpr Traced() = default;
        constexpr explicit Traced(Value);
        Traced(const Traced&) = delete;
        Traced& operator=(const Traced&) = delete;
        
        Value load(Ordering) const;
        void store(Value, Ordering);
        Value exchange(Value, Ordering);
        bool compare_exchange_weak(Value&, Value, Ordering, Ordering);
        bool compare_exchange_strong(Value&, Value, Ordering, Ordering);

    };
    
    void value_trace(const Traced<Atomic<Value>>& self);

    
    

    
    
    
    
    
   

    
    
    
    
    
    
    
    int _value_tag(const Value& self);
    bool _value_is_small_integer(const Value& self);
    bool _value_is_object(const Value& self);
    bool _value_is_short_string(const Value& self);
    bool _value_is_tombstone(const Value& self);
    
    const Object* _value_as_object(const Value& self);
    const Object* _value_as_object_else(const Value& self, const Object*);
    int64_t _value_as_small_integer(const Value& self);
    int64_t _value_as_small_integer_else(const Value& self, int64_t);
    std::string_view _value_as_short_string(const Value& self);
    std::string_view _value_as_short_string_else(const Value& self);
    Value _value_make_with(const Object* object);
    Value _value_make_tombstone();

    
    
    
    
    
    
    
    
    struct IndirectFixedCapacityValueArray : Object {
        
        std::size_t _capacity;
        Traced<Value>* _storage; // TODO: type?
        explicit IndirectFixedCapacityValueArray(std::size_t count);
        ~IndirectFixedCapacityValueArray()      ;
    }; // struct IndirectFixedCapacityValueArray

    
    
    // TODO: upgrade to array of limbs of arbitrary precision integer
    struct HeapInt64 : Object {
        std::int64_t _integer;
        explicit HeapInt64(std::int64_t z);
        std::int64_t as_int64_t() const;
    };
    
    struct HeapString : Object {
        std::size_t _hash;
        std::size_t _size;
        char _bytes[0];
        static void* operator new(std::size_t count, std::size_t extra);
        static HeapString* make(std::size_t hash, std::string_view view);
        static HeapString* make(std::string_view view);
        std::string_view as_string_view() const;
        HeapString();
    }; // struct HeapString
        
    
    
    
    
    
    
    
    
    
    
        
   
    
    
    
    void foo();
   
    

    

    
    
    
    
    inline void value_trace(Value a) {
        if (_value_is_object(a))
            object_trace(_value_as_object(a));
    }

    inline void value_trace(const Traced<Value>& self) {
        value_trace(self._atomic_value.load(Ordering::ACQUIRE));
    }

    inline void value_trace(const Traced<Atomic<Value>>& self) {
        value_trace(self._atomic_value.load(Ordering::ACQUIRE));
    }

   
    
    // user defined literals
    
    // String operator""_v(const char* s, std::size_t n);
    
    
    
    
    
    
    
    
    
    
    enum _value_tag_e {
        VALUE_TAG_OBJECT = 0,
        VALUE_TAG_SMALL_INTEGER = 1,
        VALUE_TAG_SHORT_STRING = 2,
        VALUE_TAG_BOOLEAN = 3,
        VALUE_TAG_ENUMERATION = 4,
        VALUE_TAG_ERROR = 5,
        VALUE_TAG_CHARACTER = 6,
        VALUE_TAG_OPCODE = 7,
        VALUE_TAG_SPECIAL = 15,
    };
    
    enum {
        VALUE_SHIFT = 4,
    };

    enum : uint64_t {
        VALUE_MASK = 0x000000000000000F,
        VALUE_POINTER_MASK = 0x00007FFFFFFFFFF0,
    };
    
    enum : uint64_t {
        VALUE_DATA_NULL = 0,
        VALUE_DATA_ZERO = VALUE_TAG_SMALL_INTEGER,
        VALUE_DATA_EMPTY_STRING = VALUE_TAG_SHORT_STRING,
        VALUE_DATA_FALSE = VALUE_TAG_BOOLEAN,
        VALUE_DATA_TRUE = VALUE_TAG_BOOLEAN | ((uint64_t)1 << VALUE_SHIFT),
        VALUE_DATA_ERROR = VALUE_TAG_ERROR,
        VALUE_DATA_TOMBSTONE = 0x0F,
        VALUE_DATA_OK = 0x1F,
        VALUE_DATA_NOTFOUND = 0x2F,
        VALUE_DATA_RESTART = 0x3F,

    };
    
    struct _short_string_t {
        char _tag_and_len;
        char _chars[7];
        char* data() { return _chars; }
        constexpr std::size_t size() const {
            assert((_tag_and_len & VALUE_MASK) == VALUE_TAG_SHORT_STRING);
            return _tag_and_len >> VALUE_SHIFT;
        }
        constexpr std::string_view as_string_view() const {
            return std::string_view(_chars, size());
        }
        std::size_t hash() const {
            return std::hash<std::string_view>()(as_string_view());
        }
    };
    
    
    
    
    constexpr Value::Value(std::nullptr_t) : _data(0) {}
    constexpr Value::Value(bool flag) : _data(((uint64_t)flag << VALUE_SHIFT) | VALUE_TAG_BOOLEAN) {}
    constexpr Value::Value(const char* ntbs) { *this = value_make_string_with(ntbs); }
    constexpr Value::Value(int i) : _data(((int64_t)i << VALUE_SHIFT) | VALUE_TAG_SMALL_INTEGER) {}

    template<std::size_t N, typename>
    constexpr Value::Value(const char (&ntbs)[N]) {
        const std::size_t M = N - 1;
        assert(ntbs[M] == '\0');
        if (M < 8) {
            _short_string_t s;
            s._tag_and_len = (M << VALUE_SHIFT) | VALUE_TAG_SHORT_STRING;
            // builtin for constexpr
            __builtin_memcpy(s._chars, ntbs, M);
            __builtin_memcpy(&_data, &s, 8);
        } else {
            _data = (uint64_t)HeapString::make(ntbs);
        }
    }
    
    
    
    struct _value_subscript_result_t {
        Value& container;
        Value key;
        operator Value() &&;
        _value_subscript_result_t&& operator=(Value desired) &&;
        _value_subscript_result_t&& operator=(_value_subscript_result_t&& desired) &&;
    };

    
    
    
    
    constexpr Value value_make_opcode(int code) {
        Value result;
        result._data = ((int64_t)code << VALUE_SHIFT) | VALUE_TAG_OPCODE;
        return result;
    }

    // TODO: fixme
    constexpr Value::Value(int64_t x) : _data((x << VALUE_SHIFT) | VALUE_TAG_SMALL_INTEGER) {}


    inline int value_as_opcode(const Value& self) {
        if (_value_tag(self) != VALUE_TAG_OPCODE)
            abort();
        return (int)((int64_t)(self._data) >> VALUE_SHIFT);
    }

    inline int64_t Value::as_int64_t() const {
        return (int64_t)_data >> VALUE_SHIFT;
    }
    
    inline int Value::as_opcode() const {
        return (int)as_int64_t();
    }
    
    inline bool Value::is_opcode() const {
        return _value_tag(*this) == VALUE_TAG_OPCODE;
    }
    
    inline bool Value::is_int64_t() const {
        return _value_tag(*this) == VALUE_TAG_SMALL_INTEGER;
    }
    
    inline bool Value::is_Empty() const {
        return !_data;
    }
    
    
    
    
    
    
    
    
} // namespace wry::gc

#endif /* value_hpp */
