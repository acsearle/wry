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
#include "garbage_collected.hpp"
#include "Scan.hpp"
//#include "adl.hpp"

namespace wry {
    
    // TODO: which string view?
    using std::string_view;
    
    struct _value_subscript_result_t;
    
    struct Value {
        
        uint64_t _data;
        
        // implicit construction from vocabulary types
        
        constexpr Value() = default;
        constexpr Value(const Value&) = default;
        constexpr Value(Value&) = default;
        constexpr Value(Value&&) = default;
        constexpr Value& operator=(const Value&) = default;
        
        constexpr Value(nullptr_t);
        constexpr Value(bool);
        constexpr Value(int);
        constexpr Value(int64_t);
        constexpr Value(const char*);
        template<std::size_t N, typename = std::enable_if_t<(N > 0)>>
        constexpr Value(const char (&ntbs)[N]);
        Value(string_view);
        
        // TODO: call syntax
        Value operator()(/* args type? */) const;
        
        // TODO: subscript syntax; probably get/set is the best.  We can't hand
        // out references into the backing array which will be atomic or
        // immutable
        Value operator[](Value) const;
        _value_subscript_result_t operator[](Value);
        
        // implicit conversion
        constexpr explicit operator bool() const;
        
        // TODO: make free functions
        constexpr bool is_opcode() const;
        constexpr int as_opcode() const;
        
        constexpr bool is_int64_t() const;
        constexpr int64_t as_int64_t() const;
        
        constexpr bool is_Empty() const;
        
    }; // struct Value
    
    void shade(const Value& value);
    void trace(const Value& value,void*p);
    
} // namespace wry

namespace wry {
    
    template<>
    struct Atomic<Value> {
        
        Atomic<std::uint64_t> _data;
        
        constexpr Atomic() = default;
        constexpr explicit Atomic(Value desired) : _data(desired._data) {}
        
        Value load(Ordering order) const {
            Value v;
            v._data = _data.load(order);
            return v;
        }
        
        Value exchange(Value desired, Ordering order) {
            shade(desired);
            desired._data = _data.exchange(desired._data, order);
            shade(desired);
            return desired;
        }
        
        void store(Value desired, Ordering order) {
            (void) exchange(desired, order);
        }
        
    };
    
}



namespace wry {
    
    
    constexpr Value value_make_boolean_with(bool flag);
    constexpr Value value_make_character_with(int utf32);
    constexpr Value value_make_enumeration_with(int64_t);
    constexpr Value value_make_error();
    Value value_make_error_with(const char*);
    constexpr Value value_make_false();
    Value value_make_integer_with(int64_t z);
    constexpr Value value_make_null();
    constexpr Value value_make_empty();
    Value value_make_string_with(const char* ntbs);
    Value value_make_string_with(string_view);
    Value value_make_array();
    Value value_make_table();
    constexpr Value value_make_true();
    Value value_make_zero();
    Value value_make_one();
    constexpr Value value_make_opcode(int);
    
    Value value_make_deep_copy(const Value&);
    
    constexpr bool value_is_boolean(const Value& self);
    constexpr bool value_is_character(const Value& self);
    constexpr bool value_is_error(const Value& self);
    constexpr bool value_is_null(const Value& self);
    constexpr bool value_is_enum(const Value& self);
    
    constexpr bool value_as_boolean(const Value& self);
    constexpr bool value_as_boolean_else(const Value& self, bool);
    constexpr int  value_as_character(const Value& self);
    constexpr int  value_as_character_else(const Value& self, int);
    constexpr std::pair<int, int> value_as_enum(const Value& self);
    constexpr int64_t value_as_int64_t(const Value& self);
    constexpr int64_t value_as_int64_t_else(const Value& self, int64_t);
    string_view value_as_string_view(const Value& self);
    string_view value_as_string_view_else(const Value& self, string_view);
    constexpr int value_as_opcode(const Value& self);
    
    bool value_contains(const Value& self, Value key);
    void value_resize(Value& self, Value count);
    Value value_find(const Value& self, Value key);
    Value value_insert_or_assign(Value& self, Value key, Value value);
    Value value_erase(Value& self, Value key);
    size_t value_size(const Value&);
    void value_push_back(const Value&, Value value);
    void value_pop_back(const Value&);
    Value value_back(const Value&);
    Value value_front(const Value&);
    
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
    struct Scan<Value> {
        
        Atomic<Value> _atomic_value;
        
        constexpr Scan() = default;
        Scan(const Scan& other);
        ~Scan() = default;
        Scan& operator=(const Scan& other);
        explicit Scan(const Value& other);
        Scan& operator=(const Value& other);
        explicit operator bool() const;
        operator Value() const;
        bool operator==(const Scan& other) const;
        auto operator<=>(const Scan& other) const;
        Value get() const;
    };
    
