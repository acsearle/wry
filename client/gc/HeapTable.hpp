//
//  wry/gc/HeapTable.hpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#ifndef wry_gc_HeapTable_hpp
#define wry_gc_HeapTable_hpp

#include <cassert>
#include <concepts>
#include <bit>
#include <optional>

#include "debug.hpp"
#include "object.hpp"
#include "value.hpp"
#include "HeapManaged.hpp"
#include "RealTimeGarbageCollectedDynamicArray.hpp"
#include "hash.hpp"

namespace wry::gc {
        
    // If the map has tombstones, it must periodically be copied to
    // restore an acceptable number of vacancies to terminate searches.
    //
    // Yet we can't incrementally copy such a map into the same sized
    // allocation unless: it is mostly unoccupied; or, we move a large (and
    // occupancy-dependent) number of elements per operation.
    //
    // We fall back to Robin Hood hashing, which has no tombstones and
    // instead performs this sort of "compaction" continuously.
    //
    // When the Robin Hood hash set needs to be resized, we double its
    // size.  The new capacity is half full; it is sufficient for every
    // insert to copy over one element (on average) for the incremental
    // copy to be complete before the new set fills up.  In practice we
    // want the incremental resize to complete even if the workflow is
    // mostly lookups, so we copy 2 slots (which we expect to contain 3/2
    // objects) per operation.

    
    // T must support
    // - T()
    // - .occupied
    // - .vacant
    // - .vacate
    // - .hash
    // - .assign
    
    // TODO: is it worth making Entry a concept?
    
    template<typename A, typename B>
    using Pair = std::pair<A, B>;
    
    template<typename K, typename V>
    struct BasicEntry {
        
        // we can't use std::optional here because we need to concurrently
        // scan the object, and construction/destruction/has_value are not
        // atomic
        
        // More generally, the Pair needs to always be in a constructed and
        // scannable state, even when unoccupied
        
        Pair<K, V> _kv;
        bool _occupied;
        
        bool occupied() const { return _occupied; }
        bool vacant() const { return !_occupied; }
        void vacate() {
            assert(_occupied);
            _occupied = false;
        }
        
        size_t hash() const {
            using wry::hash;
            return hash(_kv.first);
        }
        
        template<typename J, typename U>
        static size_t hash(const Pair<J, U>& ju) {
            using wry::hash; 
            return hash(ju.first);
        }

        template<typename J>
        static size_t hash(const J& j) {
            using wry::hash;
            return hash(j);
        }

        void assign(BasicEntry&& other) {
            assert(other._occupied);
            _kv = std::move(other._kv);
            _occupied = true;
        }
        
        template<typename J, typename U>
        void assign(std::pair<J, U>&& ju) {
            _kv = std::move(ju);
            _occupied = true;
        }
        
        template<typename J, typename U>
        void assign(J&& j, U&& u) {
            _kv.first = std::forward<J>(j);
            _kv.second = std::forward<U>(u);
            _occupied = true;
        }
        
        template<typename J>
        bool equivalent(size_t h, J&& j) const {
            assert(_occupied);
            return _kv.first == j;
        }

    };
    
    template<typename K, typename V>
    void object_trace(const BasicEntry<K, V>& e) {
        // object_trace(e._kv.first);
        object_trace(e._kv.second);
    }

    template<typename K, typename V>
    void object_shade(const BasicEntry<K, V>& e) {
        // object_trace(e._kv.first);
        object_shade(e._kv.second);
    }

    template<typename K, typename V>
    void object_debug(const BasicEntry<K, V>& e) {
        // object_debug(e._kv.first);
        // object_debug(e._kv.second);
    }

    // Provides Robin Hood semantics on a power-of-two-sized array of
    // entries that satisfy minimal requirements.
    //
    // Does not
    // - own storage
    // - track occupant count
    // - track load factor
    // - resize
    // - know about garbage collection
    //
    // These services are provided by the next layers
    
    template<typename T>
    struct BasicHashSetA {
        
        T* _data;
        size_t _capacity;
        
