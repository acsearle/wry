//
//  table.hpp
//  client
//
//  Created by Antony Searle on 26/7/2023.
//

#ifndef table_hpp
#define table_hpp

#include "assert.hpp"
#include "algorithm.hpp"
#include "hash.hpp"
#include "memory.hpp"
#include "stdint.hpp"
#include "utility.hpp"
#include "with_capacity.hpp"

// TODO:
// - Unify with HeapTable
//   - Automatic choice of storage depending on trace/garbage_collected_shade behaviour of payload
// - Type trait: trivial gc behavior
// - Make all things incrementally resizing
// - Rename everything HashMap and HashSet
//

namespace wry {
    
    
    
    
    
    // hash map
    
    // BasicTable is concerned with probing and resizing
    //
    // the internal structure of slots, hashing and key comparison are
    // delegated to Entry and EntryService
    //
    // BasicTable uses Robin Hood hashing
    //
    // Entry must default construct to an empty state
    //
    // Examples:
    // - std::optional<Key, Value>
    //
    // - struct {
    //       uint64_t _hash;
    //       std::pair<Key, Value> _kv;
    //   };
    //
    // EntryService must
    // - determine if an Entry is empty
    // - compute a hash for Keylikes
    // - compute or retrive a stored hash for a nonempty Entry

    // we resize by two and therefore don't strive for a particularly high load
    // factor, because even if we permit full load, the median load will be
    // 71%, aka we're only slightly delaying resizes?
    
    // iteration order is effectively nondeterministic, depending on hasher and
    // on insertion order.  subranges make little sense; only operations on all
    // elements are sensible.  this is a danger point for desynchronization of
    // the game state.  for serialization we should move into a sorted container
    // first?

    // Just like we moved keymatching up to a predicate, can we replace the
    // EntryService object with an argument to calls that need it, or bake it into
    // a function passed to those?
    
    
    // Mask is OK, but shift is specific to a particular choice of index
    // generation which is not great.  Pointers, for example, might want to
    // be rotr64(x, 4) & _mask
        
    template<typename Entry, typename EntryService>
    struct BasicTable {
        
        using value_type = Entry;
        using iterator = value_type*;
        using const_iterator = const value_type*;
        using reference = value_type&;
        
        // an array of Entries
                
        [[no_unique_address]] EntryService _service;
        Entry* _entries;
        std::uint64_t _mask;
        int _shift;
        std::uint64_t _count;
        std::uint64_t _trigger;

        std::uint64_t count() const {
            return _count;
        }
        
        std::uint64_t size() const {
            return _mask + 1;
        }
        
        iterator begin() { return _entries; }
        iterator end() { return _entries + size(); }
        const_iterator begin() const { return _entries; }
        const_iterator end() const { return _entries + size(); }
        const_iterator cbegin() const { return _entries; }
        const_iterator cend() const { return _entries + size(); }
        
        void clear() noexcept {
            for (auto& e : *this)
                _service.clear_entry(e);
            _count = 0;
        }
        
        void swap(BasicTable& other) {
            using std::swap;
            swap(_service, other._service);
            swap(_entries, other._entries);
            swap(_mask, other._mask);
            swap(_shift, other._shift);
            swap(_count, other._count);
            swap(_trigger, other._trigger);
        }
                
        BasicTable(with_capacity_t, std::uint64_t capacity)
        : BasicTable() {
            if (capacity) {
                _shift = __builtin_clzll((capacity | 15) - 1);
                _mask = ((std::uint64_t) -1) >> _shift;
                _trigger = _mask ^ (_mask >> 3);
                _entries = (Entry*) ::operator new(sizeof(Entry) * size());
                _count = 0;
                std::uninitialized_value_construct_n(_entries, size());
            }
        }

        BasicTable()
        : _service()
        , _entries(nullptr)
        , _mask(-1)
        , _shift(61)
        , _count(0)
        , _trigger(0) {
        }
        
        BasicTable(BasicTable&& other)
        : BasicTable() {
            swap(other);
        }

        ~BasicTable() {
            std::destroy_n(_entries, size());
            ::operator delete(static_cast<void*>(_entries));
        }
        
        BasicTable& operator=(BasicTable&& other) {
            BasicTable(std::move(other)).swap(*this);
            return *this;
        }
                                        
        std::uint64_t _get_index(std::uint64_t h) const {
            // we index by the top bits so resize has a linear access pattern
            // TODO: but, this means we rely on the hash having good high bits
            // Make this a choice by the EntryService?
            return h >> _shift;
        }
        