    /*
     size_t object_hash(const Scan<Value>&);
     void object_debug(const Scan<Value>&);
     void object_passivate(Scan<Value>&);
     void shade(const Scan<Value>&);
     void object_trace(const Scan<Value>&);
     void object_trace_weak(const Scan<Value>&);
     */
    
    template<>
    struct Scan<Atomic<Value>> {
        
        Atomic<Value> _atomic_value;
        
        constexpr Scan() = default;
        constexpr explicit Scan(Value);
        Scan(const Scan&) = delete;
        Scan& operator=(const Scan&) = delete;
        
        Value load(Ordering) const;
        void store(Value, Ordering);
        Value exchange(Value, Ordering);
        bool compare_exchange_weak(Value&, Value, Ordering, Ordering);
        bool compare_exchange_strong(Value&, Value, Ordering, Ordering);
        
    };
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    constexpr int _value_tag(const Value& self);
    constexpr bool _value_is_small_integer(const Value& self);
    constexpr bool _value_is_object(const Value& self);
    constexpr bool _value_is_short_string(const Value& self);
    constexpr bool _value_is_tombstone(const Value& self);
    
    GarbageCollected* _value_as_object(const Value& self);
    GarbageCollected* _value_as_garbage_collected_else(const Value& self, GarbageCollected*);
    constexpr int64_t _value_as_small_integer(const Value& self);
    constexpr int64_t _value_as_small_integer_else(const Value& self, int64_t);
    std::string_view _value_as_short_string(const Value& self);
    std::string_view _value_as_short_string_else(const Value& self);
    Value _value_make_with(const GarbageCollected* object);
    constexpr Value _value_make_tombstone();
    
    
    
    
    inline void shade(const Value& self) {
        if (_value_is_object(self))
            shade(_value_as_object(self));
    }
    
    inline void trace(const Value& self,void*p) {
        if (_value_is_object(self))
            trace(_value_as_object(self),p);
    }
    
    inline void shade(const Scan<Value>& self) {
        shade(self._atomic_value.load(Ordering::RELAXED));
    }
    
    inline void trace(const Scan<Value>& self,void*p) {
        trace(self._atomic_value.load(Ordering::ACQUIRE),p);
    }
    
    size_t hash(const Value& self);
    
    
    
    
    
    
    
    
    // TODO: upgrade to array of limbs of arbitrary precision integer
    struct HeapInt64 : GarbageCollected {
        std::int64_t _integer;
        explicit HeapInt64(std::int64_t z);
        virtual ~HeapInt64() final = default;
        std::int64_t as_int64_t() const;
        virtual void _garbage_collected_shade() const override;
        virtual void _garbage_collected_enumerate_fields(TraceContext*) const override;
    };
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    void foo();
    
    
    
    
    
    
    
    
    
    
    inline void trace(const Scan<Atomic<Value>>& self,void*p) {
        trace(self._atomic_value.load(Ordering::ACQUIRE),p);
    }
    
    inline void passivate(Value& self) {
        self._data = 0;
    }
    
    inline void passivate(Scan<Value>& self) {
        self._atomic_value.exchange(value_make_null(), Ordering::RELAXED);
    }
    
    inline void passivate(Scan<Atomic<Value>>& self) {
        // TODO: Is it ever right to call this?
        __builtin_trap();
        self.store(value_make_null(), Ordering::ACQUIRE);
    }
    
    // user defined literals
    
    // String operator""_v(const char* s, std::size_t n);
    
    
    
    
    
    
    
    
    
    
    enum _value_tag_e {
        VALUE_TAG_OBJECT,
        VALUE_TAG_BOOLEAN,
        VALUE_TAG_CHARACTER,
        VALUE_TAG_ENUMERATION,
        VALUE_TAG_ERROR,
        VALUE_TAG_SHORT_STRING,
        VALUE_TAG_SMALL_INTEGER,
        VALUE_TAG_OPCODE,
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
        VALUE_DATA_TRUE = VALUE_TAG_BOOLEAN | (1 << VALUE_SHIFT),
        VALUE_DATA_ERROR = VALUE_TAG_ERROR,
        VALUE_DATA_TOMBSTONE = VALUE_TAG_SPECIAL,
        VALUE_DATA_OK = VALUE_TAG_SPECIAL | (1 << VALUE_SHIFT),
        VALUE_DATA_NOTFOUND = VALUE_TAG_SPECIAL | (2 << VALUE_SHIFT),
        VALUE_DATA_RESTART = VALUE_TAG_SPECIAL | (3 << VALUE_SHIFT),
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
    
    
    constexpr int value_as_opcode(const Value& self) {
        if (_value_tag(self) != VALUE_TAG_OPCODE)
            abort();
        return (int)((int64_t)(self._data) >> VALUE_SHIFT);
    }
    
