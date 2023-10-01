//
//  table.hpp
//  client
//
//  Created by Antony Searle on 26/7/2023.
//

#ifndef table_hpp
#define table_hpp

#include <cassert>
#include <compare>
#include <cstdint>
#include <map>

#include "with_capacity.hpp"
#include "hash.hpp"

namespace wry {
    
    // hash map
    
    // basic_table is concerned with probing and resizing
    //
    // the internal structure of slots, hashing and key comparison are
    // delegated to Entry and Hasher
    //
    // basic_table uses Robin Hood hashing
    //
    // Entry must be convertible to bool to indicate occupancy, and default
    // construct to an empty state
    //
    // Examples:
    // - std::optional<Key, Value>
    //
    // - struct {
    //       uint64_t _hash;
    //       std::pair<Key, Value> _kv;
    //       explicit operator bool() const { return static_cast<bool>(_hash); }
    //   };
    //
    // Hasher must be able to compute a hash for Keylikes and compute or retrive
    // a stored hash for an Entry
    
    // we resize by two and therefore don't strive for a particularly high load
    // factor, because even if we permit full load, the median load will be
    // 71%, aka we're only slightly delaying resizes?
    
    // iteration order is effectively nondeterministic, depending on hasher and
    // on insertion order.  subranges make little sense; only operations on all
    // elements are sensible.  this is a danger point for desynchronization of
    // the game state.  for serialization we should move into a sorted container
    // first?
        
    template<typename Entry, typename Hasher>
    struct basic_table {
        
        using value_type = Entry;
        using iterator = value_type*;
        using const_iterator = const value_type*;
        using reference = value_type&;
        
        // an array of Entries
                
        Hasher _hasher;
        Entry* _begin;
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
        
        iterator begin() { return _begin; }
        iterator end() { return _begin + size(); }
        const_iterator begin() const { return _begin; }
        const_iterator end() const { return _begin + size(); }
        const_iterator cbegin() const { return _begin; }
        const_iterator cend() const { return _begin + size(); }
        
        void clear() noexcept {
            for (auto& e : *this) {
                if (e) {
                    std::destroy_at(&e);
                    std::construct_at(&e);
                }
            }
            _count = 0;
        }
        
        void swap(basic_table& other) {
            using std::swap;
            swap(_hasher, other._hasher);
            swap(_begin, other._begin);
            swap(_mask, other._mask);
            swap(_shift, other._shift);
            swap(_count, other._count);
            swap(_trigger, other._trigger);
        }
                
        basic_table(with_capacity_t, std::uint64_t capacity)
        : basic_table() {
            if (capacity) {
                _shift = __builtin_clzll((capacity | 15) - 1);
                _mask = ((std::uint64_t) -1) >> _shift;
                _trigger = _mask ^ (_mask >> 3);
                _begin = ::operator new(sizeof(Entry) * size());
                _count = 0;
                std::uninitialized_value_construct_n(_begin, size());
            }
        }

        basic_table()
        : _hasher()
        , _begin(nullptr)
        , _mask(-1)
        , _shift(61)
        , _count(0)
        , _trigger(0) {
        }
        
        basic_table(basic_table&& other)
        : basic_table() {
            swap(other);
        }

        ~basic_table() {
            std::destroy_n(_begin, size());
            ::operator delete(static_cast<void*>(_begin));
        }
        
        basic_table& operator=(basic_table&& other) {
            basic_table(std::move(other)).swap(*this);
            return *this;
        }
                                        
        std::uint64_t _get_index(std::uint64_t h) const {
            // we index by the top bits so resize has a linear access pattern
            return h >> _shift;
        }
        
        std::uint64_t _next_index(std::uint64_t i) const {
            return (i + 1) & _mask;
        }
        
        std::uint64_t _displacement(std::uint64_t desired, std::uint64_t actual) const {
            return (actual - desired) & _mask;
        }
        
        Entry* find(std::uint64_t h, auto&& predicate) const {
            if (!_count)
                return nullptr;
            std::uint64_t ih = _get_index(h); // ideal
            std::uint64_t i = ih;
            for (;;) {
                if (!_begin[i])
                    return nullptr; // found vacancy
                std::uint64_t g = _hasher.get_hash(_begin[i]);
                if ((g == h) && predicate(_begin[i]))
                    return _begin + i; // found exact match
                std::uint64_t ig = _get_index(g);
                if (_displacement(ih, i) > _displacement(ig, i))
                    return nullptr; // the key would have evicted this entry
                i = _next_index(i); // try next element
            }
        }
                