        BasicHashSetA()
        : _data(nullptr)
        , _capacity(0) {}
        
        T* data() const { return _data; }
        size_t capacity() const { return _capacity; }
        
        T* begin() const { return _data; }
        T* end() const { return _data + _capacity; }
        
        size_t _mask(size_t i) const {
            return(_capacity - 1) & i;
        }
        
        size_t _succ(size_t index) const {
            return _mask(index + 1);
        }
        
        size_t _pred(size_t index) const {
            return _mask(index - 1);
        }
        
        size_t _displacement(size_t i) const {
            assert(i < capacity());
            assert(_data[i].occupied());
            size_t h = _data[i].hash();
            return _mask(i - h);
        }
        
        size_t _invariant() const {
            assert(_capacity == 0 || std::has_single_bit(_capacity));
            size_t count = 0;
            for (size_t j = 0; j != _capacity; ++j) {
                if (_data[j].occupied()) {
                    ++count;
                    size_t e = _displacement(j);
                    size_t i = _pred(j);
                    if (_data[i].occupied()) {
                        // if there is an occupied slot before us, it should be
                        // at least as displaced as we are
                        size_t d = _displacement(i);
                        assert(d + 1 >= e);
                    } else {
                        // if there is an empty slot before this entry, we must
                        // be in our ideal slot
                        assert(e == 0);
                    }
                }
            }
            return count;
        }

        void _steal_from_the_rich(size_t i) const {
            _invariant();
            assert(i < capacity());
            assert(_data[i].occupied());
            size_t j = i;
            for (;;) {
                j = _succ(j);
                if (_data[j].vacant())
                    break;
            }
            // move_backward
            for (;;) {
                size_t k = _pred(j);
                _data[j].assign(std::move(_data[k]));
                if (k == i)
                    break;
                j = k;
            }
        }
        
        size_t _give_to_the_poor(size_t i) const {
            assert(i < capacity());
            assert(_data[i].occupied());
            for (;;) {
                size_t j = _succ(i);
                if (_data[j].vacant())
                    break;
                size_t e = _displacement(j);
                if (e == 0)
                    break;
                // move forward
                _data[i].assign(std::move(_data[j]));
                i = j;
            }
            assert(_data[i].occupied());
            return i;
        }
        
        // we claim that the element is present, so we don't need to check
        // several conditions
        template<typename Q>
        size_t _find_present(size_t h, Q&& q) const {
            size_t i = _mask(h);
            for (;;) {
                assert(_data[i].vacant());
                if (_data[i].equivalent(h, q))
                    return i;
                i = _succ(i);
            }
        }
        
        // the element is known not to be present; we are looking for where
        // it should be inserted
        template<typename Q>
        size_t _find_absent(size_t h, Q&& q) const {
            size_t d = 0;
            size_t i = _mask(h);
            for (;;) {
                if (_data[i].vacant())
                    return i;
                size_t e = _displacement(i);
                if (e < d)
                    return i;
                i = _succ(i);
                ++d;
            }
        }
        
        // where and if
        template<typename Q>
        std::pair<size_t, bool> _find(size_t h, Q&& q) const {
            if (_capacity == 0)
                return { 0, false };
            size_t d = 0;
            size_t i = _mask(h);
            for (;;) {
                if (_data[i].vacant())
                    return { i, false };
                if (_data[i].equivalent(h, q))
                    return { i, true };
                size_t e = _displacement(i);
                if (e < d)
                    return {i, false };
                i = _succ(i);
                ++d;
            }
        }
      
        template<typename Q>
        std::pair<size_t, bool> _erase(size_t h, Q&& q) const {
            auto [i, f] = _find(h, std::forward<Q>(q));
            if (f) {
                size_t j = _give_to_the_poor(i);
                _data[j].vacate();
            }
            return {i, f};
        }

        void _erase_present_at(size_t i) const {
            i = _give_to_the_poor(i);
            _data[i].vacate();
        }
        
