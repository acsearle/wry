//
//  value.hpp
//  client
//
//  Created by Antony Searle on 19/1/2024.
//

#ifndef value_hpp
#define value_hpp

#include <cinttypes>
#include <experimental/type_traits>
#include <concepts>

#include "debug.hpp"
#include "stdint.hpp"
#include "stdfloat.hpp"

#include "parse.hpp"
#include "string.hpp"
#include "table.hpp"

namespace wry::value {
    
    struct Value;
    
    using String = String;
    using Array = Array<Value>;
    using Dictionary = HashMap<String, Value>;
    
    struct Empty {
        
        constexpr explicit operator bool() const {
            return false;
        }
        
        constexpr bool operator!() const {
            return true;
        }
        
        constexpr std::strong_ordering operator<=>(const Empty&) const {
            return std::strong_ordering::equal;
        }

        constexpr bool operator==(const Empty&) const { 
            return true;
        }
        
    };
    
    struct Cow {
        // Value data;
        int64_t count;
    };
    
    struct Exception {
        
    };
    
    struct Trap {
        
    };


    
    // Value
            
    struct alignas(16) Value {
        
        enum : int64_t {
            
            EMPTY = 0,
            BOOL = 1,
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
        
        constexpr Value();

        constexpr Value(int64_t discriminant, auto&& value);
        
        constexpr explicit Value(Empty);
        constexpr explicit Value(std::nullptr_t);
        constexpr explicit Value(std::monostate);
        constexpr explicit Value(bool);
        constexpr explicit Value(int);
        constexpr explicit Value(int64_t);
        constexpr explicit Value(uint64_t);
        constexpr explicit Value(double);
        explicit Value(String&&);
        explicit Value(Array&&);
        explicit Value(Dictionary&&);

        constexpr Value(const Value&);
        constexpr Value(Value&&);
        constexpr ~Value();
        
        bool is_Empty() const;
        bool is_bool() const;
        bool is_int64_t() const;
        bool is_uint64_t() const;
        bool is_double() const;
        bool is_opcode() const;
        bool is_String() const;
        bool is_Array() const;
        bool is_Dictionary() const;
        
        Empty as_Empty() const;
        bool as_bool() const;
        int64_t as_int64_t() const;
        uint64_t as_uint64_t() const;
        double as_double() const;
        int64_t as_opcode() const;
        String& as_String() const;
        Array& as_Array() const;
        Dictionary& as_Dictionary() const;
        
        // member operators

        constexpr Value& operator=(const Value&);
        constexpr Value& operator=(Value&&);
        constexpr Value& operator=(auto&&);
                        
        explicit operator bool() const;
        explicit operator int() const;
        explicit operator int64_t() const;
        explicit operator uint64_t() const;
        explicit operator double() const;
        explicit operator const String&() const;
        explicit operator const Array&() const;
        explicit operator const Dictionary&() const;
        explicit operator String&();
        explicit operator Array&();
        explicit operator Dictionary&();
        
        Value& operator[](const Value&);

        // queries

        // "does the value hold a variable of exactly this type?"

        // "can the value, independent of its actual representation, be
        // accurately represented as a member of this mathematical set?"
        //
        // some representation types are strict subsets of these entities,
        // others may or may not be depending on specific values

        bool is_boolean() const;             // bool
        
        bool is_positive_integer() const;
        bool is_nonpositive_integer() const;
        bool is_negative_integer() const;
        bool is_nonnegative_integer() const; // + all uint64_t
        bool is_integer() const;             // + all int64_t
        bool is_binary() const;              // + finite double
        bool is_decimal() const;             // + all JSON numbers
        bool is_real() const;

        
        
        bool is_zero() const;
        bool is_nonzero() const;
        bool is_true() const;
        bool is_false() const;
        
        bool is_convertible_to_bool() const;

    };
            
    inline constexpr Value::Value(int64_t discriminant, auto&& value)
    : d(discriminant)
    , x(std::bit_cast<int64_t>(value)) {
    }
    
    inline constexpr Value::Value(const Value& other) {
        switch (other.d) {
            case EMPTY:
                d = other.d;
                x = 0;
                break;
            case BOOL:
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
                x = std::bit_cast<int64_t>(new Array(*std::bit_cast<Array*>(other.x)));
                d = other.d;
                break;
            case DICTIONARY:
                d = EMPTY;
                x = std::bit_cast<int64_t>(new Dictionary(*std::bit_cast<Dictionary*>(other.x)));
                d = other.d;
                break;
            default:
                abort();
        }
    }
    
