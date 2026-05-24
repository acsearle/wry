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
#include <span>

#include "atomic.hpp"
#include "entity_id.hpp"
#include "garbage_collected.hpp"
#include "save_types.hpp"

// ====================================================================
// Value: tagged 64-bit word - pointer-or-inline-data
// ====================================================================
//
// A Value is a 64-bit tagged word; the low 4 bits are a tag indicating
// what the high 60 bits encode.  See _value_tag_e for the assignment.
//
// LOAD-BEARING INVARIANTS
//
// (1) VALUE_TAG_OBJECT == 0.  This makes the bit pattern of a non-null
//     HeapValue* literally identical to a Value with that tag.  Many
//     call paths rely on this punning.  Do not renumber.
//
// (2) The 4-bit tag space is closed by allocator-alignment convention.
//     GarbageCollected::operator new returns 16-byte-aligned memory
//     (via calloc), guaranteeing the low 4 bits of any HeapValue* are
//     zero.  Widening the tag would require bumping GC alignment too.
//
// (3) A default-constructed Value is bitwise zero: tag == OBJECT,
//     pointer == nullptr.  Value{}, Value{nullptr}, and value_make_null()
//     are the same bit pattern.  An empty persistent container (the
//     empty AMT representation) is also this pattern: empty == null
//     OBJECT.
//
// (4) Values are deeply immutable.  Every HeapValue subclass is
//     immutable post-construction; all _value_* virtuals are const and
//     return new Values rather than mutating in place.  Cells holding
//     Values may be mutated during their parent's construction (while
//     the parent is mutator-private) but not after the parent is
//     published to the collector.
//
// (5) Bit stealing is LOW-bit only.  The high bits of an OBJECT-tagged
//     Value are the pointer as-stored; no transform is required to use
//     it.  Future hardware features that mutate high bits (ARM PAC,
//     MTE) would be handled at the single extraction site
//     (_value_as_object) - never duplicate that conversion.
//
// (6) Falsey rule: bool(Value) iff (_data >> 4) != 0.  Equivalently,
//     any Value whose payload bits are all zero is falsey, regardless
//     of tag.  Examples that are falsey: null OBJECT, boolean false,
//     small_integer(0), empty short_string, ERROR.  Non-zero payloads
//     of any tag are truthy.
//
// (7) Predicate vs partial-op contract.  Total predicates (value_eq,
//     value_is_*) always return a clean bool / Value-of-bool.  Partial
//     operations (value_less, arithmetic, indexing, call) propagate
//     ERROR when given ERROR input.  ERROR is a singleton bit pattern;
//     value_eq(ERROR, ERROR) == true, value_eq(ERROR, x != ERROR) ==
//     false.  No exceptions; no NaN-style "not equal to self".
//
// LAYOUT VERSIONING
//
// The in-RAM layout is documented in this file; a layout-altering
// change is a flag-day and must be paired with a save-format version
// bump (VALUE_SAVE_VERSION in save_format).  Today there are no
// shipped saves, so the version counter is mental commitment more
// than runtime code.
//
// ====================================================================

namespace wry {

    struct HeapValue;

    struct Value {
        
        uint64_t _data = {};
                
        constexpr Value() = default;
        
        // Implicit special member functions are fine

        // Implicit construction from vocabulary types

        constexpr Value(std::nullptr_t);
        constexpr Value(bool);
        constexpr Value(int);
        constexpr Value(int64_t);
        
        constexpr Value(const char*);
        Value(std::string_view);

        Value(HeapValue const*);


        // Implicit conversion to vocabulary types
        constexpr explicit operator bool() const;

        // Member operators
        Value operator()(Value) const;
        Value operator[](Value) const;

        constexpr bool is_opcode() const;
        constexpr int as_opcode() const;
        
        constexpr bool is_int64_t() const;
        constexpr int64_t as_int64_t() const;
        
        constexpr bool is_Empty() const;
        
    }; // struct Value
    
    void garbage_collected_shade(Value const& value);
    void garbage_collected_scan(Value const& value);

    constexpr Value value_make_boolean_with(bool flag);
    constexpr Value value_make_character_with(int utf32);
    constexpr Value value_make_enumeration_with(int64_t);
    constexpr Value value_make_error();
    constexpr Value value_make_false();
    constexpr Value value_make_integer_with(int64_t z);
    constexpr Value value_make_null();
    constexpr Value value_make_empty();
    Value value_make_string_with(const char* ntbs);
    Value value_make_string_with(std::string_view);

