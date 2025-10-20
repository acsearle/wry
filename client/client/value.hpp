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

namespace wry {
    
    // TODO: which string view?
    using std::string_view;
        
    struct Value {
        
        uint64_t _data = {};
                
        constexpr Value() = default;
        constexpr Value(const Value&) = default;
        constexpr Value(Value&) = default;
        constexpr Value(Value&&) = default;
        constexpr Value& operator=(const Value&) = default;
        
        // implicit construction from vocabulary types
        
        constexpr Value(std::nullptr_t);
        constexpr Value(bool);
        constexpr Value(int);
        constexpr Value(int64_t);
        
        constexpr Value(const char*);
        template<std::size_t N> requires (N > 0)
        constexpr Value(const char (&ntbs)[N]);
        Value(string_view);
        
        // TODO: call syntax
        // Value operator()(/* args type? */) const;
        
        Value operator[](Value) const;
        
        // implicit conversion
        constexpr explicit operator bool() const;
        
        constexpr bool is_opcode() const;
        constexpr int as_opcode() const;
        
        constexpr bool is_int64_t() const;
        constexpr int64_t as_int64_t() const;
        
        constexpr bool is_Empty() const;
        
    }; // struct Value
    
    void garbage_collected_shade(const Value& value);
    void garbage_collected_scan(const Value& value);

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
        
    constexpr bool value_is_boolean(Value);
    constexpr bool value_is_character(Value);
    constexpr bool value_is_error(Value);
    constexpr bool value_is_null(Value);
    constexpr bool value_is_enum(Value);
    
    constexpr bool value_as_boolean(Value);
    constexpr bool value_as_boolean_else(Value, bool);
    constexpr int  value_as_character(Value);
    constexpr int  value_as_character_else(Value, int);
    constexpr std::pair<int, int> value_as_enum(Value);
    constexpr int64_t value_as_int64_t(Value);
    constexpr int64_t value_as_int64_t_else(Value, int64_t);
    string_view value_as_string_view(Value);
    string_view value_as_string_view_else(Value, string_view);
    constexpr int value_as_opcode(Value);
    
    bool value_contains(Value, Value key);
    Value value_find(Value, Value key);
    Value value_insert_or_assign(Value self, Value key, Value value);
    Value value_erase(Value self, Value key);
    size_t value_size(Value);
    void value_push_back(Value, Value value);
    void value_pop_back(Value);
    Value value_back(Value);
    Value value_front(Value);
    
    // non-member operator overloads
    
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
        
    
        
    struct HeapValue : GarbageCollected {
                
        // TODO: is it useful to have a base class above the Value interface?
        virtual Value _value_insert_or_assign(Value key, Value value) const;
        virtual bool _value_empty() const;
        virtual size_t _value_size() const;
        virtual bool _value_contains(Value key) const;
        virtual Value _value_find(Value key) const;
        virtual Value _value_erase(Value key) const;
        virtual Value _value_add(Value right) const;
        virtual Value _value_sub(Value right) const;
        virtual Value _value_mul(Value right) const;
        virtual Value _value_div(Value right) const;
        virtual Value _value_mod(Value right) const;
        virtual Value _value_rshift(Value right) const;
        virtual Value _value_lshift(Value right) const;
        
    };
    
    
    
    
    
    
    constexpr int _value_tag(Value);
    constexpr bool _value_is_small_integer(Value);
    constexpr bool _value_is_object(Value);
    constexpr bool _value_is_short_string(Value);
    constexpr bool _value_is_tombstone(Value);
    
    HeapValue* _value_as_object(Value);
    HeapValue const* _value_as_heap_value_else(Value, HeapValue const*);
    GarbageCollected const* _value_as_garbage_collected_else(Value, GarbageCollected const*);
    constexpr int64_t _value_as_small_integer(Value);
    constexpr int64_t _value_as_small_integer_else(Value, int64_t);
    Value _value_make_with(const GarbageCollected* object);

    // lifetimebomb
    [[nodiscard]] std::string_view _value_as_short_string(Value const&);
    [[nodiscard]] std::string_view _value_as_short_string_else(Value const&, std::string_view);

    
    
    
    inline void garbage_collected_shade(Value const& self) {
        if (_value_is_object(self))
            garbage_collected_shade(_value_as_object(self));
    }
    
    inline void garbage_collected_scan(Value const& self) {
        if (_value_is_object(self))
            garbage_collected_scan(_value_as_object(self));
    }
        