        std::uint64_t _next_index(std::uint64_t i) const {
            return (i + 1) & _mask;
        }
        
        std::uint64_t _displacement(std::uint64_t desired, std::uint64_t actual) const {
            return (actual - desired) & _mask;
        }

        // Find any entry with hash h for which predicate is true
        Entry* find(std::uint64_t h, auto&& predicate) const {
            if (!_count)
                return nullptr;
            std::uint64_t ih = _get_index(h); // preferred index
            std::uint64_t i = ih;
            for (;;) {
                if (_service.entry_is_empty(_entries[i]))
                    return nullptr; // found vacancy
                std::uint64_t g = _service.get_hash(_entries[i]);
                assert(g);
                if ((g == h) && predicate(_entries[i]))
                    return _entries + i; // found a match
                std::uint64_t ig = _get_index(g);
                if (_displacement(ih, i) > _displacement(ig, i))
                    return nullptr; // the key would have evicted this entry
                i = _next_index(i); // try next element
            }
        }
                
        void _relocate_backward_from(std::uint64_t i) {
            // SAFETY: This is a relocate.  Explicit casts to void suppresses
            // warning for non-trivially copyable types
            assert((i <= _mask) && _entries[i]);
            // find next empty slot
            std::uint64_t j = i;
            do {
                j = _next_index(j);
                assert(j != i);
            } while (_entries[j]);
            std::destroy_at(_entries + j);
            if (j < i) {
                std::memmove((void*)(_entries + 1), _entries, j * sizeof(Entry));
                std::memcpy((void*)_entries, _entries + _mask, sizeof(Entry));
                j = _mask;
            }
            std::memmove((void*)(_entries + i + 1), _entries + i, (j - i) * sizeof(Entry));
            std::construct_at(_entries + i); // was relocated from and destroyed
        }
                        
        std::uint64_t _insert_uninitialized(std::uint64_t h, auto&& predicate) {
            if (_count == _trigger)
                resize();
            std::uint64_t ih = _get_index(h);
            std::uint64_t i = ih;
            for (;;) {
                if (_service.entry_is_empty(_entries[i])) {
                    ++_count;
                    return i;
                }
                std::uint64_t g = _service.get_hash(_entries[i]);
                if ((g == h) && predicate(_entries[i])) {
                    return i;
                }
                std::uint64_t ig = _get_index(g);
                if (_displacement(ih, i) > _displacement(ig, i)) {
                    _relocate_backward_from(i);
                    ++_count;
                    return i;
                }
                i = _next_index(i);
            }
        }
        
        void _relocate_forward_into(std::uint64_t i) {
            // SAFETY: This is a relocate.  Explicit casts to void suppresses
            // warning for non-trivially copyable types
            assert(i <= _mask);
            assert(_entries[i]);
            std::destroy_at(_entries + i); // will be relocated over
            std::uint64_t j = i, k;
            for (;;) {
                k = _next_index(j);
                if (!_entries[k])
                    break;
                std::uint64_t g = _service.get_hash(_entries[k]);
                if (k == _get_index(g))
                    break;
                j = k;
            }
            // now we have [i] to overwrite, (i, j] to move and [j] to zero
            if (j < i) {
                // we have wrapped
                std::memmove((void*)(_entries + i), _entries + i + 1, sizeof(Entry) * (_mask - i));
                std::memcpy((void*)(_entries + _mask), _entries, sizeof(Entry));
                i = 0;
            }
            std::memmove((void*)(_entries + i), _entries + i + 1, sizeof(Entry) * (j - i));
            std::construct_at(_entries + j);
        }
        
        std::size_t erase(std::uint64_t h, auto&& predicate) {
            Entry* p = find(h, predicate);
            if (!p)
                return 0;
            _relocate_forward_into(p - _entries);
            --_count;
            return 1;
        }
        
        void resize() {

            size_t n = size();
            
            Entry* first = _entries;
            Entry* last = _entries + n;
            
            n = n ? (n << 1) : 16;
            _entries = (Entry*) operator new(n * sizeof(Entry));
            _mask = n - 1;
            --_shift;
            assert((((uint64_t) -1) >> _shift) == _mask);
            _trigger = _mask ^ (_mask >> 3);
            assert(_count < _trigger);
            
            std::uninitialized_value_construct_n(_entries, n);
            
            for (Entry* p = first; p != last; ++p) {
                if (*p) {
                    uint64_t h = _service.get_hash(*p);
                    uint64_t ih = _get_index(h);
                    uint64_t j = ih;
                    while (_service.entry_is_nonempty(_entries[j])) {
                        uint64_t g = _service.get_hash(_entries[j]);
                        uint64_t ig = _get_index(g);
                        // there should be no duplicates in the input
                        // assert((g != h) || _hasher.key_compare(_entries[j], *p));
                        if (_displacement(ih, j) > _displacement(ig, j)) {
                            using std::swap;
                            swap(_entries[j], *p);
                            h = g;
                            ih = ig;
                        }
                        j = _next_index(j);
                    }
                    _entries[j] = std::move(*p);
                }
                std::destroy_at(p); // <-- relocate instead?
            }
            ::operator delete(static_cast<void*>(first));
        }
        