        template<typename Q>
        size_t _erase_present(size_t h, Q&& q) const {
            size_t i = _find_present(h, std::forward<Q>(q));
            _erase_present_at(i);
            return i;
        }
        
        template<typename Q>
        size_t _assign_present(size_t h, Q&& q) const {
            size_t i = _find_present(h, q);
            _data[i].assign(std::forward<Q>(q));
            return i;
        }

        template<typename... Q>
        void _assign_present_at(size_t i, Q&&... q) const {
            _data[i].assign(std::forward<Q>(q)...);
        }

        template<typename... Q>
        void _insert_absent_at(size_t i, Q&&... q) {
            if (_data[i].occupied())
                _steal_from_the_rich(i);
            _data[i].assign(std::forward<Q>(q)...);
        }
        
        template<typename Q>
        size_t _insert_absent(size_t h, Q&& q) {
            size_t i = _find_absent(h, q);
            _insert_absent_at(i, std::forward<Q>(q));
            return i;
        }
        
        template<typename Q>
        bool _insert_or_assign(size_t h, Q&& q) {
            auto [i, f] = _find(h, q);
            if (!f && _data[i].occupied())
                _steal_from_the_rich(i);
            _data[i].assign(std::forward<Q>(q));
            return !f;
        }
        
        size_t _threshold() const {
            return _capacity - (_capacity >> 2);
        }
        
    }; // BasicHashSetA
    
    
    // BasicHashSetC
    //
    // owns storage, counts occupants, knows if full
    // still not dynamically resized, but can be manually resized when empty
    
    template<typename T>
    struct BasicHashSetB {
        BasicHashSetA<T> _inner;
        size_t _size;
        Traced<GarbageCollectedIndirectStaticArray<T>*> _storage;
        
        BasicHashSetB()
        : _inner()
        , _size(0)
        , _storage() {
        }
        
        void _invariant() {
            assert(_size < _inner._capacity || _size == 0);
            size_t n = _inner._invariant();
            assert(n == _size);
        }
        
        void clear() {
            _inner._data = nullptr;
            _inner._capacity = 0;
            _size = 0;
            _storage = nullptr;
        }
        
        void _reserve(size_t new_capacity) {
            assert(_size == 0);
            assert(std::has_single_bit(new_capacity));
            auto* p = new GarbageCollectedIndirectStaticArray<T>(new_capacity);
            _inner._data = p->data();
            _inner._capacity = new_capacity;
            _size = 0;
            _storage = p;
        }
        
        bool empty() const {
            return _size == 0;
        }
        
        bool full() const {
            // notably, true when the capacity is zero
            assert(_size <= _inner._threshold());
            return _size == _inner._threshold();
        }
        
        size_t capacity() const {
            return _inner.capacity();
        }
        
        size_t size() const {
            return _size;
        }
        
        void _insert_absent(T&& x) {
            size_t h = x.hash();
            _inner._insert_absent(h, std::move(x));
            assert(!full());
            ++_size;
        }

        template<typename... R>
        void _insert_absent_at(size_t i, R&&... r) {
            _inner._insert_absent_at(i, std::forward<R>(r)...);
            assert(!full());
            ++_size;
        }

        void _erase_present_at(size_t i) {
            _inner._erase_present_at(i);
            assert(_size);
            --_size;
        }
        
        template<typename... R>
        void _assign_present_at(size_t i, R&&... r) {
            _inner._assign_present_at(i, std::forward<R>(r)...);
        }
        
        template<typename Q>
        bool _erase(size_t h, Q&& q) {
            if (_size == 0)
                return false;
            auto [i, f] = _inner._erase(h, std::forward<Q>(q));
            if (f)
                --_size;
            return f;
        }
                
        
    };
    
    template<typename T>
    void object_trace(const BasicHashSetB<T>& self) {
        object_trace(self._storage);
    }

    template<typename T>
    void object_shade(const BasicHashSetB<T>& self) {
        object_shade(self._storage);
    }

    
    // ?
    // object_shade
    // object_trace
    // !object_scan
    