    constexpr int64_t Value::as_int64_t() const {
        return (int64_t)_data >> VALUE_SHIFT;
    }
    
    constexpr  int Value::as_opcode() const {
        return (int)as_int64_t();
    }
    
    constexpr  bool Value::is_opcode() const {
        return _value_tag(*this) == VALUE_TAG_OPCODE;
    }
    
    constexpr  bool Value::is_int64_t() const {
        return _value_tag(*this) == VALUE_TAG_SMALL_INTEGER;
    }
    
    constexpr  bool Value::is_Empty() const {
        return !_data;
    }
    
    
    
    constexpr bool value_is_enum(const Value& self) { return _value_tag(self) == VALUE_TAG_ENUMERATION; }
    constexpr bool value_is_null(const Value& self) { return !self._data; }
    constexpr bool value_is_error(const Value& self) { return _value_tag(self) == VALUE_TAG_ERROR; }
    constexpr bool value_is_boolean(const Value& self) { return _value_tag(self) == VALUE_TAG_BOOLEAN; }
    constexpr bool value_is_character(const Value& self) { return _value_tag(self) == VALUE_TAG_CHARACTER; }
    
    
    constexpr bool value_as_boolean(const Value& self) {
        assert(value_is_boolean(self));
        return self._data >> VALUE_SHIFT;
    }
    
    constexpr int value_as_character(const Value& self) {
        assert(value_is_character(self));
        return (int)((int64_t)self._data >> VALUE_SHIFT);
    }
    
    constexpr Value value_make_boolean_with(bool flag) {
        Value result;
        result._data = ((uint64_t)flag << VALUE_SHIFT) | VALUE_TAG_BOOLEAN;
        assert(value_is_boolean(result));
        return result;
    }
    
    constexpr Value::operator bool() const {
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
    
    constexpr Value value_make_error() { Value result; result._data = VALUE_TAG_ERROR; return result; }
    constexpr Value value_make_null() { Value result; result._data = 0; return result; }
    constexpr Value value_make_empty() { Value result; result._data = 0; return result; }
    constexpr Value _value_make_tombstone() { Value result; result._data = VALUE_DATA_TOMBSTONE; return result; }
    
    
    
    constexpr int _value_tag(const Value& self) { return self._data & VALUE_MASK; }
    constexpr bool _value_is_small_integer(const Value& self) { return _value_tag(self) == VALUE_TAG_SMALL_INTEGER; }
    constexpr bool _value_is_object(const Value& self) { return _value_tag(self) == VALUE_TAG_OBJECT; }
    constexpr bool _value_is_short_string(const Value& self) { return _value_tag(self) == VALUE_TAG_SHORT_STRING; }
    constexpr bool _vaue_is_tombstone(const Value& self) { return _value_tag(self) == VALUE_DATA_TOMBSTONE; }
    
    constexpr bool value_is_RESTART(const Value& self) {
        return self._data == VALUE_DATA_RESTART;
    }
    
    constexpr Value value_make_RESTART() {
        Value result;
        result._data = VALUE_DATA_RESTART;
        return result;
    }
    
    constexpr bool value_is_OK(const Value& self) {
        return self._data == VALUE_DATA_OK;
    }
    
    constexpr Value value_make_OK() {
        Value result;
        result._data = VALUE_DATA_OK;
        return result;
    }
    
    constexpr bool value_is_NOTFOUND(const Value& self) {
        return self._data == VALUE_DATA_NOTFOUND;
    }
    
    constexpr Value value_make_NOTFOUND() {
        Value result;
        result._data = VALUE_DATA_NOTFOUND;
        return result;
    }
    
    inline GarbageCollected* _value_as_object(const Value& self) {
        assert(_value_is_object(self));
        return (GarbageCollected*)self._data;
    }
    
    inline const GarbageCollected* _as_pointer_or_nullptr(const Value& self) {
        return _value_is_object(self) ? _value_as_object(self) : nullptr;
    }
    
    constexpr int64_t _value_as_small_integer(const Value& self) {
        assert(_value_is_small_integer(self));
        return (int64_t)self._data >> VALUE_SHIFT;
    }
    
    constexpr bool _value_is_tombstone(const Value& self) {
        return self._data == VALUE_DATA_TOMBSTONE;
    }
    
    constexpr Value value_make_enum(int meta, int code) {
        Value result;
        result._data = (VALUE_TAG_ENUMERATION
                        | ((uint32_t)meta << VALUE_SHIFT)
                        | ((int64_t)code << 32));
        return result;
    }
    
    constexpr std::pair<int, int> value_as_enum(const Value& self) {
        assert(value_is_enum(self));
        int code = (int)(self._data >> 32);
        int meta = ((int)self._data) >> VALUE_SHIFT;
        return {meta, code};
    }
    
    
    
    
    
    
    
} // namespace wry

#endif /* value_hpp */
