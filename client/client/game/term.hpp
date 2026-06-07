//
//  term.hpp
//  client
//
//  Created by Antony Searle on 31/5/2024.
//

#ifndef term_hpp
#define term_hpp

#include <cassert>
#include <string_view>
#include <span>

#include "atomic.hpp"
#include "entity_id.hpp"
#include "garbage_collected.hpp"
#include "save_types.hpp"

// ====================================================================
// Term: tagged 64-bit word - pointer-or-inline-data
// ====================================================================
//
// A Term is a 64-bit tagged word; the low 4 bits are a tag indicating
// what the high 60 bits encode.  See _term_tag_e for the assignment.
//
// LOAD-BEARING INVARIANTS
//
// (1) TERM_TAG_OBJECT == 0.  This makes the bit pattern of a non-null
//     HeapTerm* literally identical to a Term with that tag.  Many
//     call paths rely on this punning.  Do not renumber.
//
// (2) The 4-bit tag space is closed by allocator-alignment convention.
//     GarbageCollected::operator new returns 16-byte-aligned memory
//     (via calloc), guaranteeing the low 4 bits of any HeapTerm* are
//     zero.  Widening the tag would require bumping GC alignment too.
//
// (3) A default-constructed Term is bitwise zero: tag == OBJECT,
//     pointer == nullptr.  Term{}, Term{nullptr}, and term_make_null()
//     are the same bit pattern.  An empty persistent container (the
//     empty AMT representation) is also this pattern: empty == null
//     OBJECT.
//
// (4) Values are deeply immutable.  Every HeapTerm subclass is
//     immutable post-construction; all _term_* virtuals are const and
//     return new Values rather than mutating in place.  Cells holding
//     Values may be mutated during their parent's construction (while
//     the parent is mutator-private) but not after the parent is
//     published to the collector.
//
// (5) Bit stealing is LOW-bit only.  The high bits of an OBJECT-tagged
//     Term are the pointer as-stored; no transform is required to use
//     it.  Future hardware features that mutate high bits (ARM PAC,
//     MTE) would be handled at the single extraction site
//     (_term_as_object) - never duplicate that conversion.
//
// (6) Falsey rule: bool(Term) iff (_data >> 4) != 0.  Equivalently,
//     any Term whose payload bits are all zero is falsey, regardless
//     of tag.  Examples that are falsey: null OBJECT, boolean false,
//     small_integer(0), empty short_string, ERROR.  Non-zero payloads
//     of any tag are truthy.
//
// (7) Predicate vs partial-op contract.  Total predicates (term_eq,
//     term_is_*) always return a clean bool / Term-of-bool.  Partial
//     operations (term_less, arithmetic, indexing, call) propagate
//     ERROR when given ERROR input.  ERROR is a singleton bit pattern;
//     term_eq(ERROR, ERROR) == true, term_eq(ERROR, x != ERROR) ==
//     false.  No exceptions; no NaN-style "not equal to self".
//
// LAYOUT VERSIONING
//
// The in-RAM layout is documented in this file; a layout-altering
// change is a flag-day and must be paired with a save-format version
// bump (TERM_SAVE_VERSION in save_format).  Today there are no
// shipped saves, so the version counter is mental commitment more
// than runtime code.
//
// ====================================================================

namespace wry {

    struct HeapTerm;

    struct Term {
        
        uint64_t _data = {};
                
        constexpr Term() = default;
        
        // Implicit special member functions are fine

        // Implicit construction from vocabulary types

        constexpr Term(std::nullptr_t);
        constexpr Term(bool);
        constexpr Term(int);
        constexpr Term(int64_t);
        
        constexpr Term(const char*);
        Term(std::string_view);

        Term(HeapTerm const*);


        // Implicit conversion to vocabulary types
        constexpr explicit operator bool() const;

        // Member operators
        Term operator()(Term) const;
        Term operator[](Term) const;

        constexpr bool is_opcode() const;
        constexpr int as_opcode() const;
        
        constexpr bool is_int64_t() const;
        constexpr int64_t as_int64_t() const;
        
        constexpr bool is_Empty() const;
        
    }; // struct Term
    
    void garbage_collected_shade(Term const& value);
    void garbage_collected_scan(Term const& value);

