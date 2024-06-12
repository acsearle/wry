//
//  value.cpp
//  client
//
//  Created by Antony Searle on 31/5/2024.
//

#include <algorithm>
#include <bit>
#include <numeric>
#include <random>

#include "../client/hash.hpp"
#include "debug.hpp"
#include "value.hpp"

// To support basic math operators we need
// + -> arbitrary precision
// - -> signed
// / -> rational
//
// a more conventional solution is to use floating point, but this is
// problematic for getting consistent results across clients, and generally
// a can of worms
//
// other choices:
// hard limit at some human-friendly decimal value like 1k, 1M?
// expose hardware modulo 2^n?
// expose intger division?
// fixed point?
//
// I don't really want to expose arbitrary math on approximate real numbers,
// like sqrt, sin etc.


// Pointer to a garbage-collected heap representation, or an inline integer
// representation.  Uses invalid (misaligned) pointer values to distinguish
// multiple cases; we can tag 15 kinds of not-pointer with the 4 lsbs, with
// 0b...0000 being a valid aligned address.
//
// Some kinds of Value are exclusively represented on the heap (mutable
// containers) or exclusively on the stack (bool, enumerations), and
// others may switch (immutable containers such as strings and arbitrary
// precision integers keep small, and presumed common, values inline)

// TODO: Is this a Value... or a Variable?
// "strings are immutable", but V*** can change which immutable string they
// reference.  And indeed, the gc::String object can change what it points
// to.  Call these things Variables instead?


namespace gc {
            
    std::string_view String::as_string_view() const {
        switch (_discriminant()) {
            case Value::POINTER: {
                return _pointer->as_string_view();
            }
            case Value::SHORT_STRING: {
                return _string.as_string_view();
            }
            default:
                abort();
        }
    }
    

    Value& operator++(Value& self) {
        switch (self._discriminant()) {
            case Value::POINTER: {
                self._as_pointer()->prefix_increment(self);
                break;
            }
            case Value::SMALL_INTEGER: {
                self = Value::from_int64(self._as_small_integer() + 1);
                break;
            }
            default:
                self = Value::make_error();
                break;
        }
        return self;
    }
    
    Value operator++(Value& self, int) {
        Value old = self;
        switch (self._discriminant()) {
            case Value::POINTER: {
                self._as_pointer()->postfix_increment(self);
                break;
            }
            case Value::SMALL_INTEGER: {
                self = Value::from_int64(self._as_small_integer() + 1);
                break;
            }
            default: {
                self = Value::make_error();
                break;
            }
        }
        return old;
    }
    
    Value& operator+=(Value& self, const Value& other) {
        switch (self._discriminant()) {
            case Value::POINTER: {
                self._as_pointer()->assigned_addition(self, other);
                break;
            }
            case Value::SMALL_INTEGER: {
                if (other._discriminant() == Value::SMALL_INTEGER) {
                    self = Value::from_int64(self._as_small_integer() + other._as_small_integer());
                } else {
                    // int32 + ??? -> need more functions
                    abort();
                }
                break;
            }
            default: {
                self = Value::make_error();
                break;
            }
        }
        return self;
    }
    
    Value operator+(const Value& self, const Value& other) {
        switch (self._discriminant()) {
            case Value::POINTER: {
                return self._as_pointer()->addition(other);
            }
            case Value::SMALL_INTEGER: {
                if (other._discriminant() == Value::SMALL_INTEGER) {
                    return Value::from_int64(self._as_small_integer()
                                             + other._as_small_integer());
                } else {
                    // int32 + ??? -> need more functions
                    abort();
                }
            }
            default: {
                return Value::make_error();
            }
        }
    }
    
    Value operator*(const Value&, const Value&) { abort(); }
    Value operator/(const Value&, const Value&) { abort(); }
    Value operator%(const Value&, const Value&) { abort(); }
    Value operator-(const Value&, const Value&) { abort(); }
    Value operator<<(const Value&, const Value&) { abort(); }
    Value operator>>(const Value&, const Value&) { abort(); }
    Value operator&(const Value&, const Value&) { abort(); }
    Value operator^(const Value&, const Value&) { abort(); }
    Value operator|(const Value&, const Value&) { abort(); }
    
