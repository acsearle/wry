//
//  value.hpp
//  client
//
//  Created by Antony Searle on 31/5/2024.
//

#ifndef value_hpp
#define value_hpp

#include <cinttypes>

#include <bit>

#include "../client/hash.hpp"
#include "debug.hpp"
#include "gc.hpp"

namespace gc {
    
    struct Value;
    
    struct _deferred_subscript_t;
    
    struct Value {
        
        uint64_t _data;
        
        int _discriminant() const;
        bool _is_small_integer() const;
        bool _is_pointer() const;
        bool _is_short_string() const;
        bool _is_tombstone() const;

        // these logical types are always stored inline
        bool is_enumeration() const;
        bool is_null() const;
        bool is_error() const;
        bool is_boolean() const;
        bool is_character() const;
        
        // Several types have only a small number of values, we can pack
        // them all into a single tag?
        // true, false, error, tombstone, UTF-32 character
        
        constexpr Value() = default;
        
        // implicit copy and move constructors
        
        constexpr Value(std::nullptr_t);
        constexpr Value(bool flag);
        Value(const char* ntbs);
        template<std::size_t N, typename = std::enable_if_t<(N > 0)>>
        constexpr Value(const char (&ntbs)[N]);
        constexpr Value(int i);
        
        // implicit destructor

        // implicit copy and move assignment operators
        
        const Object* _as_pointer() const;
        
        const Object* _as_pointer_or_nullptr() const;
        int64_t _as_small_integer() const;
        
        std::string_view _as_short_string() const;
        
        bool as_boolean() const;
        int64_t as_enumeration() const;
        
        static Value _from_object(const Object* object);
        static Value from_int64(std::int64_t z);
        static Value from_ntbs(const char* ntbs);
        static Value from_bool(bool flag);

        // monostates
        static Value make_error();
        static Value make_null();
        static Value make_tombstone();
                        
        // common interface functions
        std::size_t hash() const;
        
        std::size_t size() const;
        bool contains(Value key) const;
        Value find(Value key) const;
        
        void resize(Value);
        Value insert_or_assign(Value key, Value value);
        Value erase(Value key);

        // these operators must be defined inline
        Value operator()() const;
        Value operator[](Value) const;
        explicit operator bool() const;
        
        _deferred_subscript_t operator[](Value);

    }; // Value
    
    struct _deferred_subscript_t {
        Value& container;
        Value key;
        operator Value() &&;
        _deferred_subscript_t&& operator=(Value desired) &&;
        _deferred_subscript_t&& operator=(_deferred_subscript_t&& desired) &&;
    };

    
    
    
    
    
        

    template<>
    struct Traced<Value> {
        
        Atomic<Value> _value;

        Traced() = default;
        Traced(const Traced& other);
        ~Traced() = default;
        Traced& operator=(const Traced& other);
        explicit Traced(const Value& other);
        Traced& operator=(const Value& other);
        explicit operator bool() const;
        operator Value() const;
        bool operator==(const Traced& other) const;
        auto operator<=>(const Traced& other) const;
        Value get() const;
    };
    
    template<>
    struct Traced<Atomic<Value>> {
        // ...
    };
    
    
    
    
    /*
    
    struct Array;
    struct Boolean;
    struct Character;
    struct Enumeration;
    struct Number;
    struct String;
    struct Table;
     
     */
    