    constexpr Term term_make_boolean_with(bool flag);
    constexpr Term term_make_character_with(int utf32);
    constexpr Term term_make_enumeration_with(int64_t);
    constexpr Term term_make_error();
    constexpr Term term_make_false();
    constexpr Term term_make_integer_with(int64_t z);
    constexpr Term term_make_null();
    constexpr Term term_make_empty();
    Term term_make_string_with(const char* ntbs);
    Term term_make_string_with(std::string_view);

    constexpr Term term_make_true();
    constexpr Term term_make_zero();
    constexpr Term term_make_one();
    constexpr Term term_make_opcode(int);

    // EntityID is a weak reference: a stable 60-bit identity that may
    // outlive any particular Entity snapshot.  Resolution to a live
    // Entity goes through World's registry.  See entity_id.hpp.
    constexpr Term term_make_entity_id(EntityID);
    constexpr bool term_is_entity_id(Term);
    constexpr EntityID term_as_entity_id(Term);
        
    constexpr bool term_is_boolean(Term);
    constexpr bool term_is_character(Term);
    constexpr bool term_is_error(Term);
    constexpr bool term_is_null(Term);
    constexpr bool term_is_enum(Term);
    
    constexpr bool term_as_boolean(Term);
    constexpr bool term_as_boolean_else(Term, bool);
    constexpr int  term_as_character(Term);
    constexpr int  term_as_character_else(Term, int);
    constexpr std::pair<int, int> term_as_enum(Term);
    constexpr int64_t term_as_int64_t(Term);
    constexpr int64_t term_as_int64_t_else(Term, int64_t);
    std::string_view term_as_string_view(Term);
    std::string_view term_as_string_view_else(Term, std::string_view);
    constexpr int term_as_opcode(Term);
    
    bool term_contains(Term, Term key);
    Term term_find(Term, Term key);
    Term term_insert_or_assign(Term self, Term key, Term value);
    Term term_erase(Term self, Term key);
    size_t term_size(Term);
    void term_push_back(Term, Term value);
    void term_pop_back(Term);
    Term term_back(Term);
    Term term_front(Term);

    // ---- Equality / ordering / hash ----
    //
    // term_eq is TOTAL: always returns a clean boolean Term
    // (true / false).  ERROR participates as an ordinary tag: an ERROR
    // equals only itself.  Cross-tag non-ERROR comparisons return ERROR
    // (incomparable).  Bitwise short-circuit makes the same-bits case
    // cheap; the deep-eq path goes through HeapTerm::_term_eq.
    //
    // term_less is PARTIAL: returns a boolean Term when ordering is
    // defined, ERROR otherwise (cross-type, ERROR-input, unordered
    // values).  Equal values return false.
    //
    // term_hash is PARTIAL: returns a hash Term (small_integer-tagged)
    // for hashable inputs, ERROR otherwise.  Inline tags hash by mixing
    // the _data word; OBJECT-tagged Values dispatch to _term_hash which
    // defaults to ERROR (subclasses opt in).
    Term term_eq(Term a, Term b);
    Term term_less(Term a, Term b);
    Term term_hash(Term);

    // Convenience: most binary operators on Term want to return ERROR
    // immediately if either argument is ERROR.  Use at the top of any
    // op that participates in error propagation.  See the partial-op
    // contract in the invariants block at the top of this file.
#define TERM_PROPAGATE_ERROR(a, b) \
    do { \
        if (term_is_error(a) || term_is_error(b)) \
            return term_make_error(); \
    } while (0)
    
    // Non-member operator overloads.
    //
    // Equality and ordering are intentionally NOT overloaded as C++ operators.
    // Use term_eq(a, b) (total, returns Term-of-bool) and term_less(a, b)
    // (partial, may return ERROR).  C++ operator== / operator<=> cannot
    // express the partial-op contract.

    Term operator+(const Term&) ;
    Term operator-(const Term&) ;
    Term operator~(const Term&) ;

    Term operator*(const Term&, const Term&) ;
    Term operator/(const Term&, const Term&) ;
    Term operator%(const Term&, const Term&) ;

    Term operator+(const Term&, const Term&) ;
    Term operator-(const Term&, const Term&) ;