    Value& operator*=(Value&, const Value&) { abort(); }
    Value& operator/=(Value&, const Value&) { abort(); }
    Value& operator%=( Value&, const Value&) { abort(); }
    Value& operator-=( Value&, const Value&) { abort(); }
    Value& operator&=( Value&, const Value&) { abort(); }
    Value& operator^=( Value&, const Value&) { abort(); }
    Value& operator|=( Value&, const Value&) { abort(); }
    Value& operator<<=( Value&, const Value&) { abort(); }
    Value& operator>>=( Value&, const Value&) { abort(); }
    
    Value& operator--(Value&) { abort(); }
    Value operator--(Value&,int) { abort(); }
    
    
    
    
    
    std::size_t Value::hash() const {
        switch (_discriminant()) {
            case POINTER: {
                const HeapValue* a = _as_pointer();
                return a ? a->gc_hash() : 0;
            }
            case SMALL_INTEGER: {
                std::int64_t a = _as_small_integer();
                // std::hash<std::int64_t> is trivial (on libc++)
                // return std::hash<std::int64_t>()(a);
                return wry::hash(a);
            }
            case SHORT_STRING: {
                return std::hash<std::string_view>()(_as_short_string());
            }
            case BOOLEAN: {
                return std::hash<bool>()(as_boolean());
            }
            default:
                abort();
        }
    }
    
    Value Value::from_int64(std::int64_t z) {
        Value result;
        std::int64_t y = z << 4;
        if ((y >> 4) == z) {
            result._integer = y | SMALL_INTEGER;
        } else {
            HeapInt64* a = new HeapInt64(z);
            result._pointer = a;
        }
        return result;
    }
    
    Value Value::from_ntbs(const char* ntbs) {
        Value result;
        std::size_t n = std::strlen(ntbs);
        if (n < 8) {
            result._enumeration = (n << 4) | SHORT_STRING;
            std::memcpy(result._short_string._chars, ntbs, n);
            assert(result._is_short_string());
        } else {
            result._pointer = HeapString::make(std::string_view(ntbs, n));
            assert(result._is_pointer());
        }
        return result;
    }
    
    Value Value::_from_object(const HeapValue* object) {
        Value result;
        result._pointer = object;
        assert(result._is_pointer());
        return result;
    }
    
    Value Value::from_bool(bool flag) {
        Value result;
        result._tag = BOOLEAN;
        result._boolean.boolean = flag;
        assert(result.is_boolean());
        return result;
    }
        
    
    
    
    
    
    
    
    bool HeapValue::logical_not() const {
        abort();
    }
    
    std::partial_ordering HeapValue::three_way_comparison(Value other) const {
        abort();
    }
    
    bool HeapValue::equality(Value) const {
        abort();
    }
    
    
    Value HeapValue::multiplication(Value) const {
        return Value::make_error();
    }

    Value HeapValue::division(Value) const {
        return Value::make_error();
    }

    Value HeapValue::remainder(Value) const {
        return Value::make_error();
    }
    
    Value HeapValue::addition(Value) const {
        return Value::make_error();
    }

    Value HeapValue::subtraction(Value) const {
        return Value::make_error();
    }

    Value HeapValue::bitwise_and(Value) const {
        return Value::make_error();
    }
    
    Value HeapValue::bitwise_or(Value) const {
        return Value::make_error();
    }
    
    Value HeapValue::bitwise_xor(Value) const {
        return Value::make_error();
    }
    
    Value HeapValue::function_call() const {
        return Value::make_error();
    }
    
    Value HeapValue::left_shift(Value) const {
        return Value::make_error();
    }
    
    Value HeapValue::right_shift(Value) const {
        return Value::make_error();
    }
    
    Value HeapValue::unary_plus() const {
        return Value::make_error();
    }
    
    Value HeapValue::unary_minus() const {
        return Value::make_error();
    }
    
    Value HeapValue::bitwise_not() const {
        return Value::make_error();
    }
    
    
    
    
    
    void HeapValue::prefix_increment(Value& self) const {
        self += Value::from_int64(1);
    }

    void HeapValue::prefix_decrement(Value& self) const {
        self -= Value::from_int64(1);
    }