        void _invariant() const {
#ifndef NDEBUG
            assert(_count <= _trigger);
            if (_count == 0) {
                return;
            }
            assert(_trigger <= _mask);
            uint64_t i = 0;
            do {
                uint64_t j = _next_index(i);
                if (_service.entry_is_nonempty(_entries[j])) {
                    uint64_t g = _service.get_hash(_entries[j]);
                    uint64_t ig = _get_index(g);
                    if (_service.entry_is_nonempty(_entries[i])) {
                        uint64_t h = _service.get_hash(_entries[i]);
                        uint64_t ih = _get_index(h);
                        // if the slot before is occupied, we must not be
                        // entitled to it, i.e. we can't improve the average
                        // displacement by swapping the elements
                        auto d1 = _displacement(ih, i) + _displacement(ig, j);
                        auto d2 = _displacement(ig, i) + _displacement(ih, j);
                        assert(d1 <= d2);
                    } else {
                        // if the slot before is vacant, we must be in our
                        // preferred position
                        assert(j == ig);
                    }
                }
                i = j;
            } while(i);
#endif
        }
        
        std::uint64_t total_displacement() const {
            std::uint64_t n = 0;
            for (std::uint64_t i = 0; i != size(); ++i) {
                if (_service.entry_is_nonempty(_entries[i])) {
                    std::uint64_t h = _service.get_hash(_entries[i]);
                    // printf("displaced %llu\n", _displacement(_get_index(h), i));
                    n += _displacement(_get_index(h), i);
                }
            }
            return n;
        }
                
    };
    

    // TODO: parameter EntryService
    template<typename Key, typename T>
    struct Table {
        
        // in the general case, we can't steal states from Key or T to indicate
        // a slot is unoccupied, so we need a discriminant, and unless Key or T
        // are tiny, the discriminant might as well be the pointer-sized hash
        // though only one hash value counts as empty, we set the lsb as the
        // simplest way to guarantee the hash is always nonzero.  this won't
        // affect the slot lookup and doubles the negligable rate of false
        // hash matches that go to key matching.  this rate will be something
        // 1 in 2^64 random hash collision, factor of 2 from or, factor of
        // capacity() from the fact that we are comparing them because they
        // are in (or near) the same slot because their high bits are similar
        // like 2^(-64 + 1 + (64 - shift)) = 2^(1-shift)?, aka negligable since
        // the hash table will occupy all addressable memoy before shift < 20
        
        
        struct Entry {
            
            Entry()
            : _hash(0) {
            }
            
            Entry(const Entry& other)
            : _hash(other._hash) {
                if (_hash)
                    std::construct_at(&_kv, other._kv);
            }
            
            Entry(Entry&& other)
            : _hash(other._hash) {
                if (_hash)
                    std::construct_at(&_kv, std::move(other._kv));
            }
            
            ~Entry() {
                if (_hash)
                    std::destroy_at(&_kv);
            }
                        
            Entry& operator=(Entry&& other) {
                assert(other._hash);
                if (_hash) {
                    if (other._hash) {
                        _kv = std::move(other._kv);
                    } else {
                        std::destroy_at(&_kv);
                    }
                } else {
                    if (other._hash) {
                        std::construct_at(&_kv, std::move(other._kv));
                    } else {
                        // no-op
                    }
                }
                _hash = other._hash;
                return *this;
            }
            
            std::uint64_t _hash;
            union {
                std::pair<Key, T> _kv;
            };
            
            bool operator!() const {
                return !_hash;
            }
            
            explicit operator bool() const {
                return static_cast<bool>(_hash);
            }

            void clear() {
                if (_hash) {
                    _hash = 0;
                    std::destroy_at(&_kv);
                }
            }

        };
        
        struct EntryService {
                        
            std::uint64_t get_hash(const Entry& e) const {
                return e._hash;
            }
            
            std::uint64_t get_hash(const auto& keylike) const {
                return hash(keylike) | 1;
            }
            