    Term operator<<(const Term&, const Term&) ;
    Term operator>>(const Term&, const Term&) ;

    Term operator&(const Term&, const Term&) ;
    Term operator^(const Term&, const Term&) ;
    Term operator|(const Term&, const Term&) ;



    struct Saver;
    struct Loader;

    struct HeapTerm : GarbageCollected {

        // Operator hooks.  All are const (Values are deeply immutable;
        // operations return new Values).  Pass-by-value throughout.
        // Defaults return ERROR (term_make_error) so a subclass that
        // doesn't override an operator silently rejects it.
        //
        // term_eq / term_less / term_hash follow the predicate-vs-
        // partial-op contract from the invariants block at the top of
        // this file:
        //   - _term_eq is TOTAL.  Default returns false (identity:
        //     same-pointer was already caught by the bitwise short-
        //     circuit in term_eq, so any remaining call has different
        //     pointers, and identity-only types correctly answer false).
        //     Content-comparable subclasses (HeapInt64, HeapString,
        //     future HeapArray / HeapTable) override to compare bodies.
        //   - _term_less is PARTIAL.  Default returns ERROR; orderable
        //     subclasses override.
        //   - _term_hash is PARTIAL.  Default returns ERROR; hashable
        //     subclasses override (typically HeapString-style, with the
        //     hash cached on the instance).

        virtual Term _term_eq(Term right) const;
        virtual Term _term_less(Term right) const;
        virtual Term _term_hash() const;

        virtual Term _term_insert_or_assign(Term key, Term value) const;
        virtual bool _term_empty() const;
        virtual size_t _term_size() const;
        virtual bool _term_contains(Term key) const;
        virtual Term _term_find(Term key) const;
        virtual Term _term_erase(Term key) const;
        virtual Term _term_add(Term right) const;
        virtual Term _term_sub(Term right) const;
        virtual Term _term_mul(Term right) const;
        virtual Term _term_div(Term right) const;
        virtual Term _term_mod(Term right) const;
        virtual Term _term_rshift(Term right) const;
        virtual Term _term_lshift(Term right) const;
        virtual Term _term_call(Term args) const { return term_make_error(); }
        virtual Term _term_and(Term right) const { return term_make_error(); }
        virtual Term _term_or(Term right) const { return term_make_error(); }
        virtual Term _term_band(Term right) const { return term_make_error(); }
        virtual Term _term_bor(Term right) const { return term_make_error(); }
        virtual Term _term_bxor(Term right) const { return term_make_error(); }

        // ---- Save format dispatch ----
        // Every HeapTerm must declare a stable type tag and a body
        // emitter.  See io/save_format.cpp for the registry.  See also
        // the parallel hook on Entity (which now inherits HeapTerm).
        virtual uint64_t _save_type_tag() const = 0;
        virtual void _save_body(Saver& saver) const = 0;

    };
    
    
    
    
    
    
    constexpr int _term_tag(Term);
    constexpr bool _term_is_small_integer(Term);
    constexpr bool _term_is_object(Term);
    constexpr bool _term_is_short_string(Term);
    constexpr bool _term_is_tombstone(Term);
    
    HeapTerm* _term_as_object(Term);
    HeapTerm const* _term_as_heap_value_else(Term, HeapTerm const*);
    GarbageCollected const* _term_as_garbage_collected_else(Term, GarbageCollected const*);
    constexpr int64_t _term_as_small_integer(Term);
    constexpr int64_t _term_as_small_integer_else(Term, int64_t);
    Term _term_make_with(const GarbageCollected* object);

    // lifetimebomb
    [[nodiscard]] std::string_view _term_as_short_string(Term const&);
    [[nodiscard]] std::string_view _term_as_short_string_else(Term const&, std::string_view);

    
    
    
    inline void garbage_collected_shade(Term const& self) {
        if (_term_is_object(self))
            garbage_collected_shade(_term_as_object(self));
    }
    
    inline void garbage_collected_scan(Term const& self) {
        if (_term_is_object(self))
            garbage_collected_scan(_term_as_object(self));
    }
        
