//
//  value.cpp
//  client
//
//  Created by Antony Searle on 31/5/2024.
//

#include <cinttypes>
#include <cstdlib>

#include <algorithm>
#include <numeric>
#include <random>

#include "hash.hpp"
#include "utility.hpp"
#include "value.hpp"
#include "HeapString.hpp"
#include "debug.hpp"


namespace wry {
    
    std::string_view _value_as_short_string(const Value& self) {
        assert(_value_is_short_string(self));
        return ((const _short_string_t&)self._data).as_string_view();
    }
    
    // Arithmetic / bitwise binary operators on Value.
    //
    // Two macro arguments: NAME is the lowercase suffix of the
    // HeapValue::_value_<NAME> virtual to dispatch to when the left
    // operand is an OBJECT-tagged HeapValue; OP is the C++ operator
    // symbol used in the integer fast-path.
    //
    // Each operator:
    //   - returns ERROR immediately if either argument is ERROR
    //     (VALUE_PROPAGATE_ERROR contract);
    //   - takes the inline-integer fast-path when left is SMALL_INTEGER
    //     (recurses via the int64_t overload);
    //   - dispatches to the polymorphic _value_<NAME> virtual when left
    //     is an OBJECT-tagged HeapValue;
    //   - returns ERROR otherwise.
#define X(NAME, OP) \
    Value operator OP (int64_t left, const Value& right) { \
        if (value_is_error(right)) return value_make_error(); \
        switch (_value_tag(right)) { \
            case VALUE_TAG_SMALL_INTEGER: \
                return value_make_integer_with(left OP _value_as_small_integer(right)); \
            default: \
                return value_make_error(); \
        } \
    } \
    \
    Value operator OP (const Value& left, const Value& right) { \
        VALUE_PROPAGATE_ERROR(left, right); \
        switch (_value_tag(left)) { \
            case VALUE_TAG_SMALL_INTEGER: \
                return _value_as_small_integer(left) OP right; \
            case VALUE_TAG_OBJECT: { \
                HeapValue* p = _value_as_object(left); \
                return p ? p->_value_##NAME(right) : value_make_error(); \
            } \
            default: \
                return value_make_error(); \
        } \
    }

    X(mul,    *)
    X(div,    /)
    X(mod,    %)
    X(sub,    -)
    X(add,    +)
    X(band,   &)
    X(bxor,   ^)
    X(bor,    |)
    X(lshift, <<)
    X(rshift, >>)

#undef X

    Value value_make_string_with(const char* ntbs) {
        Value result;
        std::size_t n = std::strlen(ntbs);
        if (n < 8) {
            _short_string_t s = {};
            s._tag_and_len = (n << 4) | VALUE_TAG_SHORT_STRING;
            __builtin_memcpy(s._chars, ntbs, n);
            __builtin_memcpy(&result, &s, 8);
            assert(_value_is_short_string(result));
        } else {
            result._data = (uint64_t)HeapString::make(std::string_view(ntbs, n));
            assert(_value_is_object(result));
        }
        return result;
    }
    