            template<typename K, typename U>
            std::uint64_t get_hash(const std::pair<K, U>& valuelike) const {
                return get_hash(valuelike.first);
            }

            void clear_entry(Entry& e) const {
                e.clear();
            }

            bool entry_is_empty(Entry const& e) const {
                return !e;
            }

            bool entry_is_nonempty(Entry const& e) const {
                return (bool)e;
            }


        };
        
        using value_type = std::pair<const Key, T>;
        using reference = value_type&;
        using pointer = value_type*;
        using const_reference = const value_type&;
        using const_pointer = const value_type*;

        struct iterator {
            
            Entry* _pointer;
            BasicTable<Entry, EntryService>* _context;
            
            Entry* _begin() {
                return _context->begin();
            }
            
            Entry* _end() {
                return _context->end();
            }
            
            void _advance() {
                while ((_pointer != _end()) && (!*_pointer))
                    ++_pointer;

            }
            
            void _retreat() {
                do {
                    --_pointer;
                } while (!*_pointer);
            }
            
            iterator() = default;
            
            iterator(Entry* b, BasicTable<Entry, EntryService>* c)
            : _pointer(b)
            , _context(c) {
            }
            
            iterator& operator++() {
                assert(_pointer != _end());
                ++_pointer;
                _advance();
                return *this;
            }

            iterator& operator--() {
                _retreat();
                return *this;
            }

            reference operator*() const {
                return reinterpret_cast<reference>(_pointer->_kv);
            }

            pointer operator->() const {
                return (pointer) &(_pointer->_kv);
            }
            
            bool operator==(const iterator& other) const {
                assert(_context == other._context);
                return _pointer == other._pointer;
            }
            
            auto operator<=>(const iterator& other) const {
                assert(_context == other._context);
                return _pointer <=> other._pointer;
            }
            
        };
        
        
        struct const_iterator {
            
            const Entry* _pointer;
            const BasicTable<Entry, EntryService>* _context;
            
            const Entry* _begin() {
                return _context->begin();
            }
            
            const Entry* _end() {
                return _context->end();
            }
            
            void _advance() {
                while ((_pointer != _end()) && !*_pointer)
                    ++_pointer;
                
            }
            
            void _retreat() {
                do {
                    --_pointer;
                } while (!*_pointer);
            }
            
            const_iterator(const Entry* b, const BasicTable<Entry, EntryService>* c)
            : _pointer(b)
            , _context(c) {
            }
            
            const_iterator& operator++() {
                assert(_pointer != _end());
                ++_pointer;
                _advance();
                return *this;
            }
            
            const_iterator& operator--() {
                _retreat();
                return *this;
            }
            
            const_reference operator*() const {
                return reinterpret_cast<const_reference>(_pointer->_kv);
            }
            
            const_pointer operator->() const {
                return &reinterpret_cast<const_reference>(_pointer->_kv);
            }
            
            bool operator==(const const_iterator& other) const {
                assert(_context == other._context);
                return _pointer == other._pointer;
            }
            
            auto operator<=>(const const_iterator& other) const {
                assert(_context == other._context);
                return _pointer <=> other._pointer;
            }

            bool operator==(const iterator& other) const {
                assert(_context == other._context);
                return _pointer == other._pointer;
            }
            
            auto operator<=>(const iterator& other) const {
                assert(_context == other._context);
                return _pointer <=> other._pointer;
            }

        };
        
        
    
        
        BasicTable<Entry, EntryService> _inner;
        
        Table& swap(Table& other) {
            _inner.swap(other._inner);
            return other;
        }
        
        Table() = default;
        
        Table(with_capacity_t, std::size_t count)
        : _inner(with_capacity, count) {
        }
        
        Table(Table&&) = default;
        
        Table(const Table& other)
        : _inner(with_capacity, other.size()) {
            for (const auto& a : other) {
                insert(a);
            }
        }
        
        ~Table() = default;
        
        Table& operator=(const Table& other) {
            return Table(other).swap(*this);
        }

        Table& operator=(Table&& other) {
            return Table(std::move(other)).swap(*this);
        }

        iterator begin() {
            iterator it{_inner.begin(), &_inner};
            it._advance();
            return it;
        }
        
        iterator end() {
            return iterator(_inner.end(), &_inner);
        }

        const_iterator begin() const {
            const_iterator it{_inner.begin(), &_inner};
            it._advance();
            return it;
        }
        
        const_iterator end() const {
            return const_iterator(_inner.end(), &_inner);
        }

        bool empty() const {
            return !size();
        }
        