    // DEPRECATED.  HeapArray / HeapTable are attic'd.  These declarations
    // survive only because io/json.hpp's parse_json_array / parse_json_object
    // bodies reference them; nothing actually calls those code paths today,
    // so the symbols never need to be defined.  Delete once json.hpp is
    // rewritten against the eventual persistent associative type.
    Value value_make_array();
    Value value_make_table();

    constexpr Value value_make_true();
    constexpr Value value_make_zero();
    constexpr Value value_make_one();
    constexpr Value value_make_opcode(int);

    // EntityID is a weak reference: a stable 60-bit identity that may
    // outlive any particular Entity snapshot.  Resolution to a live
    // Entity goes through World's registry.  See entity_id.hpp.
    constexpr Value value_make_entity_id(EntityID);
    constexpr bool value_is_entity_id(Value);
    constexpr EntityID value_as_entity_id(Value);
        
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
    std::string_view value_as_string_view(Value);
    std::string_view value_as_string_view_else(Value, std::string_view);
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

    // ---- Equality / ordering / hash ----
    //
    // value_eq is TOTAL: always returns a clean boolean Value
    // (true / false).  ERROR participates as an ordinary tag: an ERROR
    // equals only itself.  Cross-tag non-ERROR comparisons return ERROR
    // (incomparable).  Bitwise short-circuit makes the same-bits case
    // cheap; the deep-eq path goes through HeapValue::_value_eq.
    //
    // value_less is PARTIAL: returns a boolean Value when ordering is
    // defined, ERROR otherwise (cross-type, ERROR-input, unordered
    // values).  Equal values return false.
    //
    // value_hash is PARTIAL: returns a hash Value (small_integer-tagged)
    // for hashable inputs, ERROR otherwise.  Inline tags hash by mixing
    // the _data word; OBJECT-tagged Values dispatch to _value_hash which
    // defaults to ERROR (subclasses opt in).
    Value value_eq(Value a, Value b);
    Value value_less(Value a, Value b);
    Value value_hash(Value);

    // Convenience: most binary operators on Value want to return ERROR
    // immediately if either argument is ERROR.  Use at the top of any
    // op that participates in error propagation.  See the partial-op
    // contract in the invariants block at the top of this file.
#define VALUE_PROPAGATE_ERROR(a, b) \
    do { \
        if (value_is_error(a) || value_is_error(b)) \
            return value_make_error(); \
    } while (0)
    
    // Non-member operator overloads.
    //
    // Equality and ordering are intentionally NOT overloaded as C++ operators.
    // Use value_eq(a, b) (total, returns Value-of-bool) and value_less(a, b)
    // (partial, may return ERROR).  C++ operator== / operator<=> cannot
    // express the partial-op contract.

    Value operator+(const Value&) ;
    Value operator-(const Value&) ;
    Value operator~(const Value&) ;

    Value operator*(const Value&, const Value&) ;
    Value operator/(const Value&, const Value&) ;
    Value operator%(const Value&, const Value&) ;

    Value operator+(const Value&, const Value&) ;
    Value operator-(const Value&, const Value&) ;

    Value operator<<(const Value&, const Value&) ;
    Value operator>>(const Value&, const Value&) ;

    Value operator&(const Value&, const Value&) ;
    Value operator^(const Value&, const Value&) ;
    Value operator|(const Value&, const Value&) ;



    struct Saver;
    struct Loader;

    struct HeapValue : GarbageCollected {

        // Operator hooks.  All are const (Values are deeply immutable;
        // operations return new Values).  Pass-by-value throughout.
        // Defaults return ERROR (value_make_error) so a subclass that
        // doesn't override an operator silently rejects it.
        //
        // value_eq / value_less / value_hash follow the predicate-vs-
        // partial-op contract from the invariants block at the top of
        // this file:
        //   - _value_eq is TOTAL.  Default returns false (identity:
        //     same-pointer was already caught by the bitwise short-
        //     circuit in value_eq, so any remaining call has different
        //     pointers, and identity-only types correctly answer false).
        //     Content-comparable subclasses (HeapInt64, HeapString,
        //     future HeapArray / HeapTable) override to compare bodies.
        //   - _value_less is PARTIAL.  Default returns ERROR; orderable
        //     subclasses override.
        //   - _value_hash is PARTIAL.  Default returns ERROR; hashable
        //     subclasses override (typically HeapString-style, with the
        //     hash cached on the instance).

