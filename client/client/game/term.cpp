//
//  term.cpp
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
#include "term.hpp"
#include "matter.hpp"
#include "HeapString.hpp"
#include "debug.hpp"
#include "test.hpp"


namespace wry {
    
    std::string_view _term_as_short_string(const Term& self) {
        assert(_term_is_short_string(self));
        return ((const _short_string_t&)self._data).as_string_view();
    }
    
    // Arithmetic / bitwise binary operators on Term.
    //
    // Two macro arguments: NAME is the lowercase suffix of the
    // HeapTerm::_term_<NAME> virtual to dispatch to when the left
    // operand is an OBJECT-tagged HeapTerm; OP is the C++ operator
    // symbol used in the integer fast-path.
    //
    // Each operator:
    //   - returns ERROR immediately if either argument is ERROR
    //     (TERM_PROPAGATE_ERROR contract);
    //   - takes the inline-integer fast-path when left is SMALL_INTEGER
    //     (recurses via the int64_t overload);
    //   - dispatches to the polymorphic _term_<NAME> virtual when left
    //     is an OBJECT-tagged HeapTerm;
    //   - returns ERROR otherwise.
#define X(NAME, OP) \
    Term operator OP (int64_t left, const Term& right) { \
        if (term_is_error(right)) return term_make_error(); \
        switch (_term_tag(right)) { \
            case TERM_TAG_SMALL_INTEGER: \
                return term_make_integer_with(left OP _term_as_small_integer(right)); \
            default: \
                return term_make_error(); \
        } \
    } \
    \
    Term operator OP (const Term& left, const Term& right) { \
        TERM_PROPAGATE_ERROR(left, right); \
        switch (_term_tag(left)) { \
            case TERM_TAG_SMALL_INTEGER: \
                return _term_as_small_integer(left) OP right; \
            case TERM_TAG_OBJECT: { \
                HeapTerm* p = _term_as_object(left); \
                return p ? p->_term_##NAME(right) : term_make_error(); \
            } \
            default: \
                return term_make_error(); \
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

    Term term_make_string_with(const char* ntbs) {
        Term result;
        std::size_t n = std::strlen(ntbs);
        if (n < 8) {
            _short_string_t s = {};
            s._tag_and_len = (n << 4) | TERM_TAG_SHORT_STRING;
            __builtin_memcpy(s._chars, ntbs, n);
            __builtin_memcpy(&result, &s, 8);
            assert(_term_is_short_string(result));
        } else {
            result._data = (uint64_t)HeapString::make(std::string_view(ntbs, n));
            assert(_term_is_object(result));
        }
        return result;
    }
    
    // _term_make_garbage_collected_with was an orphan duplicate of
    // _term_make_with (declared in term.hpp).  Removed.
    
        
    
    
    
    
    
    
    
    
    
    
    
  
    
      
#if 0
    // Aspirational smoke test for the persistent associative-container
    // surface (term_make_table / term_insert_or_assign / term_find /
    // term_erase / term_size / term_contains, plus the operator
    // overloads).  Disabled because:
    //   - HeapArray / HeapTable are attic'd; term_make_table()
    //     currently returns a null Term, so most assertions don't hold.
    //   - The tests use operator== which has been removed in favor of
    //     term_eq(a, b) returning Term-of-bool.
    // Restore as a real define_test once the persistent associative DS
    // and term_eq land.
    void foo() {

        Term t = term_make_table();
        
        if (!(::rand() % 100)) {
            Term("This weak-cached string should be occasionally collected");
        }
        
        assert(term_size(t) == 0);
        assert(!term_contains(t, "a"));
        assert(term_find(t, "a") == term_make_null());
        term_insert_or_assign(t, "a", "A");
        assert(term_size(t) == 1);
        assert(term_contains(t, "a"));
        assert(term_find(t, "a") == "A");
        assert(term_insert_or_assign(t, "a", "A2") == "A");
        assert(term_size(t) == 1);
        assert(term_contains(t, "a"));
        assert(term_find(t, "a") == "A2");
        term_erase(t, "a");
        assert(term_size(t) == 0);
        assert(!term_contains(t, "a"));
        assert(term_find(t, "a") == term_make_null());
        
        {
            Term k = "very long key";
            Term v = "very long value";
            Term k2 = "another very long key";
            assert(!term_contains(t, k));
            assert(term_find(t, k) == term_make_null());
            assert(term_insert_or_assign(t, k, v) == term_make_null());
            assert(term_contains(t, k));
            assert(term_find(t, k) == v);
            assert(term_erase(t, k));
        }
        
        {
            Term a;
            a = 1;
            assert(a == 1);
            Term b;
            b = 2;
            assert(b == 2);
            Term c;
            c = a + b;
            assert(c == 3);
            /*
            term_debug(a);
            term_debug(b);
            term_debug(c);
             */
        }
        
        
        std::vector<int> v(100);
        std::iota(v.begin(), v.end(), 0);
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(v.begin(), v.end(), g);
        
        for (int i = 0; i != 100; ++i) {
            assert(term_size(t) == i);
            assert(!term_contains(t, v[i]));
            assert(term_is_null(term_find(t, v[i])));
            // assert(hash(v[i]) == hash(Term(v[i])));
            assert(term_is_null(term_insert_or_assign(t, v[i], v[i])));
            assert(term_size(t) == i + 1);
            assert(term_contains(t, v[i]));
            assert(term_find(t, v[i]) == v[i]);
        }
        
        std::shuffle(v.begin(), v.end(), g);
        for (int i = 0; i != 100; ++i) {
            assert(term_contains(t, v[i]));
            assert(term_find(t, v[i]) == v[i]);
            assert(!term_contains(t, v[i] + 100));
            assert(term_is_null(term_find(t, v[i] + 100)));
        }

        for (int i = 0; i != 100; ++i) {
            assert(term_contains(t, v[i]));
            assert(term_find(t, v[i]) == v[i]);
            assert(term_erase(t, v[i])== v[i]);
            assert(term_contains(t, v[i]) == false);
            assert(term_is_null(term_find(t, v[i])));
        }
        
        assert(term_size(t) == 0);

        std::shuffle(v.begin(), v.end(), g);
        
        Term s = t;
        for (int i = 0; i != 100; ++i) {
            assert(term_size(s) == i);
            assert(!term_contains(s, v[i]));
            assert(s[v[i]] == term_make_null());
            // t._pointer->_invariant();
            s[v[i]] = v[i];
            // t._pointer->_invariant();
            assert(term_size(t) == i + 1);
            assert(term_contains(t, v[i]));
            assert(s[v[i]] == v[i]);
        }

    }
#endif // 0

    bool contains(const HeapTerm* self, Term key) {
        return self ? self->_term_contains(key) : false;
    }

    Term find(const HeapTerm* self, Term key) {
        return self ? self->_term_find(key) : term_make_error();
    }

    Term insert_or_assign(HeapTerm* self, Term key, Term value) {
        return self ? self->_term_insert_or_assign(key, value) : term_make_error();
    }

    Term erase(HeapTerm* self, Term key) {
        return self ? self->_term_erase(key) : term_make_error();
    }

    size_t size(const HeapTerm* self) {
        return self ? self->_term_size() : 0;
    }
    
    size_t term_size(Term self) {
        switch (_term_tag(self)) {
            case TERM_TAG_OBJECT:
                return self._data ? size(_term_as_object(self)) : 0;
            case TERM_TAG_SHORT_STRING:
                return (self._data >> 4) & 7;
            default:
                return 0;
        }
    }
    
    bool term_contains(Term self, Term key) {
        switch (_term_tag(self)) {
            case TERM_TAG_OBJECT:
                return self._data && contains(_term_as_object(self), key);
            default:
                return false;
        }
    }
    
    
    Term Term::operator[](Term key) const {
        abort();
    }
    
    
    void object_shade(const Term& value) {
        if (_term_is_object(value)) {
            object_shade(_term_as_object(value));
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
    
    Term term_insert_or_assign(Term self, Term key, Term value) {
        return insert_or_assign(_term_as_object(self), key, value);
    }

    
    Term term_find(Term self, Term key) {
        return find(_term_as_object(self), key);
    }
    
    Term term_erase(Term self, Term key) {
        return erase(_term_as_object(self), key);
    }

        
    void debug(Term self) {
        switch (_term_tag(self)) {
            case TERM_TAG_OBJECT:
                if (!self._data)
                    return (void)printf("null\n");
                return debug(_term_as_object(self));
            case TERM_TAG_ENUMERATION: {
                auto [meta, code] = term_as_enum(self);
                switch (meta) {
                    case TERM_ENUM_META_BOOLEAN:
                        return (void)printf("%s\n", code ? "TRUE" : "FALSE");
                    case TERM_ENUM_META_CHARACTER:
                        return (void)printf("\'%lc\'\n", code);
                    case TERM_ENUM_META_OPCODE:
                        return (void)printf("opcode{%d}\n", code);
                    case TERM_ENUM_META_SENTINEL:
                        switch (code) {
                            case TERM_SENTINEL_TOMBSTONE: return (void)printf("TOMBSTONE\n");
                            case TERM_SENTINEL_OK:        return (void)printf("OK\n");
                            case TERM_SENTINEL_NOTFOUND:  return (void)printf("NOTFOUND\n");
                            case TERM_SENTINEL_RESTART:   return (void)printf("RESTART\n");
                            default:                       return (void)printf("SENTINEL{%d}\n", code);
                        }
                    default:
                        return (void)printf("enum{meta=%d, code=%d}\n", meta, code);
                }
            }
            case TERM_TAG_ERROR:
                return (void)printf("ERROR\n");
            case TERM_TAG_SHORT_STRING: {
                auto v = _term_as_short_string(self);
                return (void)printf("\"%.*s\"\n", (int)v.size(), v.data());
            }
            case TERM_TAG_SMALL_INTEGER:
                return (void)printf("%lld\n", (long long)_term_as_small_integer(self));
            case TERM_TAG_ENTITY_ID:
                return (void)printf("entity_id{%llu}\n",
                                    (unsigned long long)term_as_entity_id(self).data);
            default:
                return (void)printf("Term{%#0." PRIx64 "}\n", self._data);
        }
    }
    
    Term _term_make_with(const GarbageCollected* p) {
        Term result;
        result._data = (uint64_t)p;
        return result;
    }


    bool HeapTerm::_term_empty() const { abort(); }
    size_t HeapTerm::_term_size() const { return 0; }
    bool HeapTerm::_term_contains(Term key) const { return false; }
    Term HeapTerm::_term_find(Term key) const { return term_make_error(); }
    Term HeapTerm::_term_insert_or_assign(Term key, Term value) const { return term_make_error(); }
    Term HeapTerm::_term_erase(Term key) const { return term_make_error(); }

    // _term_eq: default is identity.  term_eq's bitwise short-circuit
    // already returned true for same-pointer; any call to this default
    // has different pointers, and identity-only subclasses (Entity,
    // World, ...) correctly answer false.  Content-comparable
    // subclasses override.
    Term HeapTerm::_term_eq(Term /*right*/) const { return term_make_false(); }

    // _term_less and _term_hash are partial; defaults are ERROR.
    Term HeapTerm::_term_less(Term /*right*/) const { return term_make_error(); }
    Term HeapTerm::_term_hash() const { return term_make_error(); }

    Term HeapTerm::_term_add(Term right) const { return term_make_error(); }
    Term HeapTerm::_term_sub(Term right) const { return term_make_error(); }
    Term HeapTerm::_term_mul(Term right) const { return term_make_error(); }
    Term HeapTerm::_term_div(Term right) const { return term_make_error(); }
    Term HeapTerm::_term_mod(Term right) const { return term_make_error(); }
    Term HeapTerm::_term_rshift(Term right) const { return term_make_error(); }
    Term HeapTerm::_term_lshift(Term right) const { return term_make_error(); }

    // ====================================================================
    // term_eq / term_less / term_hash free-function dispatch.
    //
    // See the predicate-vs-partial-op contract in the invariants block at
    // the top of term.hpp.
    // ====================================================================

    // Mix function for inline-tag Term hashing.  Variant of the
    // splitmix64 finalizer; good distribution from a 64-bit input.
    static constexpr uint64_t _term_inline_mix(uint64_t x) {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return x;
    }

    Term term_eq(Term a, Term b) {
        // Bitwise short-circuit.  Catches identical inline values,
        // identical pointers, identical SHORT_STRING encodings,
        // (ERROR, ERROR), (null, null), (false, false), etc.
        if (a._data == b._data) return term_make_true();

        int tag_a = _term_tag(a);
        int tag_b = _term_tag(b);

        if (tag_a != tag_b) {
            // ERROR is a type that's only equal to itself.  Comparing
            // ERROR with anything else is a clean false, not an ERROR
            // result (the test "if (x == ERROR) ..." must be useful).
            if (tag_a == TERM_TAG_ERROR || tag_b == TERM_TAG_ERROR)
                return term_make_false();
            // Other cross-tag comparisons are genuinely incomparable.
            return term_make_error();
        }

        // Same outer tag, different bits.
        switch (tag_a) {
            case TERM_TAG_OBJECT: {
                // Both OBJECT, both non-null (short-circuit would have
                // fired for null == null), pointers differ.  Dispatch
                // to deep eq.  The base default returns false (identity
                // only); content-comparable subclasses override.
                HeapTerm* p = _term_as_object(a);
                return p->_term_eq(b);
            }
            case TERM_TAG_ENUMERATION: {
                // Same outer tag but the meta could differ.  If meta
                // matches, the values just have different codes -> false.
                // If meta differs, this is a cross-type compare (e.g.
                // boolean vs character) -> ERROR.
                uint32_t low_a = (uint32_t)a._data;
                uint32_t low_b = (uint32_t)b._data;
                if (low_a != low_b)
                    return term_make_error();
                return term_make_false();
            }
            default:
                // SHORT_STRING / SMALL_INTEGER / ENTITY_ID / reserved
                // tags: one canonical bit pattern per value, so
                // different bits == different value.
                return term_make_false();
        }
    }

    Term term_less(Term a, Term b) {
        TERM_PROPAGATE_ERROR(a, b);

        // Strict less-than: equal values return false.
        if (a._data == b._data) return term_make_false();

        int tag_a = _term_tag(a);
        int tag_b = _term_tag(b);

        if (tag_a != tag_b)
            // Cross-tag ordering is undefined.
            return term_make_error();

        switch (tag_a) {
            case TERM_TAG_SMALL_INTEGER:
                return term_make_boolean_with(
                    _term_as_small_integer(a) < _term_as_small_integer(b));
            case TERM_TAG_ENTITY_ID:
                return term_make_boolean_with(
                    term_as_entity_id(a).data < term_as_entity_id(b).data);
            case TERM_TAG_OBJECT: {
                HeapTerm* p = _term_as_object(a);
                return p ? p->_term_less(b) : term_make_error();
            }
            // SHORT_STRING, ENUMERATION, reserved: no canonical ordering
            // at the runtime level.  Containers that need an order should
            // use hash or a domain-specific comparator.
            default:
                return term_make_error();
        }
    }

    Term term_hash(Term v) {
        switch (_term_tag(v)) {
            case TERM_TAG_OBJECT: {
                if (!v._data) {
                    // null OBJECT hashes to the same hash as
                    // TERM_DATA_NULL (i.e. mix(0)).
                    return term_make_integer_with(
                        (int64_t)(_term_inline_mix(0) >> 4));
                }
                HeapTerm* p = _term_as_object(v);
                return p->_term_hash();
            }
            // ERROR is intentionally not hashable: containers shouldn't
            // be keyed on errors.  Falls through to the default ERROR
            // return below.
            case TERM_TAG_ERROR:
                return term_make_error();
            default: {
                // Inline tags: hash the _data word, narrow to 60 bits so
                // it fits in a SMALL_INTEGER without overflow boxing.
                uint64_t h = _term_inline_mix(v._data) >> 4;
                return term_make_integer_with((int64_t)h);
            }
        }
    }
    
    void HeapInt64::_garbage_collected_scan() const {
        
    }

    Term HeapInt64::_term_eq(Term right) const {
        // term_eq has already confirmed right is OBJECT-tagged with a
        // different pointer than this.  Cross-subtype is ERROR; same
        // subtype compares contents.
        HeapTerm* p = _term_as_object(right);
        if (p && p->_save_type_tag() == HeapInt64::SAVE_TYPE_TAG) {
            const HeapInt64* o = static_cast<const HeapInt64*>(p);
            return term_make_boolean_with(_integer == o->_integer);
        }
        return term_make_error();
    }

    Term HeapInt64::_term_less(Term right) const {
        // Also accept comparison with an inline SMALL_INTEGER.
        if (_term_is_small_integer(right))
            return term_make_boolean_with(_integer < _term_as_small_integer(right));
        HeapTerm* p = _term_as_object(right);
        if (p && p->_save_type_tag() == HeapInt64::SAVE_TYPE_TAG) {
            const HeapInt64* o = static_cast<const HeapInt64*>(p);
            return term_make_boolean_with(_integer < o->_integer);
        }
        return term_make_error();
    }

    Term HeapInt64::_term_hash() const {
        // Hash the int64 the same way term_hash hashes an inline
        // SMALL_INTEGER, so that the rare term_make_integer_with
        // overflow path doesn't change a value's hash relative to its
        // pre-overflow representation.  Inline path hashes the entire
        // _data word (tag included); we hash _data == (i << 4) | tag.
        uint64_t bits = ((uint64_t)_integer << TERM_SHIFT) | TERM_TAG_SMALL_INTEGER;
        uint64_t h = 0;
        {
            // Replicate _term_inline_mix's splitmix64 finalizer.
            uint64_t x = bits;
            x ^= x >> 30;
            x *= 0xbf58476d1ce4e5b9ULL;
            x ^= x >> 27;
            x *= 0x94d049bb133111ebULL;
            x ^= x >> 31;
            h = x;
        }
        return term_make_integer_with((int64_t)(h >> 4));
    }


    // ---- MATTER ----
    //
    // Compile-time checks of the encoding, then a runtime test of the
    // predicate family and the operator behavior the machine.cpp
    // conservation gates rely on.

    static_assert(term_is_matter(term_make_matter(MATTER_SHIPPING_CONTAINER)));
    static_assert(!term_is_opcode(term_make_matter(MATTER_SHIPPING_CONTAINER)));
    static_assert(term_as_matter(term_make_matter(MATTER_SHIPPING_CONTAINER))
                  == MATTER_SHIPPING_CONTAINER);

    define_test("term_matter") {

        Term m = term_make_matter(MATTER_SHIPPING_CONTAINER);

        // discrimination
        assert(term_is_matter(m));
        assert(m.is_matter());
        assert(term_is_enum(m));
        assert(!term_is_null(m));
        assert(!term_is_error(m));
        assert(!term_is_boolean(m));
        assert(!term_is_character(m));
        assert(!term_is_opcode(m));
        assert(!term_is_sentinel(m));
        assert(!m.is_opcode());
        assert(!m.is_int64_t());
        assert(!_term_is_object(m));

        // payload round-trip
        assert(term_as_matter(m) == MATTER_SHIPPING_CONTAINER);
        assert(m.as_matter() == MATTER_SHIPPING_CONTAINER);

        // matter is truthy (meta bits are nonzero even for code 0)
        assert(bool(m));

        // an opcode with the same code is a different bit pattern
        Term o = term_make_opcode((int)MATTER_SHIPPING_CONTAINER);
        assert(o._data != m._data);
        assert(!term_is_matter(o));

        // arithmetic refuses matter: the enumeration tag falls through
        // to the ERROR path, so no opcode can fabricate values from it
        assert(term_is_error(m + Term(1)));
        assert(term_is_error(Term(1) + m));

        // wire form: inline tags travel bitwise (save_format encodes
        // non-OBJECT terms as their raw _data word)
        assert(!_term_is_object(m));

        // name table round-trips
        assert(MATTER_from_name("MATTER_SHIPPING_CONTAINER") == MATTER_SHIPPING_CONTAINER);
        assert(std::strcmp(name_from_MATTER(MATTER_SHIPPING_CONTAINER),
                           "MATTER_SHIPPING_CONTAINER") == 0);

        co_return;
    };

} // namespace wry



