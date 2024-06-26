//
//  wry/gc/HeapTable.hpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#ifndef wry_gc_HeapTable_hpp
#define wry_gc_HeapTable_hpp

#include <cassert>

#include <bit>

#include "debug.hpp"
#include "object.hpp"
#include "value.hpp"
#include "HeapManaged.hpp"
#include "RealTimeGarbageCollectedDynamicArray.hpp"

namespace wry::gc {
    
    
    
    
    
    
    
    
    
    enum EntryState {
        VACANT = 0,
        OCCUPIED,
        TOMBSTONE,
    };
    
    struct Entry {
        
        Traced<Value> key;
        Traced<Value> value;
        
        [[nodiscard]] Value entomb() {
            this->key = _value_make_tombstone();
            Value old = this->value;
            this->value = value_make_null();
            return old;
        }
        
    };
    
    void object_trace(const Entry& entry) {
        object_trace(entry.key);
        object_trace(entry.value);
    }

    template<typename K, typename KK, typename... Args>
    bool entry_match(const Entry<K, Args...>& entry, const KK& keylike) {
        return entry.key == keylike;
    }
    
    template<typename K, typename V>
    V* entry_pvalue(Entry<K, V>& entry) {
        return &entry.value;
    }
    
    template<typename E>
    struct GarbageCollectedHashMap {
        
        E* _data = nullptr;
        hash_t _mask = 0;
        size_t _count = 0;
        size_t _grace = 0;
        Traced<const GarbageCollectedIndirectStaticArray<E>*> _storage;
        
        hash_t next(hash_t i) const {
            return (i + 1) & _mask;
        }
        
        hash_t prev(hash_t i) const {
            return (i - 1) & _mask;
        }
        
        bool empty() const {
            return !_count;
        }
        
        size_t size() const {
            return _count;
        }
        
        void clear() {
            _data = nullptr;
            _mask = -1;
            _count = 0;
            _grace = 0;
            _storage = nullptr;
        }
        
        template<typename KK>
        E* pfind(hash_t h, KK&& kk) const {
            hash_t i = h & _mask;
            for (;; i = next(i)) {
                E* p = _data + i;
                switch (entry_state(p)) {
                    case EntryState::VACANT:
                        return nullptr;
                    case EntryState::OCCUPIED:
                        if (entry_match(p, kk))
                            return p;
                        break;
                    case EntryState::TOMBSTONE:
                        break;
                }
            }
        }
        
        Value find(std::size_t h, Value k) const {
            E* p = pfind(h, k);
            return p ? *entry_pvalue(*p) : value_make_null();
        }
        
        void clear_n_tombstones_before_i(size_t n, hash_t i) {
            // grace sticks to zero
            if (!_grace)
                return;
            {
                // E* pe = _data + i;
                // [[maybe_unused]] Value ki = pe->key;
                // assert(value_is_null(ki));
            }
            while (n--) {
                i = prev(i);
                E* pe = _data + i;
                // [[maybe_unused]] Value ki = pe->key;
                // assert(_value_is_tombstone(ki));
                // pe->key = value_make_null();
                entry_exhume(*pe);
                ++_grace;
            }
        }
        
