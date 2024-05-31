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
    
    // TODO: upgrade to array of limbs of arbitrary precision integer
    struct ObjectInt64 : Leaf<Object> {
        
        std::int64_t _integer;
        
        std::size_t gc_hash() const {
            return std::hash<std::int64_t>()(_integer);
        }
        
        std::size_t gc_bytes() const {
            return sizeof(ObjectInt64);
        }
        
        explicit ObjectInt64(std::int64_t z)
        : _integer(z) {
            printf("%p new %" PRId64 "\n", this, _integer);
        }
        
        std::int64_t as_int64_t() const {
            return _integer;
        }
        
        virtual ~ObjectInt64() {
            printf("%p del %" PRId64 "\n", this, _integer);
        }
        
    };
    
    struct ObjectString : Leaf<Object> {
        
        std::size_t _hash;
        std::size_t _size;
        char _bytes[0];

        static void* operator new(std::size_t count, std::size_t extra) {
            Mutator& m = Mutator::get();
            return m._allocate(count + extra);
        }
        
        static ObjectString* make(const char* first,
                                  const char* last,
                                  std::size_t hash) {
            std::ptrdiff_t n = last - first;
            ObjectString* p = new(n) ObjectString;
            p->_hash = std::hash<std::string_view>()(std::string_view(first, n));
            p->_size = n;
            std::memcpy(p->_bytes, first, n);
            printf("%p new \"%.*s\"\n", p, (int)p->_size, p->_bytes);
            return p;
        }
        
        std::size_t gc_bytes() const override final {
            return sizeof(ObjectString) + _size;
        }
        
        std::size_t gc_hash() const override final {
            return _hash;
        }

        std::string_view as_string_view() const {
            return std::string_view(_bytes, _size);
        }
        
        virtual ~ObjectString() {
            printf("%p del \"%.*s\"\n", this, (int)_size, _bytes);
        }
        
    }; // struct ObjectString
        
    
    
    
    
    struct alignas(8) Value {
        
        // these are storage types, not logical types;
        // e.g. a short string that is packed into a word is tag STRING,
        // but a long string that lives on the heap is tag OBJECT -> ObjectString
        
        enum {
            OBJECT = 0,
            INTEGER = 1,
            STRING = 3,
            BOOLEAN = 4,
            // boolean,?
            // enum as (type code, value code) ?
            // null?
            // error?
        };
        
        unsigned char _raw[8];
        
        int _tag() const {
            return _raw[0] & 0x0000000F;
        }
        
        int _size() const {
            return _raw[0] >> 4;
        }
        
        bool _is_object() const {
            return _tag() == OBJECT;
        }
        
        bool _is_integer() const {
            return _tag() == INTEGER;
        }
        
        bool _is_string() const {
            return _tag() == STRING;
        }
        
        bool _is_boolean() const {
            return _tag() == BOOLEAN;
        }
        
        const Object* _as_object() const {
            assert(_is_object());
            const Object* a;
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
            switch (_tag()) {
                case OBJECT: {
                    const Object* a = _as_object();
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
        
        static Value from_object(const Object* object) {
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
                ObjectInt64* a = new ObjectInt64(z);
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
                ObjectString* a = ObjectString::make(ntbs, ntbs + n, hash);
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
            switch (_value_that_is_a_number._tag()) {
                case Value::OBJECT: {
                    const Object* a = _value_that_is_a_number._as_object();
                    assert(dynamic_cast<const ObjectInt64*>(a));
                    const ObjectInt64* b = static_cast<const ObjectInt64*>(a);
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
            switch (_value_that_is_a_string._tag()) {
                case Value::OBJECT: {
                    const Object* a = _value_that_is_a_string._as_object();
                    assert(dynamic_cast<const ObjectString*>(a));
                    const ObjectString* b = static_cast<const ObjectString*>(a);
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
    
    template<typename F>
    auto visit(Value value, F&& f) {
        switch (value._tag()) {
            case Value::OBJECT: {
                const Object* a = value._as_object();
                const ObjectInt64* b = dynamic_cast<const ObjectInt64*>(a);
                if (b) {
                    Number c;
                    c._value_that_is_a_number = value;
                    return f(c);
                }
                const ObjectString* c = dynamic_cast<const ObjectString*>(a);
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


    }
    
    
} // namespace gc

#endif /* value_hpp */