    /*
    struct HeapArray;
    struct HeapInt64;
    struct HeapNumber;
    struct HeapString;
    struct HeapTable;
     */

    
    /*
    struct Array {
        HeapArray* _array;
        operator Value() const { return reinterpret_cast<const Value&>(*this); };
        std::size_t size() const;
        Value operator[](std::size_t pos) const;
        Traced<Value>& operator[](std::size_t pos);
    };
    
    struct String {
        union {
            int _tag;
            const HeapString* _pointer;
            _short_string_t _string;
        };
        int _discriminant() const { return _tag & VALUE_MASK; }
        bool _is_pointer() const { return _discriminant() == TAG_POINTER; }
        bool _is_short_string() const { return _discriminant() == TAG_SHORT_STRING; }
        const HeapString* _as_pointer() const {
            assert(_is_pointer());
            return _pointer;
        }
        const HeapString* _as_pointer_else_nullptr() {
            return _is_pointer() ? _pointer : nullptr;
        }
        operator Value() const { return reinterpret_cast<const Value&>(*this); }
        std::string_view as_string_view() const;
        std::size_t size() const;
    };
    
    struct Table {
        const HeapTable* _pointer;
        Table();
        operator Value() const { return reinterpret_cast<const Value&>(*this); };
        Value operator[](Value key) const;
        Traced<Value>& operator[](Value key);
        bool contains(Value key) const;
        std::size_t size() const;
        Value find(Value key) const;
        Value erase(Value key);
        Value insert_or_assign(Value key, Value value);
    };
    
    */
    
    
    
    
    
    
    void trace(Value a);

    
    
    
    
    
   

    
    
    


    // will hold a container ref and a key, and resolve to something depending
    // on how it is used (as a get, a set, or a read-modity-write)
    
    // TODO: for binary operators, we have the multiple dispatch problem
    // - elegant solution?
    // - symmetric implementation?
    
    // We expose all C++ operators on Values, for convenience and familiarity,
    // though some are problematic with the GC syntax.  Note that there is
    // no particular requirement that "the game" or "the mod scripting
    // language" follow these same semantics.
    
    /*
    
    struct HeapValue : Object {
        
        // to reach this point the (lhs or unary) participaing Value must be
        // a heap-allocated object, but when the operation is mutating it need
        // not remain so; for example, LongInteger *= 0 will replace the Value
        // with an inline 0.
        
        // reflection
        
        const HeapInt64* as_HeapInt64() const;
        const HeapString* as_HeapString() const;
        const HeapArray* as_HeapArray() const;
        const HeapTable* as_HeapTable() const;

        // comparison
        
        std::partial_ordering three_way_comparison(Value) const;
        bool equality(Value) const;
        bool logical_not() const;

        // pure unary
        Value unary_plus() const;
        Value unary_minus() const;
        Value bitwise_not() const;
        
        // mutating unary
        Value postfix_increment(Value& self) const;
        Value postfix_decrement(Value& self) const;
        void prefix_increment(Value& self) const;
        void prefix_decrement(Value& self) const;
        
        // pure binary
        Value multiplication(Value) const;
        Value division(Value) const;
        Value remainder(Value) const;
        Value addition(Value) const;
        Value subtraction(Value) const;
        Value left_shift(Value) const;
        Value right_shift(Value) const;
        Value bitwise_and(Value) const;
        Value bitwise_xor(Value) const;
        Value bitwise_or(Value) const;
        
        // mutating binary
        
        // since numbers on the heap are immutable, there seems to be no
        // meaningful customization possible here; they will just have tp
        // do the basic operation and replace their handle with it
        void assigned_multiplication(Value&, Value) const;
        void assigned_division(Value&, Value) const;
        void assigned_remainder(Value&, Value) const ;
        void assigned_addition(Value&, Value) const;
        void assigned_subtraction(Value&, Value) const;
        void assigned_left_shift(Value&, Value) const;
        void assigned_right_shift(Value&, Value) const;
        void assigned_bitwise_and(Value&, Value) const;
        void assigned_bitwise_xor(Value&, Value) const;
        void assigned_bitwise_or(Value&, Value) const;

        // odd cases
        Value function_call() const;
        // Value subscript_const(Value other) const;
        // DeferredElementAccess subscript_mutable(Value& self, Value other);
        //virtual
        
        // built-in functions
        String str() const;
                
        // common interface
        std::size_t size() const;
        bool contains(Value) const;
        Value find(Value) const;
        Value erase(Value) const;
        Value insert_or_assign(Value, Value) const;
        
        explicit HeapValue(Class class_) : Object(class_) {}

    };
    
   */
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    // TODO: upgrade to array of limbs of arbitrary precision integer
    struct HeapInt64 : Object {
        std::int64_t _integer;
        explicit HeapInt64(std::int64_t z);
        std::int64_t as_int64_t() const;
    };
    