    // _value_make_garbage_collected_with was an orphan duplicate of
    // _value_make_with (declared in value.hpp).  Removed.
    
        
    
    
    
    
    
    
    
    
    
    
    
  
    
      
#if 0
    // Aspirational smoke test for the persistent associative-container
    // surface (value_make_table / value_insert_or_assign / value_find /
    // value_erase / value_size / value_contains, plus the operator
    // overloads).  Disabled because:
    //   - HeapArray / HeapTable are attic'd; value_make_table()
    //     currently returns a null Value, so most assertions don't hold.
    //   - The tests use operator== which has been removed in favor of
    //     value_eq(a, b) returning Value-of-bool.
    // Restore as a real define_test once the persistent associative DS
    // and value_eq land.
    void foo() {

        Value t = value_make_table();
        
        if (!(::rand() % 100)) {
            Value("This weak-cached string should be occasionally collected");
        }
        
        assert(value_size(t) == 0);
        assert(!value_contains(t, "a"));
        assert(value_find(t, "a") == value_make_null());
        value_insert_or_assign(t, "a", "A");
        assert(value_size(t) == 1);
        assert(value_contains(t, "a"));
        assert(value_find(t, "a") == "A");
        assert(value_insert_or_assign(t, "a", "A2") == "A");
        assert(value_size(t) == 1);
        assert(value_contains(t, "a"));
        assert(value_find(t, "a") == "A2");
        value_erase(t, "a");
        assert(value_size(t) == 0);
        assert(!value_contains(t, "a"));
        assert(value_find(t, "a") == value_make_null());
        
        {
            Value k = "very long key";
            Value v = "very long value";
            Value k2 = "another very long key";
            assert(!value_contains(t, k));
            assert(value_find(t, k) == value_make_null());
            assert(value_insert_or_assign(t, k, v) == value_make_null());
            assert(value_contains(t, k));
            assert(value_find(t, k) == v);
            assert(value_erase(t, k));
        }
        
        {
            Value a;
            a = 1;
            assert(a == 1);
            Value b;
            b = 2;
            assert(b == 2);
            Value c;
            c = a + b;
            assert(c == 3);
            /*
            value_debug(a);
            value_debug(b);
            value_debug(c);
             */
        }
        
        
        std::vector<int> v(100);
        std::iota(v.begin(), v.end(), 0);
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(v.begin(), v.end(), g);
        
        for (int i = 0; i != 100; ++i) {
            assert(value_size(t) == i);
            assert(!value_contains(t, v[i]));
            assert(value_is_null(value_find(t, v[i])));
            // assert(hash(v[i]) == hash(Value(v[i])));
            assert(value_is_null(value_insert_or_assign(t, v[i], v[i])));
            assert(value_size(t) == i + 1);
            assert(value_contains(t, v[i]));
            assert(value_find(t, v[i]) == v[i]);
        }
        
        std::shuffle(v.begin(), v.end(), g);
        for (int i = 0; i != 100; ++i) {
            assert(value_contains(t, v[i]));
            assert(value_find(t, v[i]) == v[i]);
            assert(!value_contains(t, v[i] + 100));
            assert(value_is_null(value_find(t, v[i] + 100)));
        }

        for (int i = 0; i != 100; ++i) {
            assert(value_contains(t, v[i]));
            assert(value_find(t, v[i]) == v[i]);
            assert(value_erase(t, v[i])== v[i]);
            assert(value_contains(t, v[i]) == false);
            assert(value_is_null(value_find(t, v[i])));
        }
        
        assert(value_size(t) == 0);

        std::shuffle(v.begin(), v.end(), g);
        
        Value s = t;
        for (int i = 0; i != 100; ++i) {
            assert(value_size(s) == i);
            assert(!value_contains(s, v[i]));
            assert(s[v[i]] == value_make_null());
            // t._pointer->_invariant();
            s[v[i]] = v[i];
            // t._pointer->_invariant();
            assert(value_size(t) == i + 1);
            assert(value_contains(t, v[i]));
            assert(s[v[i]] == v[i]);
        }

    }
#endif // 0

    bool contains(const HeapValue* self, Value key) {
        return self ? self->_value_contains(key) : false;
    }

    Value find(const HeapValue* self, Value key) {
        return self ? self->_value_find(key) : value_make_error();
    }

    Value insert_or_assign(HeapValue* self, Value key, Value value) {
        return self ? self->_value_insert_or_assign(key, value) : value_make_error();
    }

    Value erase(HeapValue* self, Value key) {
        return self ? self->_value_erase(key) : value_make_error();
    }

    size_t size(const HeapValue* self) {
        return self ? self->_value_size() : 0;
    }
    
    size_t value_size(Value self) {
        switch (_value_tag(self)) {
            case VALUE_TAG_OBJECT:
                return self._data ? size(_value_as_object(self)) : 0;
            case VALUE_TAG_SHORT_STRING:
                return (self._data >> 4) & 7;
            default:
                return 0;
        }
    }
    