    // size_t hash(const Term& self);
    
    
    
    
    
    
    
    
    // TODO: upgrade to array of limbs of arbitrary precision integer
    struct HeapInt64 final : HeapTerm {
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
        virtual Term _term_eq(Term right) const override;
        virtual Term _term_less(Term right) const override;
        virtual Term _term_hash() const override;
        virtual uint64_t _save_type_tag() const override { return SAVE_TYPE_TAG; }
        virtual void _save_body(Saver& saver) const override;
    };
    
    
    

    
    
    
    // Outer tag, low 4 bits of _data.
    //
    // Tag 0 (OBJECT) is the pointer pun; load-bearing, do not renumber.
    // Tags 1..5 are the inline payload tags.  ENUMERATION subsumes the
    // small-discriminator families (boolean, character, opcode, sentinel,
    // and future atom/mod-defined enums) via a second-level meta tag in
    // bits 4..31; see _term_enum_meta_e.
    //
    // Tags 6..15 are reserved.  Do NOT use them without bumping
    // TERM_SAVE_VERSION in save_format.  Renumbering any committed tag
    // is also a save-format break.
    enum _term_tag_e {
        TERM_TAG_OBJECT        = 0,   // HeapTerm* in the high 60 bits
        TERM_TAG_ENUMERATION   = 1,   // meta:28 in bits 4..31, code:32 in bits 32..63
        TERM_TAG_ERROR         = 2,   // singleton; _data == TERM_TAG_ERROR
        TERM_TAG_SHORT_STRING  = 3,   // len:4 in bits 4..7, chars[7] in bits 8..63
        TERM_TAG_SMALL_INTEGER = 4,   // 60-bit signed in bits 4..63
        TERM_TAG_ENTITY_ID     = 5,   // 60-bit EntityID in bits 4..63 (weak ref)
        // 6..15 reserved.  Future candidates: COORDINATE (i30.i30.t4),
        // TIME, WEAK_HANDLE generalization of ENTITY_ID.  None committed.
    };

    // Inner meta tag, bits 4..31 of _data when outer tag is ENUMERATION.
    //
    // TERM_ENUM_META_BOOLEAN must be 0 so that the boolean `false`
    // bit-pattern is (ENUMERATION | 0<<4 | 0<<32) and the falsey rule
    // (_data >> 4 == 0) keeps `false` falsey.
    enum _term_enum_meta_e {
        TERM_ENUM_META_BOOLEAN   = 0,
        TERM_ENUM_META_CHARACTER = 1,   // code is utf32 codepoint
        TERM_ENUM_META_OPCODE    = 2,   // code is opcode_t
        TERM_ENUM_META_SENTINEL  = 3,   // code is _term_sentinel_e
        // 4..(2^28-1) reserved.  Candidates: ATOM (interned symbol id),
        // mod-defined enums (engine reserves a range, game data another,
        // mods a third).  Namespace governance is deferred.
    };

    // Sentinel codes within (ENUMERATION, SENTINEL_META).  Stored in
    // bits 32..63 of _data.
    enum _term_sentinel_e : int32_t {
        TERM_SENTINEL_TOMBSTONE = 0,
        TERM_SENTINEL_OK        = 1,
        TERM_SENTINEL_NOTFOUND  = 2,
        TERM_SENTINEL_RESTART   = 3,
    };

    enum {
        TERM_SHIFT = 4,
    };

    enum : uint64_t {
        TERM_MASK_TAG = 0x000000000000000F,
        TERM_MASK_POINTER = 0x00007FFFFFFFFFF0,
        // Mask for the 32-bit (outer-tag + inner-meta) compound discriminator
        // used by enum predicates (term_is_boolean, term_is_character, ...).
        TERM_MASK_TAG_AND_META = 0x00000000FFFFFFFFull,
    };

    // Compose the low 32 bits of an ENUMERATION-tagged Term for a given
    // meta.  Used as the right-hand side of `(v._data & MASK) == ...`.
    inline constexpr uint64_t _term_enum_tag_meta_bits(uint32_t meta) {
        return (uint64_t)TERM_TAG_ENUMERATION | ((uint64_t)meta << TERM_SHIFT);
    }