    Value HeapValue::postfix_increment(Value& self) const {
        Value old;
        ++self;
        return old;
    }
    
    Value HeapValue::postfix_decrement(Value& self) const {
        Value old;
        --self;
        return old;
    }
    
    
    
    void HeapValue::assigned_addition(Value& self, Value other) const {
        self = self + other;
    }

    void HeapValue::assigned_subtraction(Value& self, Value other) const {
        self = self - other;
    }

    void HeapValue::assigned_multiplication(Value& self, Value other) const {
        self = self * other;
    }

    void HeapValue::assigned_division(Value& self, Value other) const {
        self = self / other;
    }
    
    void HeapValue::assigned_remainder(Value& self, Value other) const {
        self = self % other;
    }
    
    void HeapValue::assigned_bitwise_and(Value& self, Value other) const {
        self = self & other;
    }
    
    void HeapValue::assigned_bitwise_xor(Value& self, Value other) const {
        self = self ^ other;
    }
    
    void HeapValue::assigned_bitwise_or(Value& self, Value other) const {
        self = self | other;
    }
    
    void HeapValue::assigned_left_shift(Value& self, Value other) const {
        self = self << other;
    }

    void HeapValue::assigned_right_shift(Value& self, Value other) const {
        self = self >> other;
    }

    const HeapArray* HeapValue::as_HeapArray() const {
        return nullptr;
    }
    
    const HeapInt64* HeapValue::as_HeapInt64() const {
        return nullptr;
    }
    
    const HeapString* HeapValue::as_HeapString() const {
        return nullptr;
    }
    
    const HeapTable* HeapValue::as_HeapTable() const {
        return nullptr;
    }

    
    
    String HeapValue::str() const {
        Value a = Value::from_ntbs("HeapValue");
        String b;
        b._string = a._short_string;
        return b;
    };
    
    
    
    
    
    
    // This strange object exists so that we can have buffers of a power of two
    // number of pointers that use a power of two allocation, without having
    // their first words taken by headers, or the headers making the allocation
    // slightly more than a power of two (!)
    //
    // The size and identity of the buffer are immutable, but the buffer's
    // contents are mutable, atomic and should be subject to the write barrier
    //
    // The buffer is full of Values, but we only care if they can be
    // interpreted as Object*.
    
    //
    // This object is not needed for resizable arrays of not-potential-gc-
    // pointers, which do not need to be scanned at all and are just held and
    // managed directly perhaps in a std::vector, or small arrays of fixed
    // size, which can use the fma pattern
    struct IndirectFixedCapacityValueArray : Object {
        
        std::size_t _capacity;
        Traced<Value>* _storage; // TODO: type?
        
        explicit IndirectFixedCapacityValueArray(std::size_t count)
        : _capacity(count)
        , _storage((Traced<Value>*) calloc(count, sizeof(Traced<Value>))) {
            // printf("%p new IndirectFixedCapacityValueArray[%zd]\n", this, _capacity);
        }
        
        virtual ~IndirectFixedCapacityValueArray() override {
            free(_storage);
            // printf("%p del IndirectFixedCapacityValueArray[%zd]\n", this, _capacity);
        }
        
        virtual std::size_t gc_bytes() const override {
            return sizeof(IndirectFixedCapacityValueArray) + _capacity * sizeof(Traced<Value>);
        }
        
        virtual void gc_enumerate() const override {
            auto first = _storage;
            auto last = first + _capacity;
            for (; first != last; ++first) {
                trace(*first);
            }
        }
        
    }; // struct IndirectFixedCapacityValueArray
    
    
    struct HeapArray : HeapValue {
        
        mutable std::size_t _size;
        mutable std::size_t _capacity;
        mutable Traced<Value>* _storage; // TODO: type?
        mutable Traced<const IndirectFixedCapacityValueArray*> _storage_manager;
        
        HeapArray()
        : _size(0)
        , _capacity(0)
        , _storage(0)
        , _storage_manager(nullptr) {
            // printf("%p new Value[]\n", this);
        }
        
        virtual ~HeapArray() override {
            // printf("%p del Value[%zd]\n", this, _size);
        }
        