        std::size_t size() const {
            return _inner.count();
        }
        
        void clear() {
            _inner.clear();
        }

        Entry* _to(auto&& keylike) const {
            return _inner.find(_inner._service.get_hash(keylike),
                                   [&](const Entry& e) {
                return e._kv.first == keylike;
            });
        }

        Entry const* to(auto&& keylike) const {
            return _to(FORWARD(keylike));
        }

        Entry* to(auto&& keylike) {
            return _to(FORWARD(keylike));
        }

        const_iterator find(auto&& keylike) const {
            auto p = to(FORWARD(keylike));
            return const_iterator((p ? p : _inner.end()), &_inner);
        }

        iterator find(auto&& keylike) {
            auto p = to(FORWARD(keylike));
            return iterator((p ? p : _inner.end()), &_inner);
        }

        std::pair<iterator, bool> emplace(auto&& key, auto&& value) {
            std::uint64_t h = _inner._service.get_hash(key);
            std::uint64_t i = _inner._insert_uninitialized(h, [&key](Entry& e) {
                return e._kv.first == key;
            });
            Entry* p = _inner._entries + i;
            if (p->_hash) {
                return {iterator{p, &_inner}, false};
            } else {
                p->_hash = h;
                std::construct_at(&(p->_kv),
                                  std::forward<decltype(key)>(key),
                                  std::forward<decltype(value)>(value));
                return {iterator{p, &_inner}, true};
            }
        }

        std::pair<iterator, bool> insert(auto&& value) {
            std::uint64_t h = _inner._service.get_hash(value);
            std::uint64_t i = _inner._insert_uninitialized(h,
                                                           [&](Entry& e) {
                return e._kv.first == value.first;
            });
            Entry* p = _inner._entries + i;
            if (p->_hash) {
                return {iterator{p, &_inner}, false};
            } else {
                p->_hash = h;
                std::construct_at(&(p->_kv), std::forward<decltype(value)>(value));
                return {iterator{p, &_inner}, true};
            }
        }
        
        template<typename InputIt>
        void insert(InputIt first, InputIt last) {
            for (; first != last; ++first)
                insert(*first);
        }

        std::pair<iterator, bool> insert_or_assign(auto&& k, auto&& v) {
            std::uint64_t h = _inner._service.get_hash(k);
            std::uint64_t i = _inner._insert_uninitialized(h,
                                                           [&](Entry& e) {
                return e._kv.first == k;
            });
            Entry* p = _inner._entries + i;
            if (p->_hash) {
                p->_kv.first = std::forward<decltype(k)>(k);
                p->_kv.second = std::forward<decltype(v)>(v);
                return {iterator{p, &_inner}, false};
            } else {
                p->_hash = h;
                std::construct_at(&(p->_kv),
                                  std::forward<decltype(k)>(k),
                                  std::forward<decltype(v)>(v));
                return {iterator{p, &_inner}, true};
            }
        }
        
        std::size_t erase(iterator pos) {
            assert(pos._pointer->_hash);
            _inner._relocate_forward_into(pos._pointer - _inner._entries);
            --_inner._count;
            return 1;
        }
        
        // range erase makes no sense for unordered map
        
        std::size_t erase(const auto& keylike) {
            return _inner.erase(_inner._service.get_hash(keylike),
                                [&](const Entry& e) {
                return e._kv.first == keylike;
            });
        }
        
        T& operator[](auto&& keylike) {
            std::uint64_t h = _inner._service.get_hash(keylike);
            std::uint64_t i = _inner._insert_uninitialized(h, [&](const Entry& e) {
                return e._kv.first == keylike;
            });
            Entry* p = _inner._entries + i;
            if (!(p->_hash)) {
                p->_hash = h;
                std::construct_at(&(p->_kv),
                                  std::piecewise_construct,
                                  std::forward_as_tuple(FORWARD(keylike)),
                                  std::tuple<>());
            }
            return p->_kv.second;
        }
        
        const T& at(auto&& keylike) const {
            Entry const* p = to(FORWARD(keylike));
            assert(p);
            return p->_kv.second;
        }

        T& at(auto&& keylike) {
            Entry* p = to(FORWARD(keylike));
            assert(p);
            return p->_kv.second;
        }

        std::size_t count(auto&& keylike) const {
            return to(FORWARD(keylike)) ? 1 : 0;
        }
        
        bool contains(auto&& keylike) const {
            return (bool)to(FORWARD(keylike));
        }

    }; // struct Table<Key, T>

} // namespace wry

#endif /* table_hpp */