    struct HeapString : Object {
        std::size_t _hash;
        std::size_t _size;
        char _bytes[0];
        static void* operator new(std::size_t count, std::size_t extra);
        static HeapString* make(std::string_view v, std::size_t hash);
        static HeapString* make(std::string_view v);
        std::string_view as_string_view() const;
        HeapString();
    }; // struct HeapString
        
    
    
    
    
        
   
        
    
    
    // This is a std::vector-like object that is garbage collected
    // It is only "concurrent enough" for GC; it does not support access by
    // multiple mutators.
    //
    // Notably it is only amortized O(1), and has a worst case O(N).  As such
    // it is unsuitable for general use in soft real time contexts, but is
    // still useful for things that have some kind of moderate bounded size,
    // and as a stepping stone to more advanced data structures.
    

    
    
    
    struct DeferredElementAccess {
        Value& self;
        Value pos;
    };
    
    
    
    /*
    
    template<typename F>
    auto visit(Value value, F&& f) {
        switch (value._tag & Value::MASK) {
            case Value::POINTER: {
                const HeapValue* a = value._as_pointer();
                const HeapInt64* b = dynamic_cast<const HeapInt64*>(a);
                if (b) {
                    return f(b->_integer);
                }
                const HeapString* c = dynamic_cast<const HeapString*>(a);
                if (c) {
                    return f(c->as_string_view());
                }
                abort();
            }
            case Value::SMALL_INTEGER: {
                return f(value._as_small_integer());
            }
            case Value::SHORT_STRING: {
                return f(value._as_short_string());
            }
            case Value::BOOLEAN: {
                return f(value.as_boolean());
            }
            default:
                abort();
        }
    }
    
    */
    
    
    
    void foo();
   
    

    

    
    
    /*
     
     // There's no need for this type until we support non-int64 numbers
     
     struct Number {
     
     Value _value_that_is_a_number;
     
     operator Value() const { // upcast
     return _value_that_is_a_number;
     }
     
     std::int64_t as_int64_t() const {
     switch (_value_that_is_a_number._get_tag()) {
     case Value::POINTER: {
     const Object* a = _value_that_is_a_number._as_object();
     assert(dynamic_cast<const HeapInt64*>(a));
     const HeapInt64* b = static_cast<const HeapInt64*>(a);
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
     */
    
    
    
    
    
    inline void trace(Value a) {
        if (a._is_pointer())
            trace(a._as_pointer());
    }

    
    Value operator++(Value&, int);
    Value operator--(Value&, int);
    Value& operator++(Value&);
    Value& operator--(Value&);
    Value operator+(const Value&) ;
    Value operator-(const Value&) ;
    bool operator!(const Value&) ;
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
    
    
    
    inline void trace(const Traced<Value>& x) {
        trace(x._value.load(Order::ACQUIRE));
    }

    
   
    
    // user defined literals
    
    // String operator""_v(const char* s, std::size_t n);
    
    void shade(Value value);
    
    
    
    
    
    
    struct IndirectFixedCapacityValueArray : Object {
        
        std::size_t _capacity;
        Traced<Value>* _storage; // TODO: type?
        
        explicit IndirectFixedCapacityValueArray(std::size_t count)
        : Object(CLASS_INDIRECT_FIXED_CAPACITY_VALUE_ARRAY)
        , _capacity(count)
        , _storage((Traced<Value>*) calloc(count, sizeof(Traced<Value>))) {
            // printf("%p new IndirectFixedCapacityValueArray[%zd]\n", this, _capacity);
        }
        
    }; // struct IndirectFixedCapacityValueArray
    
    
    
    
    
    
    
    struct HeapTable : Object {
        