        Value erase_from(std::size_t h, Value k, std::size_t i) {
            std::size_t tombstones = 0;
            for (;; i = next(i)) {
                Entry* pe = _storage + i;
                Value ki = pe->key;
                if (value_is_null(ki)) {
                    clear_n_tombstones_before_i(tombstones, i);
                    return value_make_null();
                }
                if (ki == k) {
                    --_count;
                    return pe->entomb();
                }
                if (_value_is_tombstone(ki)) {
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
                if (value_is_null(ki)) {
                    pe->key = k;
                    pe->value = v;
                    ++_count;
                    --_grace;
                    return value_make_null();
                }
                if (_value_is_tombstone(ki)) {
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
                if (value_is_null(ki)) {
                    return value_make_null();
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
                if (value_is_null(ki)) {
                    pe->key = k;
                    pe->value = v;
                    ++_count;
                    --_grace;
                    return;
                }
                if (_value_is_tombstone(ki)) {
                    pe->key = k;
                    pe->value = v;
                    ++_count;
                    // Check for the violation of the precondition, that the
                    // key is not later in the table
                    assert(value_is_null(erase_from(h, k, next(i))));
                    return;
                }
                // Check for the violation of the precondition, that the
                // key was not already in the InnerTable
                assert(ki != k);
                // a different key was found, continue
            }
        }
        
        void _invariant() const {
            if (!_data)
                return;
            // scan the whole thing
            std::size_t keys = 0;
            std::size_t nulls = 0;
            std::size_t tombstones = 0;
            for (std::size_t i = 0; i != _mask + 1; ++i) {
                Entry* pe = _storage + i;
                Value ki = pe->key;
                Value vi = pe->value;
                assert(!_value_is_tombstone(vi));
                if (value_is_null(ki)) {
                    ++nulls;
                    assert(value_is_null(vi));
                } else if (_value_is_tombstone(ki)) {
                    ++tombstones;
                    assert(value_is_null(vi));
                } else {
                    ++keys;
                    assert(!value_is_null(vi));
                }
            }
            assert(keys + nulls + tombstones == _mask + 1);
            assert(keys == _count);
        }
                
    };
    
    
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
        
        
        InnerTable _alpha;
        InnerTable _beta;
        std::size_t _partition = 0;
        
        void _invariant() const {
            _alpha._invariant();
            _beta._invariant();
            /*
             if (_beta._storage) {
             for (int i = 0; i != _alpha._mask + 1; ++i) {
             Entry* pe = _alpha._storage + i;
             Value ki = pe->key;
             if (is_null(ki) || ki._is_tombstone())
             continue;
             Value vj = _beta.find(value_hash(ki), ki);
             assert(vj.is_null());
             }
             for (int i = 0; i != _beta._mask + 1; ++i) {
             Entry* pe = _beta._storage + i;
             Value ki = pe->key;
             if (is_null(ki) || ki._is_tombstone())
             continue;
             Value vj = _alpha.find(value_hash(ki), ki);
             assert(vj.is_null());
             }
             }
             */
        }
        
        
        
        Value find(Value key) const {
            std::size_t h = value_hash(key);
            if (_alpha._count) {
                Value v = _alpha.find(h, key);
                if (!value_is_null(v))
                    return v;
            }
            if (_beta._count) {
                Value v = _beta.find(h, key);
                if (!value_is_null(v))
                    return v;
            }
            return value_make_null();
        }
        
        Value erase(Value key) {
            //_invariant();
            std::size_t h = value_hash(key);
            if (_alpha._count) {
                Value v = _alpha.erase(h, key);
                if (!value_is_null(v)) {
                    return v;
                }
            }
            if (_beta._count) {
                Value v = _beta.erase(h, key);
                if (!value_is_null(v)) {
                    return v;
                }
            }
            return value_make_null();
            
        }
        
        Value insert_or_assign(Value key, Value value) {
            // _invariant();
            std::size_t h = value_hash(key);
            
            if (_alpha._grace) {
                Value x = _alpha.insert_or_assign(h, key, value);
                return x;
            }
            
            {
                for (std::size_t i = 0; i != _partition; ++i) {
                    Entry* pe = _alpha._storage + i;
                    Value ki = pe->key;
                    assert(value_is_null(ki) || _value_is_tombstone(ki));
                }
            }
            
            
            if (_alpha._count) {
                // _alpha is not yet empty
                Value u = _alpha.try_assign(h, key, value);
                // _invariant();
                if (!value_is_null(u))
                    return u;
                // we have proved that key is not in alpha
            } else {
                // _alpha is terminal and empty, discard it
                if (_beta._data) {
                    // swap in _beta
                    _alpha = _beta;
                    _beta._storage = nullptr;
                    _beta._data = nullptr;
                    _beta._count = 0;
                    _beta._grace = 0;
                    _beta._mask = 0;
                    _partition = 0;
                } else {
                    assert(_alpha._data == nullptr);
                    _alpha._manager = new HeapManaged<Entry>(4);
                    _alpha._data = _alpha._storage->_storage;
                    _alpha._mask = 3;
                    _alpha._grace = 3;
                    _alpha._count = 0;
                    _partition = 0;
                }
                return insert_or_assign(key, value);
            }
            if (!_beta._grace) {
                assert(_beta._data == nullptr);
                using wry::type_name;
                std::size_t new_capacity = std::bit_ceil((_alpha._count * 8 + 2) / 3);
                std::size_t new_grace = new_capacity * 3 / 4;
                _beta._manager = new HeapManaged<Entry>(new_capacity);
                _beta._data = _beta._storage->_storage;
                _beta._count = 0;
                _beta._grace = new_grace;
                _beta._mask = new_capacity - 1;
            }
            Value ultimate = _beta.insert_or_assign(h, key, value);
            while (_alpha._count) {
                Entry* pe = _alpha._storage + (_partition++);
                Value ki = pe->key;
                if (value_is_null(ki) || _value_is_tombstone(ki))
                    continue;
                Value vi = pe->entomb();
                _alpha._count--;
                assert(_beta._grace);
                _beta.must_insert(value_hash(ki), ki, vi);
                break;
            }
            return ultimate;
        }
        
        bool empty() const {
            return _alpha.empty();
        }
        
        size_t size() const {
            return _alpha._count + _beta._count;
        }
        
        bool contains(Value key) const {
            return !value_is_null(find(key));
        }
        
        Traced<Value>& find_or_insert_null(Value key) {
            std::size_t h = value_hash(key);
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
        
        HeapTable() {}
        virtual ~HeapTable() final = default;
        
        
        virtual bool _value_empty() const override {
            return empty();
        }
        
        virtual size_t _value_size() const override {
            return size();
        }
        
        virtual Value _value_find(Value key) const override {
            return find(key);
        }
        
        virtual Value _value_insert_or_assign(Value key, Value value) override {
            return insert_or_assign(key, value);
        }
        
        virtual bool _value_contains(Value key) const override {
            return contains(key);
        }
        
        virtual Value _value_erase(Value key) override {
            return erase(key);
        }

        virtual void _object_scan() const override {
            object_trace(_alpha._storage);
            object_trace(_beta._storage);
        }

    }; // struct HeapTable
    
    
    
}

#endif /* wry_gc_HeapTable_hpp */
