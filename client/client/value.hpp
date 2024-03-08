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
    
    // a polymorphic value type
    //
    // two words in size, it uses the first word as a discriminant and the
    // second to store basic types inline, or to store a pointer to a heap
    // object that supplies virtual interfaces.  the intent is that a
    // switch on the discriminant handles common cases, with an indirection
    // to heavyweight objects
    
    struct Value;
    
    // the base class only supports lifetime operations
    
    struct Clone {
        virtual ~Clone() = default;
        virtual Clone* clone() const = 0;
    };
    
    template<typename T>
    struct Heap : Clone {
        
        T data;
        
        Heap(const Heap& other) = default;
        
        explicit Heap(std::in_place_t, auto&&... args)
        : data(std::forward<decltype(args)>(args)...) {
        }
        
        virtual ~Heap() final override = default;
        
        virtual Heap* clone() const final override {
            return new Heap(*this);
        }
        
    };
    
    template<typename T> Heap(T&&) -> Heap<std::decay_t<T>>;
    
    struct Value {
        
        enum discriminant_t {
            
            // representation is
            // - inline 64-bit signed integer, unsigned integer, or float
            // - inline pointer
            //   - heap type
            
            // we strive to allocate identifiers such that
            // we can accomplish most tasks with either
            //
            //     if (d & FLAG)
            //
            // or
            //
            //     switch(d & MASK)
            //
            
            // for example,
            // - move-construct: bitwise copy and (conditionally?) write
            //   back source.d = 0
            // - copy-construct: check flag, fall back to virtual clone
            // - destruct: check flag, fall back to virtual destruct
            
            // JSON requires us to maintain a logical type separate from
            // the physical representation
            // - JSON_NUMBER and JSON_STRING are both backed by HEAP_STRING
            // - JSON_NULL and JSON_BOOLEAN are both backed by INLINE_I64
            //
            // More generally, classifications like NUMBER can have multiple
            // representations
            // - NUMBER can be INLINE_I64, INLINE_U64, INLINE_F64 or
            //   HEAP_STRING
            
            // open optimization questions:
            // - make move-construct's `source.d = 0` conditional on
            //   `FLAG_IS_HEAP_NOT_INLINE`
            // - devirtualize the destructor calls?
            // - does `switch (d & MASK)` prefer `MASK` be low bits?
            // - premature optimization?  quantify vs
            //   - one pointer, everything on heap
            //   - two words, d becomes a vtbl pointer, all calls virtual
            //   - n words, Table metadata etc. inline
            //   - single allocation, with Table header followed directly by Entries
            //     - still two cache misses (except for unchecked array
            //       access?)
            
            
            DEFAULT = 0, // default is uninitialized int64_t
            
            FLAG_IS_UNSIGNED_NOT_SIGNED = 1 << 0,
            FLAG_IS_FLOATING_POINT_NOT_INTEGER = 1 << 1,
            FLAG_IS_HEAP_NOT_INLINE = 1 << 2,
            FLAG_IS_COLLECTION_NOT_STRING = 1 << 1,
            
            REPRESENTATION_TYPE_MASK = 7 << 0,
            
            INLINE_I64 = 0 << 0,
            INLINE_U64 = 1 << 0,
            INLINE_F64 = 2 << 0,
            // UNUSED  = 3 << 0,
            HEAP_STRING = 4 << 0,
            // UNUSED = 5 << 0,
            HEAP_ARRAY_OF_VALUE = 6 << 0,
            HEAP_TABLE_OF_STRING_TO_VALUE = 7 << 0,
            
            FLAG_IS_BOOLEAN = 1 << 3,
            FLAG_IS_NUMBER = 1 << 4,
            
            JSON_NULL = DEFAULT,
            JSON_BOOLEAN = DEFAULT | FLAG_IS_BOOLEAN,
            JSON_NUMBER = HEAP_STRING | FLAG_IS_NUMBER,
            JSON_STRING = HEAP_STRING,
            JSON_ARRAY = HEAP_ARRAY_OF_VALUE,
            JSON_OBJECT = HEAP_TABLE_OF_STRING_TO_VALUE
            
        };
        
        discriminant_t d;
        
        bool is_heap() const {
            return d & FLAG_IS_HEAP_NOT_INLINE;
        }
        
        bool is_clone() const {
            return is_heap();
        }
        
        bool is_delete() const {
            return is_heap();
        }
        
        bool is_number() const {
            return !is_heap() || (d & FLAG_IS_NUMBER);
        }
        
        bool is_integer();
        
        union {
            int64_t i;
            uint64_t u;
            float64_t f;
            Clone* p;
            Heap<Array<Value>>* array_of_value;
            Heap<Table<String, Value>>* table_of_string_to_value;
            
        };
        
        void maybe_delete() const {
            if (is_delete())
                delete p;
        }
        
        [[nodiscard]] Clone* maybe_clone() const {
            return is_clone() ? p->clone() : p;
        }
        
        void clear() {
            maybe_delete();
            d = DEFAULT;
        }
        
        Value& swap(Value& other) {
            std::swap(d, other.d);
            std::swap(u, other.u);
            return other;
        }
        
        // Semantic operators
        
        Value()
        : d(DEFAULT) {
        }
        
        Value(const Value& other)
        : d(DEFAULT)
        , p(other.maybe_clone()) {
            d = other.d;
        }
        
        Value(Value&& other)
        : d(std::exchange(other.d, DEFAULT))
        , p(other.p) {
        }
        
        ~Value() {
            maybe_delete();
        }
        
        Value& operator=(const Value& other) {
            return Value(other).swap(*this);
        }
        
        Value& operator=(Value&& other) {
            return Value(std::move(other)).swap(*this);
        }
        
        // Custom constructors
        
        explicit Value(std::int8_t x)
        : d(INLINE_I64)
        , i(x) {
        }
        
        explicit Value(std::int16_t x)
        : d(INLINE_I64)
        , i(x) {
        }
        
        explicit Value(std::int32_t x)
        : d(INLINE_I64)
        , i(x) {
        }
        
        explicit Value(std::int64_t x)
        : d(INLINE_I64)
        , i(x) {
        }
        
        explicit Value(std::uint8_t x)
        : d(INLINE_I64)
        , i(x) {
        }
        
        explicit Value(std::uint16_t x)
        : d(INLINE_I64)
        , i(x) {
        }
        
        explicit Value(std::uint32_t x)
        : d(INLINE_I64)
        , i(x) {
        }
        
        explicit Value(std::uint64_t x)
        : d(INLINE_U64)
        , u(x) {
        }
        
        explicit Value(bool x)
        : d(INLINE_I64)
        , i(x) {
        }
        
        explicit Value(float32_t x)
        : d(INLINE_F64)
        , f(x) {
        }
        
        explicit Value(float64_t x)
        : d(INLINE_F64)
        , f(x) {
        }
                
        explicit Value(String&& x)
        : d(DEFAULT)
        , p(new Heap<String>(std::in_place, std::move(x))) {
            d = HEAP_STRING;
        }
        
        explicit Value(StringView v)
        : Value(String(v)) {
        }
        
        explicit Value(const char* ntbs)
        : Value(String(ntbs)) {
        }
        
        explicit Value(const void*) = delete;

        
        explicit Value(Array<Value>&& x)
        : d(DEFAULT)
        , p(new Heap<Array<Value>>(std::in_place, std::move(x))) {
            d = HEAP_ARRAY_OF_VALUE;
        }
        
        explicit Value(Table<String, Value>&& x)
        : d(DEFAULT)
        , p(new Heap<Table<String, Value>>(std::in_place, std::move(x))) {
            d = HEAP_TABLE_OF_STRING_TO_VALUE;
        }
        
        template<typename T>
        Value& operator=(T&& other) {
            return Value(std::forward<T>(other)).swap(*this);
        }
        
        template<typename T>
        T as_scalar() const {
            switch (d & REPRESENTATION_TYPE_MASK) {
                case INLINE_I64:
                    // TODO: gsl narrow cast?
                    return (T) i;
                case INLINE_U64:
                    return (T) u;
                case INLINE_F64:
                    return (T) f;
                case HEAP_STRING: {
                    StringView u = as_string();
                    T x;
                    if (!parse_number(x)(u))
                        abort();
                    return x;
                }
                default:
                    abort();
            }
        }
        
        int64_t as_int64_t() const {
            return as_scalar<int64_t>();
        }
        
        uint64_t as_uint64_t() const {
            return as_scalar<uint64_t>();
        }
        
        float64_t as_float64_t() const {
            return as_scalar<float64_t>();

        }
        
        String& as_string() const {
            assert((d & REPRESENTATION_TYPE_MASK) == HEAP_STRING);
            return ((Heap<String>*) p)->data;
        }
        
        Array<Value>& as_array() const {
            assert(d == HEAP_ARRAY_OF_VALUE);
            return ((Heap<Array<Value>>*) p)->data;
        }
        
        Table<String, Value>& as_table() const {
            assert(d & HEAP_TABLE_OF_STRING_TO_VALUE);
            return ((Heap<Table<String, Value>>*) p)->data;
        }
        
        Value& operator[](StringView v) {
            assert((d & REPRESENTATION_TYPE_MASK) == HEAP_TABLE_OF_STRING_TO_VALUE);
            return table_of_string_to_value->data[v];
        }
        
        Value& operator[](size_type pos) {
            return array_of_value->data[pos];
        }
        
        Value& operator[](const Value& x) {
            switch (d & REPRESENTATION_TYPE_MASK) {
                case HEAP_ARRAY_OF_VALUE:
                    return array_of_value->data[x.as_uint64_t()];
                case HEAP_TABLE_OF_STRING_TO_VALUE:
                    return table_of_string_to_value->data[x.as_string()];
                default:
                    abort();
            }
        }
        
    };
    
} // namespace wry::value


#endif /* value_hpp */