        // Based on a basic open-adressing linear-probing hash table
        //
        // Because of GC, the storage must be GC in its own right, and the
        // entries themselves must be atomic
        //
        // Because of real-time, we perform the resizes incrementally,
        // having an old and a new table, and moving one element over as
        // a tax on each insertion, until the old table is empty, and we
        // release it.
        //
        //
        // When the alpha table reaches its load limit, we allocate a new
        // beta table.  This will have enough capacity to hold twice the
        // elements in the alpha table; in erase-heavy workloads this may
        // actually be the same capacity or even smaller, since the old table
        // may be full of tombstones.  More precisely, the new table has to
        // have initial grace that is at least twice that of the old table
        // count.
        //
        // The old table has been drained up to some index.
        //
        // To insert_or_assign, we look for the entry in the old table.  If it
        // exists, we can replace it without disturbing any metrics.  The
        // drain level may mean we don't need to look.
        //
        // If it does not exist, we look for it in the new table.  If it exists,
        // we can replace it without disturbing any metrics.
        //
        // If it does not exist, we can insert into the new table.  If it
        // overwrites a tombstone, again, we have not made the situation worse.
        // and can terminate.
        //
        // If we have to consume a new element and reduce the grace of the new
        // table, we have to copy over any element of the old table to
        // preserve the invariant of beta_grace >= 2 * alpha_count
        
        // "Grace" is an integer.  For a new table, it is the load limit
        // times the capacity.  Grace is decreased whenever an empty slot is
        // consumed; note that erasure produces tombstones, so empty slots are
        // never recovered.  Thus, insertion may decrease grace, and other
        // operations do not affect it.  It is equivalent to tracking the
        // number of empty slots and comparing it to load limit.
        
        // The old table is, by definition, almost full, so it is not hard to
        // find an element to move over.  Since we write back tombstones as
        // part of a conventional erase, the edge case where the index loops
        // back to the beginning is handled.
        //
        // Interestingly, this strategy allocates new storage on the basis of
        // the current size, not the current capacity, so the table will
        // eventually forget occupancy spikes and right-size itself.  However:
        // When a table has a small count, relative to capacity, and zero grace,
        // a small new table will be allocated, and evacuation will have to
        // scan many elements to find an occupied one.  This is a pain point.
        // Evacuation should instead scan a fixed number of slots, and the
        // new table should be big enough to guarantee completion of that scan
        // before the next resize is needed.  This in turn places a limit on
        // how rapidly the table can shrink.
        
        struct Entry {
            Traced<Value> key;
            Traced<Value> value;
            
            [[nodiscard]] Value entomb() {
                this->key = Value::make_tombstone();
                // TODO: exchange
                Value old = this->value;
                this->value = Value::make_null();
                return old;
            }
            
        };
        
        struct InnerTable {
            
            Traced<const IndirectFixedCapacityValueArray*> _manager;
            Entry* _storage = nullptr;
            std::size_t _mask = 0;
            std::size_t _count = 0;
            std::size_t _grace = 0;
            
            void clear() {
                _manager = nullptr;
                _storage = nullptr;
                _mask = -1;
                _count = 0;
                _grace = 0;
            }
            
            
            std::size_t next(std::size_t i) {
                return (i + 1) & _mask;
            }
            
            std::size_t prev(std::size_t i) {
                return (i - 1) & _mask;
            }
            
            Entry* pfind(std::size_t h, Value k) {
                std::size_t i = h & _mask;
                for (;; i = next(i)) {
                    Entry* p = _storage + i;
                    Value ki = p->key;
                    
                    if (ki.is_null())
                        // Does not exist
                        return nullptr;
                    
                    // TODO: equals-with-one-known-hash
                    if (ki == k)
                        // Found
                        return p;
                    
                    // Another entry, or a tombstone
                    
                }
            }
            
            Value find(std::size_t h, Value k) {
                Entry* p = pfind(h, k);
                return p ? p->value : Value::make_null();
                
            }
            
            void clear_n_tombstones_before_i(std::size_t n, std::size_t i) {
                // grace sticks to zero
                if (!_grace)
                    return;
                {
                    Entry* pe = _storage + i;
                    [[maybe_unused]] Value ki = pe->key;
                    assert(ki.is_null());
                }
                while (n--) {
                    i = prev(i);
                    Entry* pe = _storage + i;
                    [[maybe_unused]] Value ki = pe->key;
                    assert(ki._is_tombstone());
                    pe->key = Value::make_null();
                    ++_grace;
                    // printf("disinterred one\n");
                }
            }
            