    enum : uint64_t {
        TERM_DATA_NULL         = 0,
        TERM_DATA_ZERO         = TERM_TAG_SMALL_INTEGER,
        TERM_DATA_EMPTY_STRING = TERM_TAG_SHORT_STRING,

        // BOOLEAN_META == 0, so boolean Values have all meta bits zero.
        // false code == 0, true code == 1 (shifted to bit 32).
        TERM_DATA_FALSE = (uint64_t)TERM_TAG_ENUMERATION,
        TERM_DATA_TRUE  = (uint64_t)TERM_TAG_ENUMERATION | ((uint64_t)1 << 32),

        TERM_DATA_ERROR = TERM_TAG_ERROR,

        // Sentinels: (ENUMERATION, SENTINEL_META, code).  Pre-folded.
        TERM_DATA_TOMBSTONE = (uint64_t)TERM_TAG_ENUMERATION
                             | ((uint64_t)TERM_ENUM_META_SENTINEL << TERM_SHIFT)
                             | ((uint64_t)TERM_SENTINEL_TOMBSTONE << 32),
        TERM_DATA_OK        = (uint64_t)TERM_TAG_ENUMERATION
                             | ((uint64_t)TERM_ENUM_META_SENTINEL << TERM_SHIFT)
                             | ((uint64_t)TERM_SENTINEL_OK        << 32),
        TERM_DATA_NOTFOUND  = (uint64_t)TERM_TAG_ENUMERATION
                             | ((uint64_t)TERM_ENUM_META_SENTINEL << TERM_SHIFT)
                             | ((uint64_t)TERM_SENTINEL_NOTFOUND  << 32),
        TERM_DATA_RESTART   = (uint64_t)TERM_TAG_ENUMERATION
                             | ((uint64_t)TERM_ENUM_META_SENTINEL << TERM_SHIFT)
                             | ((uint64_t)TERM_SENTINEL_RESTART   << 32),
    };
    
    struct _short_string_t {
        char _tag_and_len;
        char _chars[7];
        char* data() { return _chars; }
        constexpr std::size_t size() const {
            assert((_tag_and_len & TERM_MASK_TAG) == TERM_TAG_SHORT_STRING);
            return _tag_and_len >> TERM_SHIFT;
        }
        constexpr std::string_view as_string_view() const {
            return std::string_view(_chars, size());
        }
        //std::size_t hash() const {
        //    return std::hash<std::string_view>()(as_string_view());
        //}
    };
    
    
    
    
    constexpr Term::Term(std::nullptr_t) : _data(TERM_DATA_NULL) {}
    // BOOLEAN folded under ENUMERATION (meta=0); see _term_tag_e.
    constexpr Term::Term(bool flag) : _data(TERM_DATA_FALSE | ((uint64_t)flag << 32)) {}
    constexpr Term::Term(const char* ntbs) { *this = term_make_string_with(ntbs); }
    constexpr Term::Term(int i) : _data(((int64_t)i << TERM_SHIFT) | TERM_TAG_SMALL_INTEGER) {}


    // Generic ENUMERATION constructor.  Pack (meta, code) into a Term.
    // meta occupies bits 4..31 (28 bits); code occupies bits 32..63 (32
    // bits, treated as signed by some accessors).
    constexpr Term term_make_enum(int meta, int code) {
        Term result;
        result._data = ((uint64_t)TERM_TAG_ENUMERATION
                        | ((uint64_t)(uint32_t)meta << TERM_SHIFT)
                        | ((uint64_t)(uint32_t)code << 32));
        return result;
    }

    constexpr Term term_make_opcode(int code) {
        return term_make_enum(TERM_ENUM_META_OPCODE, code);
    }

    // TODO: fixme
    constexpr Term::Term(int64_t x) : _data(term_make_integer_with(x)._data) {}


    constexpr int term_as_opcode(Term self) {
        // Pre-condition: self is an ENUMERATION with meta == OPCODE.
        // Extracts the 32-bit code, sign-extending if it was signed.
        assert((self._data & TERM_MASK_TAG_AND_META)
               == _term_enum_tag_meta_bits(TERM_ENUM_META_OPCODE));
        return (int)(int32_t)(self._data >> 32);
    }

    constexpr int64_t Term::as_int64_t() const {
        return (int64_t)_data >> TERM_SHIFT;
    }