    // BasicHashSetC
    //
    // Real time dynamic sized HashSet
    //
    // Contains two HashSetB.  When alpha fills up, it is moved into beta
    // and an empty map with twice the capacity is installed in alpha.
    // Subsequent operations are _taxed_ to incrementally move the elements of
    // beta into alpha, such that beta is fully drained before alpha itself can
    // become full.
    //
    // Thus all our operations are bounded to be less than the sum of
    // - a calloc of any size
    // - a bounded number (3?) of probes, themselves probabilistically bounded
    //     by the Robin Hood policy and the load factor.
    //
    // The average operation will be cheaper than this worst case, but
    // operations close in time will not be independently cheap, as early in
    // the copy lookups will miss on alpha a lot, and unlucky long probes will
    // be encountered repeatedly.
    //
    // This is quite a complicated bound but it is for practical purposes
    // true constant time, compared to the amortized constant time with
    // guaranteed O(N) hiccoughs of a non-incremental resize.
    //
    // The associated cost in manager size is x2, in heap size x1.5 while
    // the old allocation is copied from, and in runtime is actually not much
    // worse since the copies must happen at some time in either scheme;
    // In a read-heavy workload, once the last incremental resize is completed
    // the only overhead is that find must check that _beta is nonempty.
    
    template<typename T>
    struct BasicHashSetC {
        
        BasicHashSetB<T> _alpha;
        BasicHashSetB<T> _beta;
        size_t _partition = 0;
        
        void _invariant() {
            _alpha._invariant();
            _beta._invariant();
            assert(_partition <= _beta.capacity());
            assert(_alpha.size() + _beta.size() <= _alpha._inner._threshold());
        }
        
        void _tax() {
            // TODO: messy
            if (_beta._inner._data) {
                if (!_beta.empty()) {
                    T& t = _beta._inner._data[_partition];
                    if (t.occupied()) {
                        _alpha._insert_absent(std::move(t));
                        _beta._erase_present_at(_partition);
                        if (_beta.empty()) {
                            _beta.clear();
                            _partition = -1;
                        }
                    }
                } else {
                    _beta.clear();
                    _partition = -1;
                }
                _partition = _beta._inner._succ(_partition);
            }
        }
        
        void _tax2() {
            _tax();
            _tax();
        }
        
        void _ensure_not_full() {
            if (_alpha.full()) {
                assert(_beta.empty());
                _beta = std::move(_alpha);
                _partition = 0;
                // TODO: clear, reserve
                _alpha.clear();
                _alpha._reserve(max(_beta.capacity() << 1, 4));
            }
        }
        
        size_t size() const {
            return _alpha.size() + _beta.size();
        }
        
        template<typename Q>
        Pair<size_t, bool> _find(size_t h, Q&& q) {
            _tax();
            auto [i, f] = _alpha._inner._find(h, q);
            if (f)
                return {i, f};
            auto [j, g] = _beta._inner._find(h, q);
            if (!g)
                return {i, f};
            _alpha._insert_absent_at(i, std::move(_beta._inner._data[j]));
            _beta._erase_present_at(j);
            return {i, g};
        }
        
        template<typename Q>
        bool _erase(size_t h, Q&& q) {
            _tax();
            return _alpha._erase(h, q) || _beta._erase(h, q);
        }
                
        template<typename Q, typename U>
        bool _insert_or_assign(size_t h, Q&& q, U&& u) {
            _tax2();
            _ensure_not_full();
            _invariant();
            auto [i, f] = _alpha._inner._find(h, q);
            _invariant();
            if (f) {
                _alpha._assign_present_at(i, std::forward<Q>(q), std::forward<U>(u));
            } else {
                _invariant();
                _beta._erase(h, q);
                _invariant();
                _alpha._insert_absent_at(i, std::forward<Q>(q), std::forward<U>(u));
                _invariant();
            }
            return !f;
        }
        
        bool empty() const {
            return _alpha.empty() && _beta.empty();
        }

    }; // BasicHashSetC
    
    template<typename T>
    void object_trace(const BasicHashSetC<T>& self) {
        object_trace(self._alpha);
        object_trace(self._beta);
    }