        virtual std::size_t gc_bytes() const override {
            return sizeof(HeapArray);
        }
        
        virtual void gc_enumerate() const override {
            trace(_storage_manager);
        }
        
        void push_back(Value value) const {
            if (_size == _capacity) {
                auto a = std::max<std::size_t>(16, _capacity * 2);
                auto b = new IndirectFixedCapacityValueArray(a);
                auto c = b->_storage;
                std::memcpy(c, _storage, _size * 8);
                _capacity = a;
                _storage = b->_storage;
                _storage_manager = b;
            }
            _storage[_size++] = value;
        }
        
        bool empty() const {
            return !_size;
        }
        
        void pop_back() const {
            assert(_size);
            _storage[--_size] = Value::make_null();
        }
        
        std::size_t size() const override {
            return _size;
        }
        
        virtual bool contains(Value key) const override {
            if (key._is_small_integer()) {
                auto pos = key._as_small_integer();
                if (0 <= pos && pos < _size)
                    return true;
            }
            return false;
        }
        
        virtual Value find(Value key) const override {
            if (key._is_small_integer()) {
                auto pos = key._as_small_integer();
                if (0 <= pos && pos < _size)
                    return _storage[pos];
            }
            return Value::make_null();
        }
        
    };
    
    


    
    
    
    