            Value erase_from(std::size_t h, Value k, std::size_t i) {
                std::size_t tombstones = 0;
                for (;; i = next(i)) {
                    Entry* pe = _storage + i;
                    Value ki = pe->key;
                    if (ki.is_null()) {
                        clear_n_tombstones_before_i(tombstones, i);
                        return Value::make_null();
                    }
                    if (ki == k) {
                        --_count;
                        return pe->entomb();
                    }
                    if (ki._is_tombstone()) {
                        ++tombstones;
                    } else {
                        tombstones = 0;
                    }
                    // a different key, or a tombstone
                }
            }
            
            Value erase(std::size_t h, Value k) {
                std::size_t i = h & _mask;
                return erase_from(h, k, i);
            }
            
            Value insert_or_assign(std::size_t h, Value k, Value v) {
                assert(_grace);
                std::size_t i = h & _mask;
                for (;; i = next(i)) {
                    Entry* pe = _storage + i;
                    Value ki = pe->key;
                    if (ki.is_null()) {
                        pe->key = k;
                        pe->value = v;
                        ++_count;
                        --_grace;
                        return Value::make_null();
                    }
                    if (ki._is_tombstone()) {
                        pe->key = k;
                        pe->value = v;
                        ++_count;
                        // we have installed the new key as early as possible
                        // but we must continue scanning and delete the old
                        // one if it exists
                        return erase_from(h, k, next(i));
                    }
                    if (ki == k) {
                        Value u = pe->value;
                        pe->value = v;
                        return u;
                    }
                    // a different key, or a tombstone
                }
            }
            
            Value try_assign(std::size_t h, Value k, Value v) {
                std::size_t i = h & _mask;
                for (;; i = ((i + 1) & _mask)) {
                    Entry* pe = _storage + i;
                    Value ki = pe->key;
                    if (ki.is_null()) {
                        return Value::make_null();
                    }
                    if (ki == k) {
                        Value u = pe->value;
                        pe->value = v;
                        return u;
                    }
                    // a different key, or a tombstone
                }
            }
            
            void must_insert(std::size_t h, Value k, Value v) {
                std::size_t i = h & _mask;
                for (;; i = ((i + 1) & _mask)) {
                    Entry* pe = _storage + i;
                    Value ki = pe->key;
                    if (ki.is_null()) {
                        pe->key = k;
                        pe->value = v;
                        ++_count;
                        --_grace;
                        return;
                    }
                    if (ki._is_tombstone()) {
                        pe->key = k;
                        pe->value = v;
                        ++_count;
                        // Check for the violation of the precondition, that the
                        // key is not later in the table
                        assert(erase_from(h, k, next(i)).is_null());
                        return;
                    }
                    // Check for the violation of the precondition, that the
                    // key was not already in the InnerTable
                    assert(ki != k);
                    // a different key was found, continue
                }
            }
            
            void _invariant() {
                if (!_storage)
                    return;
                // scan the whole thing
                std::size_t keys = 0;
                std::size_t nulls = 0;
                std::size_t tombstones = 0;
                for (std::size_t i = 0; i != _mask + 1; ++i) {
                    Entry* pe = _storage + i;
                    Value ki = pe->key;
                    Value vi = pe->value;
                    assert(!vi._is_tombstone());
                    if (ki.is_null()) {
                        ++nulls;
                        assert(vi.is_null());
                    } else if (ki._is_tombstone()) {
                        ++tombstones;
                        assert(vi.is_null());
                    } else {
                        ++keys;
                        assert(!vi.is_null());
                    }
                }
                assert(keys + nulls + tombstones == _mask + 1);
                if (keys != _count) {
                    for (std::size_t i = 0; i != _mask + 1; ++i) {
                        Entry* pe = _storage + i;
                        Value ki = pe->key;
                        printf("[%zd] = { %llx, ...}\n", i, ki._data);
                    }
                }
                assert(keys == _count);
            }
            
        };
        