        virtual Value _value_eq(Value right) const;
        virtual Value _value_less(Value right) const;
        virtual Value _value_hash() const;

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
        virtual Value _value_call(Value args) const { return value_make_error(); }
        virtual Value _value_and(Value right) const { return value_make_error(); }
        virtual Value _value_or(Value right) const { return value_make_error(); }
        virtual Value _value_band(Value right) const { return value_make_error(); }
        virtual Value _value_bor(Value right) const { return value_make_error(); }
        virtual Value _value_bxor(Value right) const { return value_make_error(); }

        // ---- Save format dispatch ----
        // Every HeapValue must declare a stable type tag and a body
        // emitter.  See io/save_format.cpp for the registry.  See also
        // the parallel hook on Entity (which now inherits HeapValue).
        virtual uint64_t _save_type_tag() const = 0;
        virtual void _save_body(Saver& saver) const = 0;

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
    struct HeapInt64 final : HeapValue {
        static constexpr uint64_t SAVE_TYPE_TAG = save_type_tag_fnv1a("wry::HeapInt64");
        std::int64_t _integer;
        HeapInt64() = default;
        explicit HeapInt64(std::int64_t z);
        virtual ~HeapInt64() final = default;
        std::int64_t as_int64_t() const;
        virtual void _garbage_collected_scan() const override;
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }
        // Content equality / ordering / hash.  Two HeapInt64s with the
        // same _integer compare equal and hash equal even though they
        // sit at different addresses (no interning).
        virtual Value _value_eq(Value right) const override;
        virtual Value _value_less(Value right) const override;
        virtual Value _value_hash() const override;
        virtual uint64_t _save_type_tag() const override { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override;
    };
    
    
    

    
    
    
    // Outer tag, low 4 bits of _data.
    //
    // Tag 0 (OBJECT) is the pointer pun; load-bearing, do not renumber.
    // Tags 1..5 are the inline payload tags.  ENUMERATION subsumes the
    // small-discriminator families (boolean, character, opcode, sentinel,
    // and future atom/mod-defined enums) via a second-level meta tag in
    // bits 4..31; see _value_enum_meta_e.
    //
    // Tags 6..15 are reserved.  Do NOT use them without bumping
    // VALUE_SAVE_VERSION in save_format.  Renumbering any committed tag
    // is also a save-format break.
    enum _value_tag_e {
        VALUE_TAG_OBJECT        = 0,   // HeapValue* in the high 60 bits
        VALUE_TAG_ENUMERATION   = 1,   // meta:28 in bits 4..31, code:32 in bits 32..63
        VALUE_TAG_ERROR         = 2,   // singleton; _data == VALUE_TAG_ERROR
        VALUE_TAG_SHORT_STRING  = 3,   // len:4 in bits 4..7, chars[7] in bits 8..63
        VALUE_TAG_SMALL_INTEGER = 4,   // 60-bit signed in bits 4..63
        VALUE_TAG_ENTITY_ID     = 5,   // 60-bit EntityID in bits 4..63 (weak ref)
        // 6..15 reserved.  Future candidates: COORDINATE (i30.i30.t4),
        // TIME, WEAK_HANDLE generalization of ENTITY_ID.  None committed.
    };

    // Inner meta tag, bits 4..31 of _data when outer tag is ENUMERATION.
    //
    // VALUE_ENUM_META_BOOLEAN must be 0 so that the boolean `false`
    // bit-pattern is (ENUMERATION | 0<<4 | 0<<32) and the falsey rule
    // (_data >> 4 == 0) keeps `false` falsey.
    enum _value_enum_meta_e {
        VALUE_ENUM_META_BOOLEAN   = 0,
        VALUE_ENUM_META_CHARACTER = 1,   // code is utf32 codepoint
        VALUE_ENUM_META_OPCODE    = 2,   // code is opcode_t
        VALUE_ENUM_META_SENTINEL  = 3,   // code is _value_sentinel_e
        // 4..(2^28-1) reserved.  Candidates: ATOM (interned symbol id),
        // mod-defined enums (engine reserves a range, game data another,
        // mods a third).  Namespace governance is deferred.
    };