    bool value_contains(Value self, Value key) {
        switch (_value_tag(self)) {
            case VALUE_TAG_OBJECT:
                return self._data && contains(_value_as_object(self), key);
            default:
                return false;
        }
    }
    
    
    Value Value::operator[](Value key) const {
        abort();
    }
    
    
    void object_shade(const Value& value) {
        if (_value_is_object(value)) {
            object_shade(_value_as_object(value));
        }
    }


    HeapInt64::HeapInt64(std::int64_t z)
    : _integer(z) {
    }

    std::int64_t HeapInt64::as_int64_t() const {
        return _integer;
    }
        
    HeapString::HeapString() {
    }
    
    Value value_insert_or_assign(Value self, Value key, Value value) {
        return insert_or_assign(_value_as_object(self), key, value);
    }

    
    Value value_find(Value self, Value key) {
        return find(_value_as_object(self), key);
    }
    
    Value value_erase(Value self, Value key) {
        return erase(_value_as_object(self), key);
    }

        
    void debug(Value self) {
        switch (_value_tag(self)) {
            case VALUE_TAG_OBJECT:
                if (!self._data)
                    return (void)printf("null\n");
                return debug(_value_as_object(self));
            case VALUE_TAG_ENUMERATION: {
                auto [meta, code] = value_as_enum(self);
                switch (meta) {
                    case VALUE_ENUM_META_BOOLEAN:
                        return (void)printf("%s\n", code ? "TRUE" : "FALSE");
                    case VALUE_ENUM_META_CHARACTER:
                        return (void)printf("\'%lc\'\n", code);
                    case VALUE_ENUM_META_OPCODE:
                        return (void)printf("opcode{%d}\n", code);
                    case VALUE_ENUM_META_SENTINEL:
                        switch (code) {
                            case VALUE_SENTINEL_TOMBSTONE: return (void)printf("TOMBSTONE\n");
                            case VALUE_SENTINEL_OK:        return (void)printf("OK\n");
                            case VALUE_SENTINEL_NOTFOUND:  return (void)printf("NOTFOUND\n");
                            case VALUE_SENTINEL_RESTART:   return (void)printf("RESTART\n");
                            default:                       return (void)printf("SENTINEL{%d}\n", code);
                        }
                    default:
                        return (void)printf("enum{meta=%d, code=%d}\n", meta, code);
                }
            }
            case VALUE_TAG_ERROR:
                return (void)printf("ERROR\n");
            case VALUE_TAG_SHORT_STRING: {
                auto v = _value_as_short_string(self);
                return (void)printf("\"%.*s\"\n", (int)v.size(), v.data());
            }
            case VALUE_TAG_SMALL_INTEGER:
                return (void)printf("%lld\n", (long long)_value_as_small_integer(self));
            case VALUE_TAG_ENTITY_ID:
                return (void)printf("entity_id{%llu}\n",
                                    (unsigned long long)value_as_entity_id(self).data);
            default:
                return (void)printf("Value{%#0." PRIx64 "}\n", self._data);
        }
    }
    
    Value _value_make_with(const GarbageCollected* p) {
        Value result;
        result._data = (uint64_t)p;
        return result;
    }

    // DEPRECATED.  See declaration in value.hpp.  Returns null so that
    // accidental use (e.g. through io/json.hpp's currently-dormant
    // parse_json_object) produces an obvious zero/error cascade rather
    // than a broken table object.
    Value value_make_table() {
        return Value{};
    }

    bool HeapValue::_value_empty() const { abort(); }
    size_t HeapValue::_value_size() const { return 0; }
    bool HeapValue::_value_contains(Value key) const { return false; }
    Value HeapValue::_value_find(Value key) const { return value_make_error(); }
    Value HeapValue::_value_insert_or_assign(Value key, Value value) const { return value_make_error(); }
    Value HeapValue::_value_erase(Value key) const { return value_make_error(); }