    constexpr int Term::as_opcode() const {
        return term_as_opcode(*this);
    }

    constexpr bool Term::is_opcode() const {
        return (_data & TERM_MASK_TAG_AND_META)
               == _term_enum_tag_meta_bits(TERM_ENUM_META_OPCODE);
    }

    constexpr bool Term::is_int64_t() const {
        return (_data & TERM_MASK_TAG) == TERM_TAG_SMALL_INTEGER;
    }

    constexpr bool Term::is_Empty() const {
        return !_data;
    }


    // ENUMERATION subsumes BOOLEAN / CHARACTER / OPCODE / SENTINEL.  The
    // term_is_* predicates discriminate by the (tag | meta<<4) compound
    // discriminator in the low 32 bits.
    constexpr bool term_is_enum(Term self)      { return (self._data & TERM_MASK_TAG) == TERM_TAG_ENUMERATION; }
    constexpr bool term_is_null(Term self)      { return !self._data; }
    constexpr bool term_is_error(Term self)     { return (self._data & TERM_MASK_TAG) == TERM_TAG_ERROR; }
    constexpr bool term_is_boolean(Term self)   { return (self._data & TERM_MASK_TAG_AND_META) == _term_enum_tag_meta_bits(TERM_ENUM_META_BOOLEAN); }
    constexpr bool term_is_character(Term self) { return (self._data & TERM_MASK_TAG_AND_META) == _term_enum_tag_meta_bits(TERM_ENUM_META_CHARACTER); }
    constexpr bool term_is_opcode(Term self)    { return (self._data & TERM_MASK_TAG_AND_META) == _term_enum_tag_meta_bits(TERM_ENUM_META_OPCODE); }
    constexpr bool term_is_sentinel(Term self)  { return (self._data & TERM_MASK_TAG_AND_META) == _term_enum_tag_meta_bits(TERM_ENUM_META_SENTINEL); }


    constexpr bool term_as_boolean(Term self) {
        assert(term_is_boolean(self));
        return (bool)(self._data >> 32);
    }

    constexpr int term_as_character(Term self) {
        assert(term_is_character(self));
        return (int)(uint32_t)(self._data >> 32);
    }

    constexpr Term term_make_boolean_with(bool flag) {
        return term_make_enum(TERM_ENUM_META_BOOLEAN, flag ? 1 : 0);
    }

    constexpr Term term_make_true() {
        return term_make_boolean_with(true);
    }

    constexpr Term term_make_false() {
        return term_make_boolean_with(false);
    }

    constexpr Term term_make_character_with(int utf32) {
        return term_make_enum(TERM_ENUM_META_CHARACTER, utf32);
    }
    
