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

//#include "adl.hpp"
#include "debug.hpp"
#include "garbage_collected.hpp"
#include "value.hpp"
#include "HeapArray.hpp"
#include "hash.hpp"

namespace wry {
        
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
        
        size_t _hash() const {
            return hash(_kv.first);
        }
        
        template<typename J, typename U>
        static size_t _hash(const Pair<J, U>& ju) {
            return hash(ju.first);
        }

        template<typename J>
        static size_t _hash(const J& j) {
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
        void emplace(J&& j) {
            _kv.first = std::forward<J>(j);
            _kv.second = V();
            _occupied = true;
        }
        
        
        template<typename J>
        bool equivalent(size_t h, J&& j) const {
            assert(_occupied);
            return _kv.first == j;
        }

    };
    
    
    
    template<typename K, typename V>
    void trace(const BasicEntry<K, V>& e,void*p) {
        trace(e._kv.first,p);
        trace(e._kv.second,p);
    }

    template<typename K, typename V>
    void shade(const BasicEntry<K, V>& e) {
        shade(e._kv.first);
        shade(e._kv.second);
    }

    template<typename K, typename V>
    void any_debug(const BasicEntry<K, V>& e) {
        any_debug(e._kv.first);
        any_debug(e._kv.second);
    }
    
    template<typename K, typename V>
    hash_t any_hash(const BasicEntry<K, V>& e) {
        return any_hash(e._kv.first);
    }
    
    template<typename K, typename V>
    void passivate(BasicEntry<K, V>& e) {
        passivate(e._kv.first);
        passivate(e._kv.second);
    }
    
    /*
    template<typename K, typename V>
    void object_trace_weak(const BasicEntry<K, V>& e) {
        object_trace(e);
    }
     */
    
    

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
        
        BasicHashSetA(BasicHashSetA&& other)
        : _data(exchange(other._data, nullptr))
        , _capacity(exchange(other._capacity, 0)) {
        }
        
        BasicHashSetA& operator=(BasicHashSetA&& other) {
            _data = exchange(other._data, nullptr);
            _capacity = exchange(other._capacity, 0);
            return *this;
        }
        
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
            size_t h = _data[i]._hash();
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