    // _value_eq: default is identity.  value_eq's bitwise short-circuit
    // already returned true for same-pointer; any call to this default
    // has different pointers, and identity-only subclasses (Entity,
    // World, ...) correctly answer false.  Content-comparable
    // subclasses override.
    Value HeapValue::_value_eq(Value /*right*/) const { return value_make_false(); }

    // _value_less and _value_hash are partial; defaults are ERROR.
    Value HeapValue::_value_less(Value /*right*/) const { return value_make_error(); }
    Value HeapValue::_value_hash() const { return value_make_error(); }

    Value HeapValue::_value_add(Value right) const { return value_make_error(); }
    Value HeapValue::_value_sub(Value right) const { return value_make_error(); }
    Value HeapValue::_value_mul(Value right) const { return value_make_error(); }
    Value HeapValue::_value_div(Value right) const { return value_make_error(); }
    Value HeapValue::_value_mod(Value right) const { return value_make_error(); }
    Value HeapValue::_value_rshift(Value right) const { return value_make_error(); }
    Value HeapValue::_value_lshift(Value right) const { return value_make_error(); }

    // ====================================================================
    // value_eq / value_less / value_hash free-function dispatch.
    //
    // See the predicate-vs-partial-op contract in the invariants block at
    // the top of value.hpp.
    // ====================================================================

    // Mix function for inline-tag Value hashing.  Variant of the
    // splitmix64 finalizer; good distribution from a 64-bit input.
    static constexpr uint64_t _value_inline_mix(uint64_t x) {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return x;
    }

    Value value_eq(Value a, Value b) {
        // Bitwise short-circuit.  Catches identical inline values,
        // identical pointers, identical SHORT_STRING encodings,
        // (ERROR, ERROR), (null, null), (false, false), etc.
        if (a._data == b._data) return value_make_true();

        int tag_a = _value_tag(a);
        int tag_b = _value_tag(b);

        if (tag_a != tag_b) {
            // ERROR is a type that's only equal to itself.  Comparing
            // ERROR with anything else is a clean false, not an ERROR
            // result (the test "if (x == ERROR) ..." must be useful).
            if (tag_a == VALUE_TAG_ERROR || tag_b == VALUE_TAG_ERROR)
                return value_make_false();
            // Other cross-tag comparisons are genuinely incomparable.
            return value_make_error();
        }

        // Same outer tag, different bits.
        switch (tag_a) {
            case VALUE_TAG_OBJECT: {
                // Both OBJECT, both non-null (short-circuit would have
                // fired for null == null), pointers differ.  Dispatch
                // to deep eq.  The base default returns false (identity
                // only); content-comparable subclasses override.
                HeapValue* p = _value_as_object(a);
                return p->_value_eq(b);
            }
            case VALUE_TAG_ENUMERATION: {
                // Same outer tag but the meta could differ.  If meta
                // matches, the values just have different codes -> false.
                // If meta differs, this is a cross-type compare (e.g.
                // boolean vs character) -> ERROR.
                uint32_t low_a = (uint32_t)a._data;
                uint32_t low_b = (uint32_t)b._data;
                if (low_a != low_b)
                    return value_make_error();
                return value_make_false();
            }
            default:
                // SHORT_STRING / SMALL_INTEGER / ENTITY_ID / reserved
                // tags: one canonical bit pattern per value, so
                // different bits == different value.
                return value_make_false();
        }
    }