    // size_t hash(const Value& self);
    
    
    
    
    
    
    
    
    // TODO: upgrade to array of limbs of arbitrary precision integer
    struct HeapInt64 : HeapValue {
        std::int64_t _integer;
        explicit HeapInt64(std::int64_t z);
        virtual ~HeapInt64() final = default;
        std::int64_t as_int64_t() const;
        virtual void _garbage_collected_scan() const override;
    };
    
    
    

    
    
    
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
        //std::size_t hash() const {
        //    return std::hash<std::string_view>()(as_string_view());
        //}
    };
    
    
    
    
    constexpr Value::Value(std::nullptr_t) : _data(VALUE_DATA_NULL) {}
    constexpr Value::Value(bool flag) : _data(((uint64_t)flag << VALUE_SHIFT) | VALUE_TAG_BOOLEAN) {}
    constexpr Value::Value(const char* ntbs) { *this = value_make_string_with(ntbs); }
    constexpr Value::Value(int i) : _data(((int64_t)i << VALUE_SHIFT) | VALUE_TAG_SMALL_INTEGER) {}

    
    constexpr Value value_make_opcode(int code) {
        Value result;
        result._data = ((int64_t)code << VALUE_SHIFT) | VALUE_TAG_OPCODE;
        return result;
    }
    
    // TODO: fixme
    constexpr Value::Value(int64_t x) : _data((x << VALUE_SHIFT) | VALUE_TAG_SMALL_INTEGER) {}
    
    
    constexpr int value_as_opcode(Value self) {
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
    
    
    
    constexpr bool value_is_enum(Value self) { return _value_tag(self) == VALUE_TAG_ENUMERATION; }
    constexpr bool value_is_null(Value self) { return !self._data; }
    constexpr bool value_is_error(Value self) { return _value_tag(self) == VALUE_TAG_ERROR; }
    constexpr bool value_is_boolean(Value self) { return _value_tag(self) == VALUE_TAG_BOOLEAN; }
    constexpr bool value_is_character(Value self) { return _value_tag(self) == VALUE_TAG_CHARACTER; }
    
    
    constexpr bool value_as_boolean(Value self) {
        assert(value_is_boolean(self));
        return self._data >> VALUE_SHIFT;
    }
    
    constexpr int value_as_character(Value self) {
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
    
    
    
    constexpr int _value_tag(Value self) { return self._data & VALUE_MASK; }
    constexpr bool _value_is_small_integer(Value self) { return _value_tag(self) == VALUE_TAG_SMALL_INTEGER; }
    constexpr bool _value_is_object(Value self) { return _value_tag(self) == VALUE_TAG_OBJECT; }
    constexpr bool _value_is_short_string(Value self) { return _value_tag(self) == VALUE_TAG_SHORT_STRING; }
    constexpr bool _vaue_is_tombstone(Value self) { return _value_tag(self) == VALUE_DATA_TOMBSTONE; }
    
    constexpr bool value_is_RESTART(Value self) {
        return self._data == VALUE_DATA_RESTART;
    }
    
    constexpr Value value_make_RESTART() {
        Value result;
        result._data = VALUE_DATA_RESTART;
        return result;
    }
    
    constexpr bool value_is_OK(Value self) {
        return self._data == VALUE_DATA_OK;
    }
    
    constexpr Value value_make_OK() {
        Value result;
        result._data = VALUE_DATA_OK;
        return result;
    }
    
    constexpr bool value_is_NOTFOUND(Value self) {
        return self._data == VALUE_DATA_NOTFOUND;
    }
    
    constexpr Value value_make_NOTFOUND() {
        Value result;
        result._data = VALUE_DATA_NOTFOUND;
        return result;
    }
    
    inline HeapValue* _value_as_object(Value self) {
        assert(_value_is_object(self));
        return (HeapValue*)self._data;
    }
    
    inline HeapValue* _as_pointer_or_nullptr(Value self) {
        return _value_is_object(self) ? _value_as_object(self) : nullptr;
    }
    
    constexpr int64_t _value_as_small_integer(Value self) {
        assert(_value_is_small_integer(self));
        return (int64_t)self._data >> VALUE_SHIFT;
    }
    
    constexpr bool _value_is_tombstone(Value self) {
        return self._data == VALUE_DATA_TOMBSTONE;
    }
    
    constexpr Value value_make_enum(int meta, int code) {
        Value result;
        result._data = (VALUE_TAG_ENUMERATION
                        | ((uint32_t)meta << VALUE_SHIFT)
                        | ((int64_t)code << 32));
        return result;
    }
    
    constexpr std::pair<int, int> value_as_enum(Value self) {
        assert(value_is_enum(self));
        int code = (int)(self._data >> 32);
        int meta = ((int)self._data) >> VALUE_SHIFT;
        return {meta, code};
    }
    
    
    
    
    
    
    
} // namespace wry

#endif /* value_hpp */
