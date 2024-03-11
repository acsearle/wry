//
//  value.hpp
//  client
//
//  Created by Antony Searle on 19/1/2024.
//

#ifndef value_hpp
#define value_hpp

#include <cinttypes>

#include "debug.hpp"
#include "stdint.hpp"
#include "stdfloat.hpp"

#include "parse.hpp"
#include "string.hpp"
#include "table.hpp"


namespace wry::value {
    
    struct alignas(16) Value {

        enum : int64_t {
            
            EMPTY = 0,
            BOOLEAN = 1,
            INT64_T = 2,
            UINT64_T = 3,
            DOUBLE = 4,
            OPCODE = 5,
            STRING = 8,
            ARRAY = 9,
            DICTIONARY = 10,
            
        };
        
        int64_t d;
        int64_t x;
        
        Value();

        Value(int64_t discriminant, auto&& value);
        
        explicit Value(bool);
        explicit Value(int);
        explicit Value(int64_t);
        explicit Value(uint64_t);
        explicit Value(double);
        explicit Value(String&&);
        explicit Value(Array<Value>&&);
        explicit Value(HashMap<String, Value>&&);

        Value(const Value&);
        Value(Value&&);
        ~Value();
        
        Value& operator=(const Value&);
        Value& operator=(Value&&);
        Value& operator=(auto&&);
        
        bool is_empty() const;
        bool is_boolean() const;
        bool is_integer() const;
        bool is_opcode() const;

        bool as_bool() const;
        int64_t as_int64_t() const;
        int64_t as_opcode() const;

    };
    
    void swap(Value&, Value&);
    void swap(Value&, Value&&);
    void swap(Value&&, Value&);
    
    
    
        
    inline Value::Value() {
        d = EMPTY;
    }
    
    inline Value::Value(int64_t discriminant, auto&& value)
    : d(discriminant)
    , x(std::bit_cast<int64_t>(value)) {
    }
    
    inline Value::Value(const Value& other) {
        switch (other.d) {
            case EMPTY:
            case BOOLEAN:
            case INT64_T:
            case UINT64_T:
            case DOUBLE:
            case OPCODE:
                d = other.d;
                x = other.x;
                break;
            case STRING:
                d = EMPTY;
                x = std::bit_cast<int64_t>(new String(*std::bit_cast<String*>(other.x)));
                d = other.d;
                break;
            case ARRAY:
                d = EMPTY;
                x = std::bit_cast<int64_t>(new Array<Value>(*std::bit_cast<Array<Value>*>(other.x)));
                d = other.d;
                break;
            case DICTIONARY:
                d = EMPTY;
                x = std::bit_cast<int64_t>(new HashMap<String, Value>(*std::bit_cast<HashMap<String, Value>*>(other.x)));
                d = other.d;
                break;
            default:
                abort();
        }
    }
    
    inline Value::Value(Value&& other) {
        d = other.d;
        x = other.x;
        switch (other.d) {
            case EMPTY:
            case BOOLEAN:
            case INT64_T:
            case UINT64_T:
            case DOUBLE:
            case OPCODE:
                break;
            case STRING:
            case ARRAY:
            case DICTIONARY:
                other.d = EMPTY;
                break;
            default:
                abort();
        }
    }
    
    inline Value::~Value() {
        switch (d) {
            case EMPTY:
            case BOOLEAN:
            case INT64_T:
            case UINT64_T:
            case DOUBLE:
            case OPCODE:
                break;
            case STRING:
                delete std::bit_cast<String*>(x);
                break;
            case ARRAY:
                delete std::bit_cast<Array<Value>*>(x);
                break;
            case DICTIONARY:
                delete std::bit_cast<HashMap<String, Value>*>(x);
                break;
            default:
                abort();
        }
    }
    
    inline void swap(Value& left, Value& right) {
        using std::swap;
        swap(left.d, right.d);
        swap(left.x, right.x);
    }

    inline void swap(Value& left, Value&& right) {
        swap(left, right);
    }

    inline void swap(Value&& left, Value& right) {
        swap(left, right);
    }

    inline Value::Value(bool value) {
        d = BOOLEAN;
        x = value;
    }
    
    inline Value::Value(int value) {
        d = INT64_T;
        x = value;
    }
    
    inline Value::Value(int64_t value) {
        d = INT64_T;
        x = value;
    }

    inline Value::Value(uint64_t value) {
        d = UINT64_T;
        x = std::bit_cast<int64_t>(value);
    }

    inline Value::Value(double value) {
        d = DOUBLE;
        x = std::bit_cast<int64_t>(value);
    }

    inline Value::Value(String&& value) {
        d = EMPTY;
        x = std::bit_cast<int64_t>(new String(std::move(value)));
        d = STRING;
    }
    
    inline Value::Value(Array<Value>&& value) {
        d = EMPTY;
        x = std::bit_cast<int64_t>(new Array<Value>(std::move(value)));
        d = ARRAY;
    }

    inline Value::Value(HashMap<String, Value>&& value) {
        d = EMPTY;
        x = std::bit_cast<int64_t>(new HashMap<String, Value>(std::move(value)));
        d = DICTIONARY;
    }

    inline Value& Value::operator=(const Value& other) {
        swap(*this, Value(other));
        return *this;
    }
    
    inline Value& Value::operator=(Value&& other) {
        swap(*this, Value(std::move(other)));
        return *this;
    }
    
    inline Value& Value::operator=(auto&& value) {
        swap(*this, Value(std::forward<decltype(value)>(value)));
        return *this;
    }
    
    
    inline bool Value::is_empty() const {
        return !d;
    }
    
    inline bool Value::is_boolean() const {
        return d == BOOLEAN;
    }
    
    inline bool Value::is_integer() const {
        return d == INT64_T;
    }
    
    inline bool Value::is_opcode() const {
        return d == OPCODE;
    }

    inline bool Value::as_bool() const {
        assert(is_boolean());
        return x;
    }
    
    inline int64_t Value::as_int64_t() const {
        assert(is_integer());
        return x;
    }

    inline int64_t Value::as_opcode() const {
        assert(is_opcode());
        return x;
    }

    
} // namespace wry::value

#endif /* value_hpp */