    Value value_less(Value a, Value b) {
        VALUE_PROPAGATE_ERROR(a, b);

        // Strict less-than: equal values return false.
        if (a._data == b._data) return value_make_false();

        int tag_a = _value_tag(a);
        int tag_b = _value_tag(b);

        if (tag_a != tag_b)
            // Cross-tag ordering is undefined.
            return value_make_error();

        switch (tag_a) {
            case VALUE_TAG_SMALL_INTEGER:
                return value_make_boolean_with(
                    _value_as_small_integer(a) < _value_as_small_integer(b));
            case VALUE_TAG_ENTITY_ID:
                return value_make_boolean_with(
                    value_as_entity_id(a).data < value_as_entity_id(b).data);
            case VALUE_TAG_OBJECT: {
                HeapValue* p = _value_as_object(a);
                return p ? p->_value_less(b) : value_make_error();
            }
            // SHORT_STRING, ENUMERATION, reserved: no canonical ordering
            // at the runtime level.  Containers that need an order should
            // use hash or a domain-specific comparator.
            default:
                return value_make_error();
        }
    }

    Value value_hash(Value v) {
        switch (_value_tag(v)) {
            case VALUE_TAG_OBJECT: {
                if (!v._data) {
                    // null OBJECT hashes to the same hash as
                    // VALUE_DATA_NULL (i.e. mix(0)).
                    return value_make_integer_with(
                        (int64_t)(_value_inline_mix(0) >> 4));
                }
                HeapValue* p = _value_as_object(v);
                return p->_value_hash();
            }
            // ERROR is intentionally not hashable: containers shouldn't
            // be keyed on errors.  Falls through to the default ERROR
            // return below.
            case VALUE_TAG_ERROR:
                return value_make_error();
            default: {
                // Inline tags: hash the _data word, narrow to 60 bits so
                // it fits in a SMALL_INTEGER without overflow boxing.
                uint64_t h = _value_inline_mix(v._data) >> 4;
                return value_make_integer_with((int64_t)h);
            }
        }
    }

//    void HeapInt64::_garbage_collected_shade() const {
//        abort();
//        //Color expected = Color::WHITE;
//        //(void) color.compare_exchange(expected, Color::BLACK);
//    }
    
    void HeapInt64::_garbage_collected_scan() const {
        // fprintf(stderr, "scanned a weak ");
        // _garbage_collected_debug();
        // abort();
    }

    Value HeapInt64::_value_eq(Value right) const {
        // value_eq has already confirmed right is OBJECT-tagged with a
        // different pointer than this.  Cross-subtype is ERROR; same
        // subtype compares contents.
        HeapValue* p = _value_as_object(right);
        if (p && p->_save_type_tag() == HeapInt64::SAVE_TYPE_TAG) {
            const HeapInt64* o = static_cast<const HeapInt64*>(p);
            return value_make_boolean_with(_integer == o->_integer);
        }
        return value_make_error();
    }

    Value HeapInt64::_value_less(Value right) const {
        // Also accept comparison with an inline SMALL_INTEGER.
        if (_value_is_small_integer(right))
            return value_make_boolean_with(_integer < _value_as_small_integer(right));
        HeapValue* p = _value_as_object(right);
        if (p && p->_save_type_tag() == HeapInt64::SAVE_TYPE_TAG) {
            const HeapInt64* o = static_cast<const HeapInt64*>(p);
            return value_make_boolean_with(_integer < o->_integer);
        }
        return value_make_error();
    }

    Value HeapInt64::_value_hash() const {
        // Hash the int64 the same way value_hash hashes an inline
        // SMALL_INTEGER, so that the rare value_make_integer_with
        // overflow path doesn't change a value's hash relative to its
        // pre-overflow representation.  Inline path hashes the entire
        // _data word (tag included); we hash _data == (i << 4) | tag.
        uint64_t bits = ((uint64_t)_integer << VALUE_SHIFT) | VALUE_TAG_SMALL_INTEGER;
        uint64_t h = 0;
        {
            // Replicate _value_inline_mix's splitmix64 finalizer.
            uint64_t x = bits;
            x ^= x >> 30;
            x *= 0xbf58476d1ce4e5b9ULL;
            x ^= x >> 27;
            x *= 0x94d049bb133111ebULL;
            x ^= x >> 31;
            h = x;
        }
        return value_make_integer_with((int64_t)(h >> 4));
    }


} // namespace wry