        mutable InnerTable _alpha;
        mutable InnerTable _beta;
        mutable std::size_t _partition = 0;
        
        
        
        
        
        
        void _invariant() const {
            _alpha._invariant();
            _beta._invariant();
            /*
             if (_beta._storage) {
             for (int i = 0; i != _alpha._mask + 1; ++i) {
             Entry* pe = _alpha._storage + i;
             Value ki = pe->key;
             if (ki.is_null() || ki._is_tombstone())
             continue;
             Value vj = _beta.find(ki.hash(), ki);
             assert(vj.is_null());
             }
             for (int i = 0; i != _beta._mask + 1; ++i) {
             Entry* pe = _beta._storage + i;
             Value ki = pe->key;
             if (ki.is_null() || ki._is_tombstone())
             continue;
             Value vj = _alpha.find(ki.hash(), ki);
             assert(vj.is_null());
             }
             }
             */
        }
        
        
        
        Value find(Value key) const {
            std::size_t h = key.hash();
            if (_alpha._count) {
                Value v = _alpha.find(h, key);
                if (!v.is_null())
                    return v;
            }
            if (_beta._count) {
                Value v = _beta.find(h, key);
                if (!v.is_null())
                    return v;
            }
            return Value::make_null();
        }
        
        Value erase(Value key) const {
            //_invariant();
            std::size_t h = key.hash();
            if (_alpha._count) {
                Value v = _alpha.erase(h, key);
                if (!v.is_null()) {
                    return v;
                }
            }
            if (_beta._count) {
                Value v = _beta.erase(h, key);
                if (!v.is_null()) {
                    return v;
                }
            }
            return Value::make_null();
            
        }
        
        Value insert_or_assign(Value key, Value value) const {
            // _invariant();
            // printf("insert_or_assign (%lld, %lld)\n", key._integer >> 4, value._integer >> 4);
            std::size_t h = key.hash();
            // printf("with hash %zd\n", h);
            
            // printf("with alpha %zd/%zd, beta %zd/%zd\n", _alpha._count, _alpha._grace, _beta._count, _beta._grace);
            
            if (_alpha._grace) {
                // _partition = 0;
                Value x = _alpha.insert_or_assign(h, key, value);
                // _invariant();
                return x;
            }
            // _alpha is terminal
            
            {
                for (std::size_t i = 0; i != _partition; ++i) {
                    Entry* pe = _alpha._storage + i;
                    Value ki = pe->key;
                    assert(ki.is_null() || ki._is_tombstone());
                }
            }
            
            
            if (_alpha._count) {
                // _alpha is not yet empty
                Value u = _alpha.try_assign(h, key, value);
                // _invariant();
                if (!u.is_null())
                    return u;
                // we have proved that key is not in alpha
            } else {
                // _alpha is terminal and empty, discard it
                if (_beta._storage) {
                    // swap in _beta
                    _alpha = _beta;
                    _beta._manager = nullptr;
                    _beta._storage = nullptr;
                    _beta._count = 0;
                    _beta._grace = 0;
                    _beta._mask = 0;
                    _partition = 0;
                } else {
                    assert(_alpha._storage == nullptr);
                    _alpha._manager = new IndirectFixedCapacityValueArray(8);
                    _alpha._storage = (Entry*) _alpha._manager->_storage;
                    _alpha._mask = 3;
                    _alpha._grace = 3;
                    _alpha._count = 0;
                    _partition = 0;
                }
                return insert_or_assign(key, value);
            }
            if (!_beta._grace) {
                assert(_beta._storage == nullptr);
                using wry::type_name;
                std::size_t new_capacity = std::bit_ceil((_alpha._count * 8 + 2) / 3);
                std::size_t new_grace = new_capacity * 3 / 4;
                // printf("New table of capacity %zd when old was %zd\n", new_capacity, _alpha._mask + 1);
                _beta._manager = new IndirectFixedCapacityValueArray(new_capacity * 2);
                _beta._storage = (Entry*) _beta._manager->_storage;
                _beta._count = 0;
                _beta._grace = new_grace;
                _beta._mask = new_capacity - 1;
            }
            // _invariant();
            Value ultimate = _beta.insert_or_assign(h, key, value);
            // _invariant();
            while (_alpha._count) {
                Entry* pe = _alpha._storage + (_partition++);
                Value ki = pe->key;
                if (ki.is_null() || ki._is_tombstone())
                    continue;
                // _invariant();
                Value vi = pe->entomb();
                _alpha._count--;
                // _invariant();
                assert(_beta._grace);
                _beta.must_insert(ki.hash(), ki, vi);
                // _invariant();
                // printf("Evacuated (%lld, %lld)\n", ki._integer >> 4, vi._integer >> 4);
                break;
            }
            return ultimate;
        }
        