    inline constexpr Value::Value(Value&& other) {
        d = other.d;
        x = other.x;
        switch (other.d) {
            case EMPTY:
            case BOOL:
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
    
    inline constexpr Value::~Value() {
        switch (d) {
            case EMPTY:
            case BOOL:
            case INT64_T:
            case UINT64_T:
            case DOUBLE:
            case OPCODE:
                break;
            case STRING:
                delete std::bit_cast<String*>(x);
                break;
            case ARRAY:
                delete std::bit_cast<Array*>(x);
                break;
            case DICTIONARY:
                delete std::bit_cast<Dictionary*>(x);
                break;
            default:
                abort();
        }
    }
        
    inline constexpr Value::Value() {
        d = EMPTY;
        x = 0;
    }
    
    inline constexpr Value::Value(std::nullptr_t) {
        d = EMPTY;
        x = 0;
    }

    inline constexpr Value::Value(std::monostate) {
        d = EMPTY;
        x = 0;
    }

    inline constexpr Value::Value(bool value) {
        d = BOOL;
        x = value;
    }
    
    inline constexpr Value::Value(int value) {
        d = INT64_T;
        x = value;
    }
    
    inline constexpr Value::Value(int64_t value) {
        d = INT64_T;
        x = value;
    }

    inline constexpr Value::Value(uint64_t value) {
        d = UINT64_T;
        x = std::bit_cast<int64_t>(value);
    }

    inline constexpr Value::Value(double value) {
        d = DOUBLE;
        x = std::bit_cast<int64_t>(value);
    }

    inline Value::Value(String&& value) {
        d = EMPTY;
        x = std::bit_cast<int64_t>(new String(std::move(value)));
        d = STRING;
    }
    
    inline Value::Value(Array&& value) {
        d = EMPTY;
        x = std::bit_cast<int64_t>(new Array(std::move(value)));
        d = ARRAY;
    }

    inline Value::Value(Dictionary&& value) {
        d = EMPTY;
        x = std::bit_cast<int64_t>(new Dictionary(std::move(value)));
        d = DICTIONARY;
    }
    
    inline constexpr void swap(Value& left, Value& right) {
        using std::swap;
        swap(left.d, right.d);
        swap(left.x, right.x);
    }
    
    inline constexpr void swap(Value& left, Value&& right) {
        swap(left, right);
    }
    
    inline constexpr void swap(Value&& left, Value& right) {
        swap(left, right);
    }


    inline constexpr Value& Value::operator=(const Value& other) {
        swap(*this, Value(other));
        return *this;
    }
    
    inline constexpr Value& Value::operator=(Value&& other) {
        swap(*this, Value(std::move(other)));
        return *this;
    }
    
    inline constexpr Value& Value::operator=(auto&& value) {
        swap(*this, Value(std::forward<decltype(value)>(value)));
        return *this;
    }
    
    inline bool Value::is_Empty() const {
        return d == EMPTY;
    }
    
    inline bool Value::is_bool() const {
        return d == BOOL;
    }
    
    inline bool Value::is_int64_t() const {
        return d == INT64_T;
    }

    inline bool Value::is_uint64_t() const {
        return d == INT64_T;
    }

    inline bool Value::is_double() const {
        return d == DOUBLE;
    }

    inline bool Value::is_opcode() const {
        return d == OPCODE;
    }

    inline bool Value::is_String() const {
        return d == STRING;
    }

    inline bool Value::is_Array() const {
        return d == ARRAY;
    }

    inline bool Value::is_Dictionary() const {
        return d == DICTIONARY;
    }

    inline Empty Value::as_Empty() const {
        assert(is_Empty());
        return Empty();
    }

    inline bool Value::as_bool() const {
        assert(is_bool());
        return x;
    }
    
    inline int64_t Value::as_int64_t() const {
        assert(is_int64_t());
        return x;
    }

    inline uint64_t Value::as_uint64_t() const {
        assert(is_uint64_t());
        return std::bit_cast<uint64_t>(x);
    }

    inline double Value::as_double() const {
        assert(is_double());
        return std::bit_cast<double>(x);
    }

    inline int64_t Value::as_opcode() const {
        assert(is_opcode());
        return x;
    }

    inline String& Value::as_String() const {
        assert(is_String());
        return *std::bit_cast<String*>(x);
    }

    inline Array& Value::as_Array() const {
        assert(is_Array());
        return *std::bit_cast<Array*>(x);
    }

    inline Dictionary& Value::as_Dictionary() const {
        assert(is_Dictionary());
        return *std::bit_cast<Dictionary*>(x);
    }

    inline bool Value::is_integer() const {
        switch (d) {
            case EMPTY:
            case BOOL:
                return false;
            case INT64_T:
            case UINT64_T:
                return true;
            case DOUBLE: {
                double a = std::bit_cast<double>(x);
                return a == trunc(a);
            }
            case OPCODE:
            case STRING:
            case ARRAY:
            case DICTIONARY:
                return false;
            default:
                abort();
        }
    }
    
    // using the visitor pattern to get operators up and running is convenient
    // but we can probably do better by hand
    
    // unary visitor
    
    decltype(auto) visit(auto&& f, const Value& a) {
        switch (a.d) {
            case Value::EMPTY:
                return std::forward<decltype(f)>(f)(a.as_Empty());
            case Value::BOOL:
                return std::forward<decltype(f)>(f)(a.as_bool());
            case Value::INT64_T:
                return std::forward<decltype(f)>(f)(a.as_int64_t());
            case Value::UINT64_T:
                return std::forward<decltype(f)>(f)(a.as_uint64_t());
            case Value::DOUBLE:
                return std::forward<decltype(f)>(f)(a.as_double());
            case Value::OPCODE:
                return std::forward<decltype(f)>(f)(a.as_opcode());
            case Value::STRING:
                return std::forward<decltype(f)>(f)(a.as_String());
            case Value::ARRAY:
                return std::forward<decltype(f)>(f)(a.as_Array());
            case Value::DICTIONARY:
                return std::forward<decltype(f)>(f)(a.as_Dictionary());
            default:
                abort();
        }
    }
    
    decltype(auto) visit(auto&& f, Value& a) {
        switch (a.d) {
            case Value::EMPTY:
                return std::forward<decltype(f)>(f)(a.as_Empty());
            case Value::BOOL:
                return std::forward<decltype(f)>(f)(a.as_bool());
            case Value::INT64_T:
                return std::forward<decltype(f)>(f)(a.as_int64_t());
            case Value::UINT64_T:
                return std::forward<decltype(f)>(f)(a.as_uint64_t());
            case Value::DOUBLE:
                return std::forward<decltype(f)>(f)(a.as_double());
            case Value::OPCODE:
                return std::forward<decltype(f)>(f)(a.as_opcode());
            case Value::STRING:
                return std::forward<decltype(f)>(f)(a.as_String());
            case Value::ARRAY:
                return std::forward<decltype(f)>(f)(a.as_Array());
            case Value::DICTIONARY:
                return std::forward<decltype(f)>(f)(a.as_Dictionary());
            default:
                abort();
        }
    }
    
    // binary visitor
    
    decltype(auto) visit(auto&& f, const Value& a, const Value& b) {
        return visit([f = std::forward<decltype(f)>(f),
                      &b](auto&& a) mutable -> decltype(auto) {
            return visit([f = std::forward<decltype(f)>(f),
                          &a](auto&& b) mutable -> decltype(auto) {
                return std::forward<decltype(f)>(f)(std::forward<decltype(a)>(a),
                                                    std::forward<decltype(b)>(b));
            }, b);
        }, a);
    }

    decltype(auto) visit(auto&& f, Value& a, const Value& b) {
        return visit([f = std::forward<decltype(f)>(f),
                      &b](auto&& a) mutable -> decltype(auto) {
            return visit([f = std::forward<decltype(f)>(f),
                          &a](auto&& b) mutable -> decltype(auto) {
                return std::forward<decltype(f)>(f)(std::forward<decltype(a)>(a),
                                                    std::forward<decltype(b)>(b));
            }, b);
        }, a);
    }

    
    
        
    template<typename A>
    concept can_promote = requires(A a) {
        { +a };
    };

    inline Value operator+(const Value& a) {
        return visit([ ](auto&& c) -> Value {
            if constexpr (can_promote<decltype(c)>) {
                return Value(+std::forward<decltype(c)>(c));
            } else {
                throw Exception();
            }
        }, a);
    }
    
    
    template<typename A>
    concept can_negate = requires(A a) {
        { -a };
    };
    
    inline Value operator-(const Value& a) {
        return visit([ ](auto&& c) -> Value {
            if constexpr (can_negate<decltype(c)>) {
                return Value(-std::forward<decltype(c)>(c));
            } else {
                throw Exception();
            }
        }, a);
    }

    
    template<typename A>
    concept can_logical_negate = requires(A a) {
        {!a} -> std::same_as<bool>;
    };
    
    inline Value operator!(const Value& a) {
        return visit([ ](auto&& c) -> Value {
            if constexpr (can_logical_negate<decltype(c)>) {
                return Value(!std::forward<decltype(c)>(c));
            } else {
                throw Exception();
            }
        }, a);
    }
    
    
    template<typename A>
    concept can_bitwise_negate = requires(A a) {
        { ~a };
    };
    
    inline Value operator~(const Value& a) {
        return visit([ ](auto&& c) -> Value {
            if constexpr (can_bitwise_negate<decltype(c)>) {
                return Value(~std::forward<decltype(c)>(c));
            } else {
                throw Exception();
            }
        }, a);
    }
    
    
    template<typename A, typename B>
    concept can_add = requires(A a, B b) {
        { a + b };
    };
    
    inline Value operator+(const Value& a, const Value& b) {
        return visit([ ](auto&& c, auto&& d) -> Value {
            if constexpr (can_add<decltype(c), decltype(d)>) {
                return Value(std::forward<decltype(c)>(c) + std::forward<decltype(d)>(d));
            } else {
                throw Exception();
            }
        }, a, b);
    }
    
    inline Value& operator+=(Value& a, const Value& b) {
        return a = a + b;
    }
    
    inline Value& operator++(Value& a) {
        return a += Value(1);
    }

    inline Value operator++(Value& a, int) {
        Value b(a);
        ++a;
        return b;
    }

    
    template<typename A, typename B>
    concept can_subtract = requires(A a, B b) {
        { a - b };
    };
    
    inline Value operator-(const Value& a, const Value& b) {
        return visit([ ](auto&& c, auto&& d) -> Value {
            if constexpr (can_subtract<decltype(c), decltype(d)>) {
                return Value(std::forward<decltype(c)>(c) - std::forward<decltype(d)>(d));
            } else {
                throw Exception();
            }
        }, a, b);
    }
    
    inline Value& operator-=(Value& a, const Value& b) {
        return a = a - b;
    }
    
    inline Value& operator--(Value& a) {
        return a -= Value(1);
    }
    
    inline Value operator--(Value& a, int) {
        Value b(a);
        --a;
        return b;
    }
    
    
    template<typename A, typename B>
    concept can_three_way_compare = requires(A a, B b) {
        { a <=> b } -> std::convertible_to<std::partial_ordering>;
    };
        
    inline std::partial_ordering operator<=>(const Value& a, const Value& b) {
        return visit([ ](const auto& c, const auto& d) -> std::partial_ordering {
            if constexpr (can_three_way_compare<decltype(c), decltype(d)>) {
                return c <=> d;
            } else {
                throw Exception();
            }
        }, a, b);
    }
    
    
    template<typename A, typename B>
    concept can_equality_compare = requires(A a, B b) {
        { a == b } -> std::same_as<bool>;
    };
    
    inline bool operator==(const Value& a, const Value& b) {
        return visit([ ](const auto& c, const auto& d) ->bool {
            if constexpr (can_equality_compare<decltype(c), decltype(d)>) {
                return c == d;
            } else {
                throw Exception();
            }
        }, a, b);
    }
    
    
    /*
    template<typename A, typename B>
    using subscript_t = decltype(std::declval<A>()[std::declval<B>()]);
    
    inline Value& Value::operator[](const Value& key) {
        return visit([ ](auto&& c, auto&& d) mutable -> Value& {
            if constexpr (std::experimental::is_detected_v<subscript_t, decltype(c), decltype(d)>) {
                return std::forward<decltype(c)>(c)[std::forward<decltype(d)>(d)];
            } else {
                abort();
            }
        }, *this, key);
    }
     */
    
    // subscript is a poor interface, consider
    //     find + insert_or_assign
    
    inline Value& Value::operator[](const Value& key) {
        switch (d) {
            case EMPTY:
            case BOOL:
            case INT64_T:
            case UINT64_T:
            case DOUBLE:
            case OPCODE:
                throw Exception();
            case ARRAY: {
                auto& r = as_Array();
                switch (key.d) {
                    case INT64_T:
                        return r[key.x];
                    case UINT64_T:
                        return r[key.as_uint64_t()];
                    default:
                        throw Exception();
                }
            }
            case DICTIONARY:
                switch (key.d) {
                    case STRING:
                        return as_Dictionary()[key.as_String()];
                    default:
                        throw Exception();
                }
            default:
                abort();
        }
    }

    
    
    
} // namespace wry::value

#endif /* value_hpp */