    // Sentinel codes within (ENUMERATION, SENTINEL_META).  Stored in
    // bits 32..63 of _data.
    enum _value_sentinel_e : int32_t {
        VALUE_SENTINEL_TOMBSTONE = 0,
        VALUE_SENTINEL_OK        = 1,
        VALUE_SENTINEL_NOTFOUND  = 2,
        VALUE_SENTINEL_RESTART   = 3,
    };

    enum {
        VALUE_SHIFT = 4,
    };

    enum : uint64_t {
        VALUE_MASK_TAG = 0x000000000000000F,
        VALUE_MASK_POINTER = 0x00007FFFFFFFFFF0,
        // Mask for the 32-bit (outer-tag + inner-meta) compound discriminator
        // used by enum predicates (value_is_boolean, value_is_character, ...).
        VALUE_MASK_TAG_AND_META = 0x00000000FFFFFFFFull,
    };

    // Compose the low 32 bits of an ENUMERATION-tagged Value for a given
    // meta.  Used as the right-hand side of `(v._data & MASK) == ...`.
    inline constexpr uint64_t _value_enum_tag_meta_bits(uint32_t meta) {
        return (uint64_t)VALUE_TAG_ENUMERATION | ((uint64_t)meta << VALUE_SHIFT);
    }

    enum : uint64_t {
        VALUE_DATA_NULL         = 0,
        VALUE_DATA_ZERO         = VALUE_TAG_SMALL_INTEGER,
        VALUE_DATA_EMPTY_STRING = VALUE_TAG_SHORT_STRING,

        // BOOLEAN_META == 0, so boolean Values have all meta bits zero.
        // false code == 0, true code == 1 (shifted to bit 32).
        VALUE_DATA_FALSE = (uint64_t)VALUE_TAG_ENUMERATION,
        VALUE_DATA_TRUE  = (uint64_t)VALUE_TAG_ENUMERATION | ((uint64_t)1 << 32),

        VALUE_DATA_ERROR = VALUE_TAG_ERROR,

        // Sentinels: (ENUMERATION, SENTINEL_META, code).  Pre-folded.
        VALUE_DATA_TOMBSTONE = (uint64_t)VALUE_TAG_ENUMERATION
                             | ((uint64_t)VALUE_ENUM_META_SENTINEL << VALUE_SHIFT)
                             | ((uint64_t)VALUE_SENTINEL_TOMBSTONE << 32),
        VALUE_DATA_OK        = (uint64_t)VALUE_TAG_ENUMERATION
                             | ((uint64_t)VALUE_ENUM_META_SENTINEL << VALUE_SHIFT)
                             | ((uint64_t)VALUE_SENTINEL_OK        << 32),
        VALUE_DATA_NOTFOUND  = (uint64_t)VALUE_TAG_ENUMERATION
                             | ((uint64_t)VALUE_ENUM_META_SENTINEL << VALUE_SHIFT)
                             | ((uint64_t)VALUE_SENTINEL_NOTFOUND  << 32),
        VALUE_DATA_RESTART   = (uint64_t)VALUE_TAG_ENUMERATION
                             | ((uint64_t)VALUE_ENUM_META_SENTINEL << VALUE_SHIFT)
                             | ((uint64_t)VALUE_SENTINEL_RESTART   << 32),
    };
    