    template<typename T>
    void object_shade(const BasicHashSetC<T>& self) {
        object_shade(self._alpha);
        object_shade(self._beta);
    }

    
    
    template<typename K, typename V>
    struct HashMap;
    
    template<typename K>
    struct HashMap<K, Value> {
        
        // TODO: Traced<K>
        // More generally, decide on Traced by some typefunction to capture
        // pointers convertible Object and Values and...
        
        using T = BasicEntry<K, Traced<Value>>;
        
        // TODO: sort out const-correctness
        mutable BasicHashSetC<T> _inner;
        
        void _invariant() const {
            _inner._invariant();
        }
        
        size_t size() const {
            return _inner.size();
        }
        
        template<typename Q>
        Value read(Q&& q) const {
            size_t h = hash(q);
            auto [i, f] = _inner._find(h, q);
            return f ? _inner._alpha._inner._data[i]._kv.second : value_make_null();
        }
        
        template<typename J, typename U>
        void write(J&& j, U&& u) {
            size_t h = hash(j);
            _inner._insert_or_assign(h, j, u);
        }
        
        template<typename Q>
        void erase(Q&& q) {
            size_t h = hash(q);
            _inner._erase(h, q);
        }
        
        bool empty() const {
            return _inner.empty();
        }
        
        template<typename Q>
        bool contains(Q&& q) const {
            return _inner._find(hash(q), q).second;
        }
                        
    };
    
    
    
    template<typename K, typename A>
    struct HashMap<K, A*> {
        
        // TODO: Traced<K>
        // More generally, decide on Traced by some typefunction to capture
        // pointers convertible Object and Values and...
        
        using T = BasicEntry<K, Traced<A*>>;
        
        // TODO: sort out const-correctness
        mutable BasicHashSetC<T> _inner;
        
        void _invariant() const {
            _inner._invariant();
        }
        
        size_t size() const {
            return _inner.size();
        }
        
        template<typename Q>
        A* read(Q&& q) const {
            size_t h = hash(q);
            auto [i, f] = _inner._find(h, q);
            return f ? _inner._alpha._inner._data[i]._kv.second : nullptr;
        }
        
        template<typename J, typename U>
        void write(J&& j, U&& u) {
            size_t h = hash(j);
            _inner._insert_or_assign(h, j, u);
        }
        
        template<typename Q>
        void erase(Q&& q) {
            size_t h = hash(q);
            _inner._erase(h, q);
        }
        
        bool empty() const {
            return _inner.empty();
        }
        
        template<typename Q>
        bool contains(Q&& q) const {
            return _inner._find(hash(q), q).second;
        }
        
    };
    
    template<typename K, typename V>
    void object_trace(const HashMap<K, V>& self) {
        return object_trace(self._inner);
    }

    template<typename K, typename V>
    void object_shade(const HashMap<K, V>& self) {
        return object_shade(self._inner);
    }


    
    
    struct HeapHashMap : Object {
        
        HashMap<Value, Value> _inner;
        
        void _invariant() const {
            _inner._invariant();
        }
        
        virtual void _object_scan() const override {
            object_trace(_inner);
        }


        virtual bool _value_empty() const override {
            _invariant();
            return _inner.empty();
        }

        virtual Value _value_erase(Value key) override {
            _invariant();
            // TODO: single lookup
            Value result = _inner.read(key);
            _inner.erase(key);
            return result;
        }
        
        virtual Value _value_find(Value key) const override {
            _invariant();
            return _inner.read(key);
        }
        
        virtual bool _value_contains(Value key) const override {
            _invariant();
            return _inner.contains(key);
        }
        
        virtual Value _value_insert_or_assign(Value key, Value value) override {
            _invariant();
            Value result = _inner.read(key);
            _inner.write(key, value);
            return result;
        }

        virtual size_t _value_size() const override {
            _invariant();
            return _inner.size();
        }
                
    };

} // namespace wry::gc

#endif /* wry_gc_HeapTable_hpp */