        std::size_t size() const {
            return _alpha._count + _beta._count;
        }
        
        bool contains(Value key) const {
            return !find(key).is_null();
        }
        
        Traced<Value>& find_or_insert_null(Value key) const {
            std::size_t h = key.hash();
            if (_alpha._count) {
                Entry* p = _alpha.pfind(h, key);
                if (p)
                    return p->value;
            }
            if (_alpha._grace) {
                
            }
            abort();
        }
        
        void clear() {
            _alpha.clear();
            _beta.clear();
            _partition = 0;
        }
        
        HeapTable() : Object(CLASS_TABLE) {}
        
    }; // struct HeapTable
    
    
    
    
    enum _tag_t {
        TAG_POINTER = 0,
        TAG_SMALL_INTEGER = 1,
        TAG_SHORT_STRING = 3,
        TAG_BOOLEAN = 4,
        TAG_ENUMERATION = 5,
        TAG_ERROR = 6,
        TAG_CHARACTER = 7,
        TAG_TOMBSTONE = 15,
    };
    
    enum {
        VALUE_SHIFT = 4,
        VALUE_MASK = 15,
    };
    
    
    
    struct _boolean_t {
        int _tag;
        bool boolean;
    };
    
    struct _character_t {
        int _tag;
        int32_t _character; // UTF-32
    };
    
    struct _short_string_t {
        char _tag_and_len;
        char _chars[7];
        char* data() { return _chars; }
        constexpr std::size_t size() const {
            assert((_tag_and_len & VALUE_MASK) == TAG_SHORT_STRING);
            return _tag_and_len >> VALUE_SHIFT;
        }
        constexpr std::string_view as_string_view() const {
            return std::string_view(_chars, size());
        }
        std::size_t hash() const {
            return std::hash<std::string_view>()(as_string_view());
        }
    };
    
    enum : uint64_t {
        VALUE_NULL = 0,
        VALUE_ZERO = TAG_SMALL_INTEGER,
        VALUE_EMPTY_STRING = TAG_SHORT_STRING,
        VALUE_FALSE = TAG_BOOLEAN,
        VALUE_TRUE = TAG_BOOLEAN | (1l << 32),
        VALUE_ERROR = TAG_ERROR,
        VALUE_TOMBSTONE = TAG_TOMBSTONE,
    };
    
    
    constexpr Value::Value(std::nullptr_t) : _data(0) {}
    constexpr Value::Value(bool flag) : _data(((uint64_t)flag << 32) | TAG_BOOLEAN) {}
    inline Value::Value(const char* ntbs) { *this = Value::from_ntbs(ntbs); }
    constexpr Value::Value(int i) : _data(((int64_t)i << VALUE_SHIFT) | TAG_SMALL_INTEGER) {}

    
       
    
    
    
    
    
    template<std::size_t N, typename>
    constexpr Value::Value(const char (&ntbs)[N]) {
        const std::size_t M = N - 1;
        assert(ntbs[M] == '\0');
        if (M < 8) {
            _short_string_t s;
            s._tag_and_len = (M << VALUE_SHIFT) | TAG_SHORT_STRING;
            // builtin for constexpr
            __builtin_memcpy(s._chars, ntbs, M);
            __builtin_memcpy(&_data, &s, 8);
        } else {
            _data = (uint64_t)HeapString::make(ntbs);
        }
    }
    
    
    
} // namespace gc

#endif /* value_hpp */