    struct _short_string_t {
        char _tag_and_len;
        char _chars[7];
        char* data() { return _chars; }
        constexpr std::size_t size() const {
            assert((_tag_and_len & VALUE_MASK_TAG) == VALUE_TAG_SHORT_STRING);
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
    // BOOLEAN folded under ENUMERATION (meta=0); see _value_tag_e.
    constexpr Value::Value(bool flag) : _data(VALUE_DATA_FALSE | ((uint64_t)flag << 32)) {}
    constexpr Value::Value(const char* ntbs) { *this = value_make_string_with(ntbs); }
    constexpr Value::Value(int i) : _data(((int64_t)i << VALUE_SHIFT) | VALUE_TAG_SMALL_INTEGER) {}


    // Generic ENUMERATION constructor.  Pack (meta, code) into a Value.
    // meta occupies bits 4..31 (28 bits); code occupies bits 32..63 (32
    // bits, treated as signed by some accessors).
    constexpr Value value_make_enum(int meta, int code) {
        Value result;
        result._data = ((uint64_t)VALUE_TAG_ENUMERATION
                        | ((uint64_t)(uint32_t)meta << VALUE_SHIFT)
                        | ((uint64_t)(uint32_t)code << 32));
        return result;
    }

    constexpr Value value_make_opcode(int code) {
        return value_make_enum(VALUE_ENUM_META_OPCODE, code);
    }

    // TODO: fixme
    constexpr Value::Value(int64_t x) : _data(value_make_integer_with(x)._data) {}


    constexpr int value_as_opcode(Value self) {
        // Pre-condition: self is an ENUMERATION with meta == OPCODE.
        // Extracts the 32-bit code, sign-extending if it was signed.
        assert((self._data & VALUE_MASK_TAG_AND_META)
               == _value_enum_tag_meta_bits(VALUE_ENUM_META_OPCODE));
        return (int)(int32_t)(self._data >> 32);
    }

    constexpr int64_t Value::as_int64_t() const {
        return (int64_t)_data >> VALUE_SHIFT;
    }

    constexpr int Value::as_opcode() const {
        return value_as_opcode(*this);
    }

    constexpr bool Value::is_opcode() const {
        return (_data & VALUE_MASK_TAG_AND_META)
               == _value_enum_tag_meta_bits(VALUE_ENUM_META_OPCODE);
    }

    constexpr bool Value::is_int64_t() const {
        return (_data & VALUE_MASK_TAG) == VALUE_TAG_SMALL_INTEGER;
    }

    constexpr bool Value::is_Empty() const {
        return !_data;
    }


    // ENUMERATION subsumes BOOLEAN / CHARACTER / OPCODE / SENTINEL.  The
    // value_is_* predicates discriminate by the (tag | meta<<4) compound
    // discriminator in the low 32 bits.
    constexpr bool value_is_enum(Value self)      { return (self._data & VALUE_MASK_TAG) == VALUE_TAG_ENUMERATION; }
    constexpr bool value_is_null(Value self)      { return !self._data; }
    constexpr bool value_is_error(Value self)     { return (self._data & VALUE_MASK_TAG) == VALUE_TAG_ERROR; }
    constexpr bool value_is_boolean(Value self)   { return (self._data & VALUE_MASK_TAG_AND_META) == _value_enum_tag_meta_bits(VALUE_ENUM_META_BOOLEAN); }
    constexpr bool value_is_character(Value self) { return (self._data & VALUE_MASK_TAG_AND_META) == _value_enum_tag_meta_bits(VALUE_ENUM_META_CHARACTER); }
    constexpr bool value_is_opcode(Value self)    { return (self._data & VALUE_MASK_TAG_AND_META) == _value_enum_tag_meta_bits(VALUE_ENUM_META_OPCODE); }
    constexpr bool value_is_sentinel(Value self)  { return (self._data & VALUE_MASK_TAG_AND_META) == _value_enum_tag_meta_bits(VALUE_ENUM_META_SENTINEL); }


    constexpr bool value_as_boolean(Value self) {
        assert(value_is_boolean(self));
        return (bool)(self._data >> 32);
    }

    constexpr int value_as_character(Value self) {
        assert(value_is_character(self));
        return (int)(uint32_t)(self._data >> 32);
    }

    constexpr Value value_make_boolean_with(bool flag) {
        return value_make_enum(VALUE_ENUM_META_BOOLEAN, flag ? 1 : 0);
    }

    constexpr Value value_make_true() {
        return value_make_boolean_with(true);
    }

    constexpr Value value_make_false() {
        return value_make_boolean_with(false);
    }

    constexpr Value value_make_character_with(int utf32) {
        return value_make_enum(VALUE_ENUM_META_CHARACTER, utf32);
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
    
    
    
    constexpr int _value_tag(Value self) { return self._data & VALUE_MASK_TAG; }
    constexpr bool _value_is_small_integer(Value self) { return _value_tag(self) == VALUE_TAG_SMALL_INTEGER; }
    constexpr bool _value_is_object(Value self) { return _value_tag(self) == VALUE_TAG_OBJECT; }
    constexpr bool _value_is_short_string(Value self) { return _value_tag(self) == VALUE_TAG_SHORT_STRING; }
    // (The canonical _value_is_tombstone lives a little further down,
    // matching the encoding _data == VALUE_DATA_TOMBSTONE rather than
    // tag-only.  A typo'd duplicate _vaue_is_tombstone was removed.)


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
    
    inline HeapValue* _value_as_nullable_pointer(Value self) {
        return _value_is_object(self) ? _value_as_object(self) : nullptr;
    }
    
    constexpr int64_t _value_as_small_integer(Value self) {
        assert(_value_is_small_integer(self));
        return (int64_t)self._data >> VALUE_SHIFT;
    }

    constexpr bool _value_is_tombstone(Value self) {
        return self._data == VALUE_DATA_TOMBSTONE;
    }

    // EntityID packing: 60 bits of the underlying uint64_t identity, in
    // bits 4..63, with VALUE_TAG_ENTITY_ID in the low 4 bits.  The
    // unconditional assert documents that EntityID issuance must respect
    // the 60-bit ceiling; the existing oracle()-based scheme will need to
    // honour this (or saves must compact IDs at load time, per
    // entity_id.hpp's note).
    constexpr Value value_make_entity_id(EntityID eid) {
        assert((eid.data >> 60) == 0 && "EntityID overflows 60-bit Value packing");
        Value result;
        result._data = ((uint64_t)eid.data << VALUE_SHIFT) | VALUE_TAG_ENTITY_ID;
        return result;
    }

    constexpr bool value_is_entity_id(Value self) {
        return (self._data & VALUE_MASK_TAG) == VALUE_TAG_ENTITY_ID;
    }

    constexpr EntityID value_as_entity_id(Value self) {
        assert(value_is_entity_id(self));
        return EntityID{ self._data >> VALUE_SHIFT };
    }
    
    constexpr std::pair<int, int> value_as_enum(Value self) {
        assert(value_is_enum(self));
        int code = (int)(int32_t)(self._data >> 32);
        // Meta lives in bits 4..31 of _data; extract as unsigned 28-bit
        // and return as int (callers compare against VALUE_ENUM_META_*).
        int meta = (int)(((uint32_t)self._data) >> VALUE_SHIFT);
        return {meta, code};
    }
    
    
    
    inline void
    garbage_collected_roots_add(Value value) {
        garbage_collected_roots_add(_value_as_nullable_pointer(value));
    }

    inline void
    garbage_collected_roots_subtract(Value value) {
        garbage_collected_roots_subtract(_value_as_nullable_pointer(value));
    }

    
    // TODO: This seems quite close to a common implementation with Root<T*>
    // but the distinction between pointer and object semantics is tough

    template<>
    struct Root<Value> {
        
        Value _value;
        
        Root() = default;
        
        Root(Root const& other)
        : _value(other._value) {
            garbage_collected_roots_add(_value);
        }
        
        Root(Root&& other)
        : _value(std::exchange(other._value, Value{})) {
        }
        
        ~Root() {
            garbage_collected_roots_subtract(_value);
        }
        
        Root& operator=(Root const& other) {
            garbage_collected_roots_subtract(_value);
            _value = other._value;
            garbage_collected_roots_add(_value);
            return *this;
        }
        
        Root& operator=(Root&& other) {
            garbage_collected_roots_subtract(_value);
            _value = std::exchange(other._value, Value{});
            return *this;
        }
        
        explicit Root(Value other)
        : _value(other) {
            garbage_collected_roots_add(_value);
        }
        
        Root& operator=(Value other) {
            garbage_collected_roots_subtract(_value);
            _value = other;
            garbage_collected_roots_add(_value);
            return *this;
        }
        
        operator Value() const {
            return _value;
        }
        
    }; // struct Root<Value>



    inline Value::Value(HeapValue const* ptr)
    : _data((uint64_t)ptr) {
        assert(_value_as_nullable_pointer(*this) == ptr);
    }

    constexpr Value value_make_integer_with(std::int64_t z) {
        Value result;
        std::int64_t y = z << 4;
        if ((y >> 4) == z) {
            result._data = y | VALUE_TAG_SMALL_INTEGER;
        } else {
            result._data = (uint64_t)new HeapInt64(z);
        }
        return result;
    }

    constexpr Value value_make_zero() {
        return value_make_integer_with(0);
    }

    constexpr Value value_make_one() {
        return value_make_integer_with(1);
    }

    
} // namespace wry

#endif /* value_hpp */