        template<typename... Q>
        void _emplace_absent_at(size_t i, Q&&... q) {
            if (_data[i].occupied())
                _steal_from_the_rich(i);
            _data[i].emplace(std::forward<Q>(q)...);
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

        template<typename Q>
        bool _try_insert(size_t h, Q&& q) {
            auto [i, f] = _find(h, q);
            if (!f) {
                // not found; insert
                if (_data[i].occupied())
                    _steal_from_the_rich(i);
                _data[i].assign(std::forward<Q>(q));
            }
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
        Scan<ArrayStaticIndirect<T>*> _storage;
        
        const T* begin() const {
            return _inner._data;
        }
        
        const T* end() const {
            return _inner._data + _inner._capacity;
        }
        
        BasicHashSetB()
        : _inner()
        , _size(0)
        , _storage() {
        }
        
        BasicHashSetB(BasicHashSetB&& other)
        : _inner(std::move(other._inner))
        , _size(exchange(other._size, 0))
        , _storage(std::move(other._storage)) {
        }

        BasicHashSetB& operator=(BasicHashSetB&& other)
        {
            _inner = std::move(other._inner);
            _size = exchange(other._size, 0);
            _storage = std::move(other._storage);
            return *this;
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
            auto* p = new ArrayStaticIndirect<T>(new_capacity);
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
        
        size_t _insert_absent_hash(size_t h, T&& x) {
            size_t i = _inner._insert_absent(h, std::move(x));
            assert(!full());
            ++_size;
            return i;
        }
        
        size_t _insert_absent(T&& x) {
            size_t h = x._hash();
            return _insert_absent_hash(h, std::move(x));
        }

        template<typename... R>
        void _insert_absent_at(size_t i, R&&... r) {
            _inner._insert_absent_at(i, std::forward<R>(r)...);
            assert(!full());
            ++_size;
        }

        template<typename... R>
        void _emplace_absent_at(size_t i, R&&... r) {
            _inner._emplace_absent_at(i, std::forward<R>(r)...);
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
    void trace(const BasicHashSetB<T>& self,void*p) {
        trace(self._storage,p);
    }

    template<typename T>
    void shade(const BasicHashSetB<T>& self) {
        shade(self._storage);
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

        BasicHashSetC()
        : _alpha()
        , _beta()
        , _partition(0) {
        }
        
        BasicHashSetC(BasicHashSetC&& other)
        : _alpha(std::move(other._alpha))
        , _beta(std::move(other._beta))
        , _partition(exchange(other._partition, 0)) {
        }
        
        BasicHashSetC& operator=(BasicHashSetC&& other) {
            _alpha = std::move(other._alpha);
            _beta = std::move(other._beta);
            _partition = exchange(other._partition, 0);
            return *this;
        }
        
        
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
        
        template<typename Q>
        size_t _find_or_emplace(size_t h, Q&& q) {
            _tax2();
            _ensure_not_full();
            _invariant();
            auto [i, f] = _alpha._inner._find(h, q);
            _invariant();
            if (f) {
                // Found in alpha, return
                return i;
            }
            auto [j, g] = _beta._inner._find(h, q);
            if (g) {
                // Found in beta, move over
                // _beta.erase(h, q);
                i = _alpha._insert_absent_hash(h, std::move(_beta._inner._data[j]));
                _beta._erase_present_at(j);
                _invariant();
                return i;
            }
            _invariant();
            _alpha._emplace_absent_at(i, std::forward<Q>(q));
            _invariant();
            return i;
        }
        
        
        bool empty() const {
            return _alpha.empty() && _beta.empty();
        }
        
        void clear() {
            _alpha.clear();
            _beta.clear();
            _partition = 0;
        }
        
        template<typename Q>
        bool _insert(size_t h, Q&& q) {
            _tax2();
            _ensure_not_full();
            _invariant();
            auto [i, f] = _alpha._inner._find(h, q);
            if (f)
                return false;
            auto [j, g] = _beta._inner._find(h, q);
            if (g)
                return false;
            _invariant();
            _alpha._insert_absent_at(i, std::forward<Q>(q));
            return true;
        }

    }; // BasicHashSetC
    
    template<typename T>
    void trace(const BasicHashSetC<T>& self,void*p) {
        trace(self._alpha,p);
        trace(self._beta,p);
    }
    
    template<typename T>
    void shade(const BasicHashSetC<T>& self) {
        shade(self._alpha);
        shade(self._beta);
    }

    
    template<typename K, typename V>
    struct GCHashMap {
        
        // TODO: Traced<K>
        // More generally, decide on Traced by some typefunction to capture
        // pointers convertible Object and Values and...
        
        using T = BasicEntry<K, V>;
        
        // TODO: sort out const-correctness
        mutable BasicHashSetC<T> _inner;
        
        void _invariant() const {
            _inner._invariant();
        }
        
        size_t size() const {
            return _inner.size();
        }
        
        template<typename Q>
        auto read(Q&& q) const {
            size_t h = hash(q);
            auto [i, f] = _inner._find(h, q);
            using U = decltype(_inner._alpha._inner._data[i]._kv.second);
            return f ? any_read(_inner._alpha._inner._data[i]._kv.second) : any_none<U>;
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
        
        // aka the notorious std::map::operator[]
        template<typename Q>
        V& find_or_emplace(Q&& q) {
            size_t h = hash(q);
            size_t i = _inner._find_or_emplace(h, std::forward<Q>(q));
            assert(_inner._alpha._inner._data);
            return _inner._alpha._inner._data[i]._kv.second;
        }
        
        template<typename Q>
        std::pair<K, V>* find(Q&& q) {
            size_t h = hash(q);
            auto [i, f] = _inner._find(h, q);
            return f ? &_inner._alpha._inner._data[i]._kv : nullptr;
        }
        
                        
    };
    
    template<typename K, typename V>
    void trace(const GCHashMap<K, V>& self,void*p) {
        trace(self._inner,p);
    }

    template<typename K, typename V>
    void shade(const GCHashMap<K, V>& self) {
        shade(self._inner);
    }

//
//    
//    template<typename K, typename A>
//    struct HashMap<K, A*> {
//        
//        // TODO: Traced<K>
//        // More generally, decide on Traced by some typefunction to capture
//        // pointers convertible Object and Values and...
//        
//        using T = BasicEntry<K, Scan<A*>>;
//        
//        // TODO: sort out const-correctness
//        mutable BasicHashSetC<T> _inner;
//        
//        void _invariant() const {
//            _inner._invariant();
//        }
//        
//        size_t size() const {
//            return _inner.size();
//        }
//        
//        template<typename Q>
//        A* read(Q&& q) const {
//            size_t h = hash(q);
//            auto [i, f] = _inner._find(h, q);
//            return f ? _inner._alpha._inner._data[i]._kv.second : nullptr;
//        }
//        
//        template<typename J, typename U>
//        void write(J&& j, U&& u) {
//            size_t h = hash(j);
//            _inner._insert_or_assign(h, j, u);
//        }
//        
//        template<typename Q>
//        void erase(Q&& q) {
//            size_t h = hash(q);
//            _inner._erase(h, q);
//        }
//        
//        bool empty() const {
//            return _inner.empty();
//        }
//        
//        template<typename Q>
//        bool contains(Q&& q) const {
//            return _inner._find(hash(q), q).second;
//        }
//        
//    };
//    
//    template<typename K, typename V>
//    void object_trace(const HashMap<K, V>& self) {
//        return object_trace(self._inner);
//    }
//
//    template<typename K, typename V>
//    void shade(const HashMap<K, V>& self) {
//        return shade(self._inner);
//    }


    
    
    struct HeapHashMap : GarbageCollected {
        
        GCHashMap<Scan<Value>, Scan<Value>> _inner;
        
        void _invariant() const {
            _inner._invariant();
        }
        
        virtual void _garbage_collected_enumerate_fields(TraceContext*p) const override {
            trace(_inner,p);
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
    
    
    
    
    
    
    template<typename K>
    struct BasicHashSetEntry {
        K _key;
        bool _occupied;
        
        bool occupied() const { return _occupied; }
        bool vacant() const { return !_occupied; }
        void vacate() {
            assert(_occupied);
            _occupied = false;
        }
        
        size_t hash() const {
            return hash(_key);
        }
        
        template<typename J>
        static size_t hash(const J& j) {
            return any_hash(j);
        }
                
        void assign(BasicHashSetEntry&& other) {
            assert(other._occupied);
            _key = std::move(other._key);
            _occupied = true;
        }
        
        template<typename J>
        void assign(J&& j) {
            _key = std::forward<J>(j);
            _occupied = true;
        }
        
        template<typename J>
        bool equivalent(size_t h, J&& j) const {
            assert(_occupied);
            return _key == j;
        }
        
    };
    
    
    
    template<typename K>
    void trace(const BasicHashSetEntry<K>& e) {
        trace(e._key);
    }
    
    template<typename K>
    void shade(const BasicHashSetEntry<K>& e) {
        shade(e._key);
    }
    
    template<typename K>
    void any_debug(const BasicHashSetEntry<K>& e) {
        any_debug(e._key);
    }
    
    template<typename K>
    hash_t any_hash(const BasicHashSetEntry<K>& e) {
        return any_hash(e._key);
    }
    
    template<typename K>
    void passivate(BasicHashSetEntry<K>& e) {
        passivate(e._key);
    }
    
    template<typename K>
    void any_trace_weak(const BasicHashSetEntry<K>& e) {
        trace(e);
    }
    
    
    
    
    
    

    template<typename K>
    struct GCHashSet {
        
        // TODO: Traced<K>
        // More generally, decide on Traced by some typefunction to capture
        // pointers convertible Object and Values and...
        
        using T = BasicHashSetEntry<K>;
        
        // TODO: sort out const-correctness
        mutable BasicHashSetC<T> _inner;
        
        void _invariant() const {
            _inner._invariant();
        }
        
        size_t size() const {
            return _inner.size();
        }
        
        template<typename J>
        void write(J&& j) {
            size_t h = hash(j);
            _inner._insert_or_assign(h, j);
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
        
        void clear() {
            _inner.clear();
        }
        
        template<typename Q>
        bool insert(Q&& q) {
            size_t h = hash(q);
            return _inner._insert(h, std::forward<Q>(q));
        }
        
    };
    
    template<typename K>
    void trace(const GCHashSet<K>& self) {
        trace(self._inner);
    }
    
    template<typename K>
    void shade(const GCHashSet<K>& self) {
        return shade(self._inner);
    }

    
    

} // namespace wry

#endif /* wry_gc_HeapTable_hpp */