        void _relocate_backward_from(std::uint64_t i) {
            assert((i <= _mask) && _begin[i]);
            // find next empty slot
            std::uint64_t j = i;
            do {
                j = _next_index(j);
                assert(j != i);
            } while (_begin[j]);
            std::destroy_at(_begin + j); // something will relocate onto it
            if (j < i) {
                std::memmove(_begin + 1, _begin, j * sizeof(Entry));
                std::memcpy(_begin, _begin + _mask, sizeof(Entry));
                j = _mask;
            }
            std::memmove(_begin + i + 1, _begin + i, (j - i) * sizeof(Entry));
            std::construct_at(_begin + i); // was relocated from and destroyed
        }
                        
        std::uint64_t _insert_uninitialized(std::uint64_t h, auto&& predicate) {
            if (_count == _trigger)
                resize();
            std::uint64_t ih = _get_index(h);
            std::uint64_t i = ih;
            for (;;) {
                if (!_begin[i]) {
                    ++_count;
                    return i;
                }
                std::uint64_t g = _hasher.get_hash(_begin[i]);
                if ((g == h) && predicate(_begin[i])) {
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
            assert(i <= _mask);
            assert(_begin[i]);
            std::destroy_at(_begin + i); // will be relocated over
            std::uint64_t j = i, k;
            for (;;) {
                k = _next_index(j);
                if (!_begin[k])
                    break;
                std::uint64_t g = _hasher.get_hash(_begin[k]);
                if (k == _get_index(g))
                    break;
                j = k;
            }
            // now we have [i] to overwrite, (i, j] to move and [j] to zero
            if (j < i) {
                // we have wrapped
                std::memmove(_begin + i, _begin + i + 1, sizeof(Entry) * (_mask - i));
                std::memcpy(_begin + _mask, _begin, sizeof(Entry));
                i = 0;
            }
            std::memmove(_begin + i, _begin + i + 1, sizeof(Entry) * (j - i));
            std::construct_at(_begin + j);
        }
        
        std::size_t erase(std::uint64_t h, auto&& predicate) {
            Entry* p = find(h, predicate);
            if (!p)
                return 0;
            _relocate_forward_into(p - _begin);
            --_count;
            return 1;
        }
        
        void resize() {
            
            // we could change hash function every resize?
            
            size_t n = size();
            
            Entry* first = _begin;
            Entry* last = _begin + n;
            
            n = n ? (n << 1) : 16;
            _begin = (Entry*) operator new(n * sizeof(Entry));
            _mask = n - 1;
            --_shift;
            assert((((uint64_t) -1) >> _shift) == _mask);
            _trigger = _mask ^ (_mask >> 3);
            assert(_count < _trigger);
            
            std::uninitialized_value_construct_n(_begin, n);
            
            for (Entry* p = first; p != last; ++p) {
                if (*p) {
                    uint64_t h = _hasher.get_hash(*p);
                    uint64_t ih = _get_index(h);
                    uint64_t j = ih;
                    while (_begin[j]) {
                        uint64_t g = _hasher.get_hash(_begin[j]);
                        uint64_t ig = _get_index(g);
                        // there should be no duplicates in the input
                        // assert((g != h) || _hasher.key_compare(_begin[j], *p));
                        if (_displacement(ih, j) > _displacement(ig, j)) {
                            using std::swap;
                            swap(_begin[j], *p);
                            h = g;
                            ih = ig;
                        }
                        j = _next_index(j);
                    }
                    _begin[j] = std::move(*p);
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
                if (_begin[j]) {
                    uint64_t g = _hasher.get_hash(_begin[j]);
                    uint64_t ig = _get_index(g);
                    if (_begin[i]) {
                        uint64_t h = _hasher.get_hash(_begin[i]);
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
        
        std::uint64_t total_displacement() {
            std::uint64_t n = 0;
            for (std::uint64_t i = 0; i != size(); ++i) {
                if (_begin[i]) {
                    std::uint64_t h = _hasher.get_hash(_begin[i]);
                    // printf("displaced %llu\n", _displacement(_get_index(h), i));
                    n += _displacement(_get_index(h), i);
                }
            }
            return n;
        }
                
    };
    
    
    template<typename Key, typename T>
    struct table {
        
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
                        //
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
                        
        };
        
        struct Hasher {
                        
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
            

        };
        
        using value_type = std::pair<const Key, T>;
        using reference = value_type&;
        using pointer = value_type*;
        using const_reference = const value_type&;
        using const_pointer = const value_type*;

        struct iterator {
            
            Entry* _pointer;
            basic_table<Entry, Hasher>* _context;
            
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
            
            iterator(Entry* b, basic_table<Entry, Hasher>* c)
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
            const basic_table<Entry, Hasher>* _context;
            
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
            
            const_iterator(const Entry* b, const basic_table<Entry, Hasher>* c)
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
        
        
    
        
        basic_table<Entry, Hasher> _inner;
        
        table() = default;
        table(with_capacity_t, std::size_t count)
        : _inner(with_capacity, count) {
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
        
        const_iterator find(const auto& keylike) const {
            Entry* p = _inner.find(_inner._hasher.get_hash(keylike),
                                   [&](const Entry& e) {
                return e._kv.first == keylike;
            });
            return const_iterator((p ? p : _inner.end()), &_inner);
        }

        iterator find(const auto& keylike) {
            Entry* p = _inner.find(_inner._hasher.get_hash(keylike),
                                   [&](const Entry& e) -> bool {
                return e._kv.first == keylike;
            });
            return iterator((p ? p : _inner.end()), &_inner);
        }

        std::pair<iterator, bool> emplace(auto&& key, auto&& value) {
            std::uint64_t h = _inner._hasher.get_hash(key);
            std::uint64_t i = _inner._insert_uninitialized(h, [&key](Entry& e) {
                return e._kv.first == key;
            });
            Entry* p = _inner._begin + i;
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
            std::uint64_t h = _inner._hasher.get_hash(value);
            std::uint64_t i = _inner._insert_uninitialized(h,
                                                           [&](Entry& e) {
                return e._kv.first == value.first;
            });
            Entry* p = _inner._begin + i;
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
            std::uint64_t h = _inner._hasher.get_hash(k);
            std::uint64_t i = _inner._insert_uninitialized(h,
                                                           [&](Entry& e) {
                return e._kv.first == k;
            });
            Entry* p = _inner._begin + i;
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
            _inner._relocate_backward_from(pos._pointer - _inner._begin);
            return 1;
        }
        
        // range erase makes no sense for unordered map
        
        std::size_t erase(const auto& keylike) {
            return _inner.erase(_inner._hasher.get_hash(keylike),
                                [&](const Entry& e) {
                return e._kv.first == keylike;
            });
        }
        
        T& operator[](auto&& key) {
            std::uint64_t h = _inner._hasher.get_hash(key);
            std::uint64_t i = _inner._insert_uninitialized(h, [&key](const Entry& e) {
                return e._kv.first == key;
            });
            Entry* p = _inner._begin + i;
            if (!(p->_hash)) {
                p->_hash = h;
                std::construct_at(&(p->_kv),
                                  std::piecewise_construct,
                                  std::forward_as_tuple(std::forward<decltype(key)>(key)),
                                  std::tuple<>());
            }
            return p->_kv.second;
        }
        
        const T& at(auto&& key) const {
            Entry* p = _inner.find(_inner._hasher.get_hash(key),
                                   [&key](const Entry& e) {
                return e._kv.first == key;
            });
            assert(p);
            return p->_kv.second;
        }

        T& at(auto&& key) {
            Entry* p = _inner.find(_inner._hasher.get_hash(key),
                                   [&key](const Entry& e) {
                return e._kv.first == key;
            });
            assert(p);
            return p->_kv.second;
        }

        std::size_t count(auto&& k) {
            return _inner.find(k) ? 1 : 0;
        }
        
        bool contains(auto&& key) {
            return _inner.find(_inner._hasher.get_hash(key),
                               [&key](const Entry& e) {
                return e._kv.first == key;
            });
        }
    };
    
    
    
    
    
    
    
    
    
    template<typename Key>
    struct hash_set {
        
       
        struct Entry {
            
            std::uint64_t _hash;
            union {
                Key _key;
            };

            
            Entry()
            : _hash(0) {
            }
            
            Entry(const Entry& other)
            : _hash(other._hash) {
                if (_hash)
                    std::construct_at(&_key, other._key);
            }
            
            Entry(Entry&& other)
            : _hash(other._hash) {
                if (_hash)
                    std::construct_at(&_key, std::move(other._key));
            }
            
            ~Entry() {
                if (_hash)
                    std::destroy_at(&_key);
            }
            
            Entry& operator=(Entry&& other) {
                assert(other._hash);
                if (_hash) {
                    if (other._hash) {
                        _key = std::move(other._key);
                    } else {
                        std::destroy_at(&_key);
                    }
                } else {
                    if (other._hash) {
                        std::construct_at(&_key, std::move(other._key));
                    } else {
                        //
                    }
                }
                _hash = other._hash;
                return *this;
            }
                        
            bool operator!() const {
                return !_hash;
            }
            
            explicit operator bool() const {
                return static_cast<bool>(_hash);
            }
            
        };
        
        struct Hasher {
            
            std::uint64_t get_hash(const Entry& e) const {
                return e._hash;
            }
            
            std::uint64_t get_hash(const auto& keylike) const {
                return hash(keylike) | 1;
            }
                        
        };
        
        using value_type = const Key;
        using reference = value_type&;
        using pointer = value_type*;
        using const_reference = const value_type&;
        using const_pointer = const value_type*;
        
        struct iterator {
            
            Entry* _pointer;
            basic_table<Entry, Hasher>* _context;
            
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
            
            iterator(Entry* b, basic_table<Entry, Hasher>* c)
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
            }
            
            reference operator*() const {
                return reinterpret_cast<reference>(_pointer->_key);
            }
            
            pointer operator->() const {
                return &(_pointer->_key);
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
            const basic_table<Entry, Hasher>* _context;
            
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
            
            const_iterator(const Entry* b, const basic_table<Entry, Hasher>* c)
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
            }
            
            const_reference operator*() const {
                return reinterpret_cast<const_reference>(_pointer->_key);
            }
            
            const_pointer operator->() const {
                return &reinterpret_cast<const_reference>(_pointer->_key);
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
        
        
        basic_table<Entry, Hasher> _inner;
        
        hash_set() = default;
        hash_set(with_capacity_t, std::size_t count)
        : _inner(with_capacity, count) {
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
        
        const_iterator find(const auto& keylike) const {
            Entry* p = _inner.find(_inner._hasher.get_hash(keylike),
                                   [&](const Entry& e) {
                return e._key == keylike;
            });
            return const_iterator((p ? p : _inner.end()), &_inner);
        }
        
        std::pair<iterator, bool> emplace(auto&& key) {
            std::uint64_t h = _inner._hasher.get_hash(key);
            std::uint64_t i = _inner._insert_uninitialized(h, [&key](Entry& e) {
                return e._key == key;
            });
            Entry* p = _inner._begin + i;
            if (p->_hash) {
                return {iterator{p, &_inner}, false};
            } else {
                p->_hash = h;
                std::construct_at(&(p->_kv),
                                  std::forward<decltype(key)>(key));
                return {iterator{p, &_inner}, true};
            }
        }
        
        
        std::pair<iterator, bool> insert(auto&& key) {
            std::uint64_t h = _inner._hasher.get_hash(key);
            std::uint64_t i = _inner._insert_uninitialized(h,
                                                           [&](Entry& e) {
                return e._key == key;
            });
            Entry* p = _inner._begin + i;
            if (p->_hash) {
                return {iterator{p, &_inner}, false};
            } else {
                p->_hash = h;
                std::construct_at(&(p->_key), std::forward<decltype(key)>(key));
                return {iterator{p, &_inner}, true};
            }
        }
        
        template<typename InputIt>
        void insert(InputIt first, InputIt last) {
            for (; first != last; ++first)
                insert(*first);
        }
        
        std::pair<iterator, bool> insert_or_assign(auto&& key) {
            std::uint64_t h = _inner._hasher.get_hash(key);
            std::uint64_t i = _inner._insert_uninitialized(h,
                                                           [&](Entry& e) {
                return e._key == key;
            });
            Entry* p = _inner._begin + i;
            if (p->_hash) {
                p->_key = std::forward<decltype(key)>(key);
                return {iterator{p, &_inner}, false};
            } else {
                p->_hash = h;
                std::construct_at(&(p->_key),
                                  std::forward<decltype(key)>(key));
                return {iterator{p, &_inner}, true};
            }
        }
        
        
        
        
        std::size_t erase(iterator pos) {
            _inner._relocate_backward_from(pos._pointer - _inner._begin);
        }
        
        // range erase makes no sense for unordered map
        
        std::size_t erase(const auto& keylike) {
            return _inner.erase(_inner._hasher.get_hash(keylike),
                                [&](const Entry& e) {
                return e._kv.first == keylike;
            });
        }
        
        
        const Key& operator[](auto&& key) {
            std::uint64_t h = _inner._hasher.get_hash(key);
            std::uint64_t i = _inner._insert_uninitialized(h, [&key](const Entry& e) {
                return e._kv.first == key;
            });
            Entry* p = _inner._begin + i;
            if (!(p->_hash)) {
                p->_hash = h;
                std::construct_at(&(p->_kv),
                                  std::piecewise_construct,
                                  std::forward_as_tuple(std::forward<decltype(key)>(key)),
                                  std::tuple<>());
            }
            return p->_kv.second;
        }
        
        const Key& at(auto&& key) const {
            Entry* p = _inner.find(_inner._hasher.get_hash(key),
                                   [&key](const Entry& e) {
                return e._kv.first == key;
            });
            assert(p);
            return p->_kv.second;
        }
        
        Key& at(auto&& key) {
            Entry* p = _inner.find(_inner._hasher.get_hash(key),
                                   [&key](const Entry& e) {
                return e._kv.first == key;
            });
            assert(p);
            return p->_kv.second;
        }
        
        std::size_t count(auto&& key) {
            return _inner.find(key) ? 1 : 0;
        }
        
        bool contains(auto&& key) {
            return _inner.find(_inner._hasher.get_hash(key),
                               [&key](const Entry& e) {
                return e._key == key;
            });
        }
    };
    
    
    
    
    
} // namespace wry

#endif /* table_hpp */