    struct HeapTable : HeapValue {
        
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
                        printf("[%zd] = { %llx, ...}\n", i, ki._enumeration);
                    }
                }
                assert(keys == _count);
            }
                        
        };
        
        mutable InnerTable _alpha;
        mutable InnerTable _beta;
        mutable std::size_t _partition = 0;
        
        
        virtual ~HeapTable() override {
            // printf("%p del HeapTable[%zd]\n", this, _alpha._count + _beta._count);
        }
        
        virtual std::size_t gc_bytes() const override {
            return sizeof(HeapTable);
        }
        
        virtual void gc_enumerate() const override {
            trace(_alpha._manager);
            trace(_beta._manager);
        }

        
        
        
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
        
        
        
        Value find(Value key) const override {
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
        
        Value erase(Value key) const override {
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
        
        Value insert_or_assign(Value key, Value value) const override {
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
        
        std::size_t size() const override {
            return _alpha._count + _beta._count;
        }
        
        bool contains(Value key) const override {
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
        
    };
    
   
    
    std::size_t Table::size() const {
        return _pointer->size();
    }
    
    bool Table::contains(Value key) const {
        return _pointer->contains(key);
    }
    
    Value Table::find(Value key) const {
        return _pointer->find(key);
    }
    
    Value Table::erase(Value key) {
        return _pointer->erase(key);
    }
    
    Value Table::insert_or_assign(Value key, Value value) {
        return _pointer->insert_or_assign(key, value);
    }

    
    Value Table::operator[](Value key) const {
        return _pointer->find(key);
    }
    
    Traced<Value>& Table::operator[](Value key) {
        return _pointer->find_or_insert_null(key);
        
    }


    
      
    void foo() {
        
        Table t;
        
        assert(t.size() == 0);
        assert(!t.contains("a"));
        assert(t.find("a") == Value::make_null());
        t.insert_or_assign("a", "A");
        assert(t.size() == 1);
        assert(t.contains("a"));
        assert(t.find("a") == "A");
        assert(t.insert_or_assign("a", "A2") == "A");
        assert(t.size() == 1);
        assert(t.contains("a"));
        assert(t.find("a") == "A2");
        t.erase("a");
        assert(t.size() == 0);
        assert(!t.contains("a"));
        assert(t.find("a") == Value::make_null());
        
        
        std::vector<int> v(100);
        std::iota(v.begin(), v.end(), 0);
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(v.begin(), v.end(), g);
        
        for (int i = 0; i != 100; ++i) {
            assert(t.size() == i);
            assert(!t.contains(v[i]));
            assert(t.find(v[i]).is_null());
            assert(t.insert_or_assign(v[i], v[i]).is_null());
            assert(t.size() == i + 1);
            assert(t.contains(v[i]));
            assert(t.find(v[i]) == v[i]);
        }
        
        std::shuffle(v.begin(), v.end(), g);
        for (int i = 0; i != 100; ++i) {
            assert(t.contains(v[i]));
            assert(t.find(v[i]) == v[i]);
            assert(!t.contains(v[i] + 100));
            assert(t.find(v[i] + 100).is_null());
        }

        for (int i = 0; i != 100; ++i) {
            assert(t.contains(v[i]));
            assert(t.find(v[i]) == v[i]);
            assert(t.erase(v[i])== v[i]);
            assert(t.contains(v[i]) == false);
            assert(t.find(v[i]).is_null());
        }
        
        assert(t.size() == 0);

        std::shuffle(v.begin(), v.end(), g);
        
        Value s = t;
        for (int i = 0; i != 100; ++i) {
            assert(s.size() == i);
            assert(!s.contains(v[i]));
            assert(s[v[i]] == Value::make_null());
            // t._pointer->_invariant();
            s[v[i]] = v[i];
            // t._pointer->_invariant();
            assert(t.size() == i + 1);
            assert(t.contains(v[i]));
            assert(s[v[i]] == v[i]);
        }



        /*
        
        // the heap-allocated objects will live until the next handshake so
        // they will live beyond the end of this function even without being
        // marked-as-roots anywhere
        
        Value a = Value::from_ntbs("hello"); // short string
        Value b = Value::from_ntbs("long kiss goodbye"); // long string
        
        assert(a._is_string()); // packed into value
        assert(b._is_pointer()); // on the heap
        
        // hack type interrogation
        String c; c._string = a._string;
        String d; d._string = b._string;
        
        auto e = c.as_string_view();
        printf("%.*s\n", (int)e.size(), e.data());
        auto f = d.as_string_view();
        printf("%.*s\n", (int)f.size(), f.data());
        
        Value z = Value::from_int64(-7);
        Value y = Value::from_int64(-777777777777777);
        
        //Number x; x._value_that_is_a_number = z;
        //Number w; w._value_that_is_a_number = y;
        
        //printf("%" PRId64 "\n", x.as_int64_t());
        //printf("%" PRId64 "\n", w.as_int64_t());
        
        auto m = [](auto x) {
            printf("visited with %s\n", __PRETTY_FUNCTION__);
        };
        
        visit(a, m);
        visit(b, m);
        visit(z, m);
        visit(y, m);
        
        auto v = new HeapArray();
        v->push_back(a);
        v->push_back(b);
        v->push_back(c);
        v->push_back(d);
        v->push_back(z);
        v->push_back(y);
        //v->push_back(w);
        //v->push_back(x);
        
         */
        
        
    }
     
    
    
    
    
    Value Traced<Value>::get() const {
        return _value.load(Order::RELAXED);
    }
        
    Traced<Value>::Traced(const Value& value) 
    : _value(value) {
    }
    
    Traced<Value>::Traced(const Traced<Value>& value)
    : Traced(value.get()) {
    }
    
    void shade(Value a, Value b) {
        shade(a);
        shade(b);
    }
    

    Traced<Value>& Traced<Value>::operator=(const Value& desired) {
        Value discovered = this->_value.exchange(desired, Order::RELEASE);
        shade(desired, discovered);
        return *this;
    }

    Traced<Value>& Traced<Value>::operator=(const Traced<Value>& desired) {
        return this->operator=(desired.get());
    }
    
    Traced<Value>::operator bool() const {
        return get().operator bool();
    }
    
    Traced<Value>::operator Value() const {
        return get();
    }
    
    Value::operator bool() const {
        // POINTER: nonnull
        //    - All containers are true, even if empty
        // INTEGER: nonzero
        // STRING: nonempty
        // ENUMERATION: nonzero
        // BOOLEAN: nonzero
        // ERROR: always false
        // TOMBSTONE: always false
        return _enumeration >> 4;
    }
    
    bool operator==(const Value& a, const Value& b) {
        // POINTER: identity; requires interned bigstrings, bignums etc.
        //    - Containers are by identity
        //    - Identity of empty containers?
        // INLINE: requires that we make padding bits consistent
        return a._enumeration == b._enumeration;
    }
    
    
    std::size_t Array::size() const {
        return _array->_size;
    }
    
    Value Array::operator[](std::size_t pos) const {
        assert(pos < _array->_size);
        return _array->_storage[pos].get();
    }

    
    
    
    std::size_t String::size() const {
        switch (_discriminant()) {
            case Value::POINTER:
                assert(_pointer);
                return _pointer->size();
            case Value::SHORT_STRING:
                return (_tag >> 4) & 15;
            default:
                abort();
        }
    }

    bool HeapValue::contains(Value) const {
        return false;
    }

    Value HeapValue::find(Value) const {
        return Value::make_error();
    }

    Value HeapValue::insert_or_assign(Value, Value) const {
        return Value::make_error();
    }

    Value HeapValue::erase(Value) const {
        return Value::make_error();
    }

    std::size_t HeapValue::size() const {
        return 0;
    }

    
    Table::Table() : _pointer(new HeapTable) {}

    
    std::size_t Value::size() const {
        switch (_discriminant()) {
            case POINTER:
                return _pointer ? _pointer->size() : 0;
            case SHORT_STRING:
                return _short_string.size();
            default:
                return 0;
        }
    }
    
    bool Value::contains(Value key) const {
        switch (_discriminant()) {
            case POINTER:
                return _pointer && _pointer->contains(key);
            default:
                return false;
        }
    }
    
    
    _deferred_subscript_t Value::operator[](Value key) {
        return {*this, key};
    }
    
    
    void shade(Value value) {
        if (value._is_pointer()) {
            shade(value._as_pointer());
        }
    }
    
    
    
    _deferred_subscript_t::operator Value() && {
        return container._pointer->find(key);
    }
    
    _deferred_subscript_t&& _deferred_subscript_t::operator=(Value desired) && {
        container._pointer->insert_or_assign(key, desired);
        return std::move(*this);
    }
    
    _deferred_subscript_t&& _deferred_subscript_t::operator=(_deferred_subscript_t&& desired) && {
        return std::move(*this).operator=((Value)std::move(desired));
    }

    
    constexpr Value Value::make_error() { Value result; result._enumeration = ERROR; return result; }
    constexpr Value Value::make_null() { Value result; result._enumeration = 0; return result; }
    constexpr Value Value::make_tombstone() { Value result; result._enumeration = TOMBSTONE; return result; }

    
    
    
    void HeapLeaf::_gc_shade() const {
        _gc_shade_for_leaf(&this->_gc_color);
    }
    void HeapLeaf::gc_enumerate() const {
        // no children
    }

    
    
    void* HeapString::operator new(std::size_t count, std::size_t extra) {
        return allocate(count + extra);
    }
    
    HeapString* HeapString::make(std::string_view v,
                            std::size_t hash) {
        HeapString* p = new(v.size()) HeapString;
        p->_hash = hash;
        p->_size = v.size();
        std::memcpy(p->_bytes, v.data(), v.size());
        //            printf("%p new \"%.*s\"%s\n",
        //                   p,
        //                   std::min((int)p->_size, 48), p->_bytes,
        //                   ((p->_size > 48) ? "..." : ""));
        return p;
    }
    
    HeapString* HeapString::make(std::string_view v) {
        return make(v, std::hash<std::string_view>()(v));
    }
    
    HeapString::~HeapString() {
        // printf("%p del \"%.*s\"\n", this, (int)_size, _bytes);
    }

    std::size_t HeapString::gc_bytes() const {
        return sizeof(HeapString) + _size;
    }
    
    std::size_t HeapString::gc_hash() const  {
        return _hash;
    }
    
    std::string_view HeapString::as_string_view() const {
        return std::string_view(_bytes, _size);
    }
    
    
    
    
    HeapInt64::~HeapInt64() {
        // printf("%p del %" PRId64 "\n", this, _integer);
    }
    
    HeapInt64::HeapInt64(std::int64_t z)
    : _integer(z) {
        // printf("%p new %" PRId64 "\n", this, _integer);
    }

    std::size_t HeapInt64::gc_hash() const {
        return std::hash<std::int64_t>()(_integer);
    }
    std::size_t HeapInt64::gc_bytes() const {
        return sizeof(HeapInt64);
    }
    
    std::int64_t HeapInt64::as_int64_t() const {
        return _integer;
    }
        
} // namespace gc
