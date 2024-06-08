//
//  value.cpp
//  client
//
//  Created by Antony Searle on 31/5/2024.
//

#include <bit>

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
        switch (_masked_tag()) {
            case Value::POINTER: {
                return _pointer->as_string_view();
            }
            case Value::STRING: {
                return _string.as_string_view();
            }
            default:
                abort();
        }
    }
    

    Value& operator++(Value& self) {
        switch (self._masked_tag()) {
            case Value::POINTER: {
                self._as_pointer()->prefix_increment(self);
                break;
            }
            case Value::INTEGER: {
                self = Value::from_int64(self._as_integer() + 1);
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
        switch (self._masked_tag()) {
            case Value::POINTER: {
                self._as_pointer()->postfix_increment(self);
                break;
            }
            case Value::INTEGER: {
                self = Value::from_int64(self._as_integer() + 1);
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
        switch (self._masked_tag()) {
            case Value::POINTER: {
                self._as_pointer()->assigned_addition(self, other);
                break;
            }
            case Value::INTEGER: {
                if (other._masked_tag() == Value::INTEGER) {
                    self = Value::from_int64(self._as_integer() + other._as_integer());
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
        switch (self._masked_tag()) {
            case Value::POINTER: {
                return self._as_pointer()->addition(other);
            }
            case Value::INTEGER: {
                if (other._masked_tag() == Value::INTEGER) {
                    return Value::from_int64(self._as_integer()
                                             + other._as_integer());
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
        switch (_masked_tag()) {
            case POINTER: {
                const HeapValue* a = _as_pointer();
                return a ? a->gc_hash() : 0;
            }
            case INTEGER: {
                std::int64_t a = _as_integer();
                // std::hash<std::int64_t> is trivial (on libc++)
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
    
    Value Value::from_int64(std::int64_t z) {
        Value result;
        std::int64_t y = z << 4;
        if ((y >> 4) == z) {
            result._integer = y | INTEGER;
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
            result._enumeration = (n << 4) | STRING;
            std::memcpy(result._string._chars, ntbs, n);
            assert(result._is_string());
        } else {
            std::size_t hash = std::hash<std::string_view>()(std::string_view(ntbs, n));
            result._pointer = HeapString::make(ntbs, ntbs + n, hash);
            assert(result._is_pointer());
        }
        return result;
    }
    
    Value Value::from_object(const HeapValue* object) {
        Value result;
        result._pointer = object;
        assert(result._is_pointer());
        return result;
    }
    
    Value Value::from_boolean(bool flag) {
        Value result;
        result._tag = BOOLEAN;
        result._boolean.boolean = flag;
        assert(result._is_boolean());
        return result;
    }
    
    Value Value::make_error() {
        __builtin_trap();
        Value result;
        result._tag = ERROR;
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
    
    Value HeapValue::subscript_const(Value) const {
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

    
    
    DeferredElementAccess HeapValue::subscript_mutable(Value& self, Value pos) {
        return {self, pos};
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
    

    
    
    String HeapValue::str() const {
        Value a = Value::from_ntbs("HeapValue");
        String b;
        b._string = a._string;
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
            printf("%p new IndirectFixedCapacityValueArray[%zd]\n", this, _capacity);
        }
        
        virtual ~IndirectFixedCapacityValueArray() override {
            free(_storage);
            printf("%p del IndirectFixedCapacityValueArray[%zd]\n", this, _capacity);
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
            printf("%p new Value[]\n", this);
        }
        
        virtual ~HeapArray() override {
            printf("%p del Value[%zd]\n", this, _size);
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
            _storage[--_size] = Value::make_empty();
        }
        
        std::size_t size() const {
            return _size;
        }
        
    };
    
    
    struct Array {
        
        // not a Value because there is only one representation possible (so far)
        HeapArray* _array;
        
        std::size_t size() const {
            return _array->_size;
        }
        
        Value operator[](std::size_t pos) const {
            assert(pos < _array->_size);
            return _array->_storage[pos].get();
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
        };
        
        struct InnerTable {
            
            Traced<const IndirectFixedCapacityValueArray*> _manager;
            Entry* _storage = nullptr;
            std::size_t _mask = 0;
            std::size_t _count = 0;
            std::size_t _grace = 0;

            Value find(std::size_t h, Value k) {
                std::size_t i = h & _mask;
                for (;; i = ((i + 1) & _mask)) {
                    Entry* pe = _storage + i;
                    Value ki = pe->key;
                    // TODO: equals-with-one-known-hash
                    if (ki._is_empty()) {
                        return Value::make_empty();
                    }
                    if (ki == k) {
                        return pe->value;
                    }
                    // a different key, or a tombstone
                }
            }
            
            Value erase(std::size_t h, Value k) {
                std::size_t i = h & _mask;
                for (;; i = ((i + 1) & _mask)) {
                    Entry* pe = _storage + i;
                    Value ki = pe->key;
                    if (ki._is_empty()) {
                        return Value::make_empty();
                    }
                    if (ki == k) {
                        --_count;
                        pe->key = Value::make_tombstone();
                        Value v = pe->value;
                        pe->value = Value::make_empty();
                        return v;
                    }
                    // a different key, or a tombstone
                }
            }
            
            Value insert_or_assign(std::size_t h, Value k, Value v) {
                assert(_grace);
                std::size_t i = h & _mask;
                for (;; i = ((i + 1) & _mask)) {
                    Entry* pe = _storage + i;
                    Value ki = pe->key;
                    if (ki._is_empty()) {
                        pe->key = k;
                        pe->value = v;
                        ++_count;
                        --_grace;
                        return Value::make_empty();
                    }
                    if (ki._is_tombstone()) {
                        pe->key = k;
                        pe->value = v;
                        ++_count;
                        return Value::make_empty();
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
                    if (ki._is_empty()) {
                        return Value::make_empty();
                    }
                    if (ki == k) {
                        ++_count;
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
                    if (ki._is_empty()) {
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
                        return;
                    }
                    // Check for the violation of the precondition, that the
                    // key was not already in the InnerTable
                    assert(ki != k);
                    // a different key was found, continue
                }
            }
            
        };
        
        mutable InnerTable _alpha;
        mutable InnerTable _beta;
        mutable std::size_t _partition;
        
        
        virtual ~HeapTable() override {
            printf("%p del HeapTable[%zd]\n", this, _alpha._count + _beta._count);
        }
        
        virtual std::size_t gc_bytes() const override {
            return sizeof(HeapTable);
        }
        
        virtual void gc_enumerate() const override {
            trace(_alpha._manager);
            trace(_beta._manager);
        }

        
        
        
        void _invariant() const {
            assert(_beta._grace >= 2 * _alpha._count);
        }
        
        
        
        Value find(Value key) const {
            std::size_t h = key.hash();
            if (_alpha._count) {
                Value v = _alpha.find(h, key);
                if (!v._is_empty())
                    return v;
            }
            if (_beta._count) {
                Value v = _beta.find(h, key);
                if (!v._is_empty())
                    return v;
            }
            return Value::make_empty();
        }
        
        Value erase(Value key) const {
            std::size_t h = key.hash();
            if (_alpha._count) {
                Value v = _alpha.erase(h, key);
                if (!v._is_empty()) {
                    return v;
                }
            }
            if (_beta._count) {
                Value v = _beta.erase(h, key);
                if (!v._is_empty()) {
                    return v;
                }
            }
            return Value::make_empty();
            
        }
        
        Value insert_or_assign(Value key, Value value) const {
            printf("insert_or_assign (%lld, %lld)\n", key._integer >> 4, value._integer >> 4);
            std::size_t h = key.hash();
            printf("with hash %zd\n", h);
            
            if (_alpha._grace) {
                return _alpha.insert_or_assign(h, key, value);
            }
            // _alpha is terminal
            if (_alpha._count) {
                Value u = _alpha.try_assign(h, key, value);
                if (!u._is_empty())
                    return u;
            } else {
                // _alpha is terminal and empty, discard it
                if (_beta._storage) {
                    _alpha = _beta;
                    _beta._manager = nullptr;
                    _beta._storage = nullptr;
                    _beta._count = 0;
                    _beta._grace = 0;
                    _beta._mask = 0;
                    _partition = 0;
                } else {
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
                printf("New table of capacity %zd when old was %zd\n", new_capacity, _alpha._mask + 1);
                _beta._manager = new IndirectFixedCapacityValueArray(new_capacity * 2);
                _beta._storage = (Entry*) _beta._manager->_storage;
                _beta._count = 0;
                _beta._grace = new_grace;
                _beta._mask = new_capacity - 1;
            }
            Value ultimate = _beta.insert_or_assign(h, key, value);
            while (_beta._grace < 2 * _alpha._count) {
                Entry* pe = _alpha._storage + (_partition++);
                Value ki = pe->key;
                if (ki._is_empty() || ki._is_tombstone()) {
                    continue;
                }
                Value vi = pe->value;
                // inline _alpha.erase(...)
                pe->key = Value::make_tombstone();
                pe->value = Value::make_empty();
                _alpha._count--;
                assert(_beta._grace);
                _beta.must_insert(ki.hash(), ki, vi);
                printf("Evacuated (%lld, %lld)\n", ki._integer >> 4, vi._integer >> 4);
            }
            return ultimate;
        }
        
        std::size_t size() const {
            return _alpha._count + _beta._count;
        }
        
        bool contains(Value key) const {
            return !find(key)._is_empty();
        }
        
        
    };
    
    
    /*
    struct Table {
        
        const HeapTable* _pointer;
        
        operator Value() const {
            Value result;
            result._enumeration = (std::uint64_t)_pointer;
            return result;
        };
        
        std::size_t size() const;
        bool contains() const;
        Value find(Value key) const;
        Value erase(Value key);
        Value insert_or_assign(Value key, Value value);
        
    };
     */
    
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


    
      
    void foo() {
        
        Table t;
        t._pointer = new HeapTable;

        assert(t.size() == 0);
        assert(!t.contains("a"));
        assert(t.find("a") == Value::make_empty());
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
        assert(t.find("a") == Value::make_empty());
        
        
        for (int i = 0; i != 100; ++i) {
            Value j = t.insert_or_assign(i, i);
            assert(j == Value::make_empty());
            assert(t.size() == i + 1);
        }
        
        for (int i = 0; i != 200; ++i) {
            assert(t.contains(i) == (i < 100));
            if (i < 100) {
                assert(t.find(i) == i);
            } else {
                assert(t.find(i) == Value::make_empty());
            }
        }

        for (int i = 0; i != 200; ++i) {
            assert(t.contains(i) == (i < 100));
            if (i < 100) {
                assert(t.erase(i) == i);
            } else {
                assert(t.erase(i) == Value::make_empty());
            }
            assert(t.contains(i) == false);
        }
        
        assert(t.size() == 0);

        



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
        return _value.load(std::memory_order_relaxed);
    }
        
    Traced<Value>::Traced(const Value& value) 
    : _value(value) {
    }
    
    Traced<Value>::Traced(const Traced<Value>& value)
    : Traced(value.get()) {
    }

    Traced<Value>& Traced<Value>::operator=(const Value& desired) {
        shade(desired);
        shade(this->_value.exchange(desired, std::memory_order_release));
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
    
} // namespace gc