    constexpr Term::operator bool() const {
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
    
    constexpr Term term_make_error() { Term result; result._data = TERM_TAG_ERROR; return result; }
    constexpr Term term_make_null() { Term result; result._data = 0; return result; }
    constexpr Term term_make_empty() { Term result; result._data = 0; return result; }
    
    
    
    constexpr int _term_tag(Term self) { return self._data & TERM_MASK_TAG; }
    constexpr bool _term_is_small_integer(Term self) { return _term_tag(self) == TERM_TAG_SMALL_INTEGER; }
    constexpr bool _term_is_object(Term self) { return _term_tag(self) == TERM_TAG_OBJECT; }
    constexpr bool _term_is_short_string(Term self) { return _term_tag(self) == TERM_TAG_SHORT_STRING; }
    // (The canonical _term_is_tombstone lives a little further down,
    // matching the encoding _data == TERM_DATA_TOMBSTONE rather than
    // tag-only.  A typo'd duplicate _vaue_is_tombstone was removed.)


    constexpr bool term_is_RESTART(Term self) {
        return self._data == TERM_DATA_RESTART;
    }
    
    constexpr Term term_make_RESTART() {
        Term result;
        result._data = TERM_DATA_RESTART;
        return result;
    }
    
    constexpr bool term_is_OK(Term self) {
        return self._data == TERM_DATA_OK;
    }
    
    constexpr Term term_make_OK() {
        Term result;
        result._data = TERM_DATA_OK;
        return result;
    }
    
    constexpr bool term_is_NOTFOUND(Term self) {
        return self._data == TERM_DATA_NOTFOUND;
    }
    
    constexpr Term term_make_NOTFOUND() {
        Term result;
        result._data = TERM_DATA_NOTFOUND;
        return result;
    }
    
    inline HeapTerm* _term_as_object(Term self) {
        assert(_term_is_object(self));
        return (HeapTerm*)self._data;
    }
    
    inline HeapTerm* _term_as_nullable_pointer(Term self) {
        return _term_is_object(self) ? _term_as_object(self) : nullptr;
    }
    
    constexpr int64_t _term_as_small_integer(Term self) {
        assert(_term_is_small_integer(self));
        return (int64_t)self._data >> TERM_SHIFT;
    }

    constexpr bool _term_is_tombstone(Term self) {
        return self._data == TERM_DATA_TOMBSTONE;
    }

    // EntityID packing: 60 bits of the underlying uint64_t identity, in
    // bits 4..63, with TERM_TAG_ENTITY_ID in the low 4 bits.  The
    // unconditional assert documents that EntityID issuance must respect
    // the 60-bit ceiling; the existing oracle()-based scheme will need to
    // honour this (or saves must compact IDs at load time, per
    // entity_id.hpp's note).
    constexpr Term term_make_entity_id(EntityID eid) {
        assert((eid.data >> 60) == 0 && "EntityID overflows 60-bit Term packing");
        Term result;
        result._data = ((uint64_t)eid.data << TERM_SHIFT) | TERM_TAG_ENTITY_ID;
        return result;
    }

    constexpr bool term_is_entity_id(Term self) {
        return (self._data & TERM_MASK_TAG) == TERM_TAG_ENTITY_ID;
    }

    constexpr EntityID term_as_entity_id(Term self) {
        assert(term_is_entity_id(self));
        return EntityID{ self._data >> TERM_SHIFT };
    }
    
    constexpr std::pair<int, int> term_as_enum(Term self) {
        assert(term_is_enum(self));
        int code = (int)(int32_t)(self._data >> 32);
        // Meta lives in bits 4..31 of _data; extract as unsigned 28-bit
        // and return as int (callers compare against TERM_ENUM_META_*).
        int meta = (int)(((uint32_t)self._data) >> TERM_SHIFT);
        return {meta, code};
    }
    
    
    
    inline void
    garbage_collected_roots_add(Term value) {
        garbage_collected_roots_add(_term_as_nullable_pointer(value));
    }

    inline void
    garbage_collected_roots_subtract(Term value) {
        garbage_collected_roots_subtract(_term_as_nullable_pointer(value));
    }

    
    // TODO: This seems quite close to a common implementation with Root<T*>
    // but the distinction between pointer and object semantics is tough

    template<>
    struct Root<Term> {
        
        Term _value;
        
        Root() = default;
        
        Root(Root const& other)
        : _value(other._value) {
            garbage_collected_roots_add(_value);
        }
        
        Root(Root&& other)
        : _value(std::exchange(other._value, Term{})) {
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
            _value = std::exchange(other._value, Term{});
            return *this;
        }
        
        explicit Root(Term other)
        : _value(other) {
            garbage_collected_roots_add(_value);
        }
        
        Root& operator=(Term other) {
            garbage_collected_roots_subtract(_value);
            _value = other;
            garbage_collected_roots_add(_value);
            return *this;
        }
        
        operator Term() const {
            return _value;
        }
        
    }; // struct Root<Term>



    inline Term::Term(HeapTerm const* ptr)
    : _data((uint64_t)ptr) {
        assert(_term_as_nullable_pointer(*this) == ptr);
    }

    constexpr Term term_make_integer_with(std::int64_t z) {
        Term result;
        std::int64_t y = z << 4;
        if ((y >> 4) == z) {
            result._data = y | TERM_TAG_SMALL_INTEGER;
        } else {
            result._data = (uint64_t)new HeapInt64(z);
        }
        return result;
    }

    constexpr Term term_make_zero() {
        return term_make_integer_with(0);
    }

    constexpr Term term_make_one() {
        return term_make_integer_with(1);
    }

    
} // namespace wry

#endif /* term_hpp */
