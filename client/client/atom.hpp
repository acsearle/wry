//
//  atom.hpp
//  client
//
//  Created by Antony Searle on 25/1/2024.
//

#ifndef atom_hpp
#define atom_hpp

#include "stdint.hpp"
#include "string.hpp"
#include "utility.hpp"

namespace wry::atom {
    
    // Not to be confused with atomic
    //
    // Maps registered strings to 64 bit values suitable for direct use in hash
    // tables; atom equality implies string equality, and strings can be looked
    // up from atoms.  However, the strings must be registered; this is not
    // a hash of their contents, and at most 2**64-1 strings can be registered
    // (though memory is exhausted first).  A hash table built on atoms can
    // regard them as implementing a perfect hash, with no need for collision
    // resolution, and plenty of entropy.
    //
    // The actual values of atoms will depend on the order in which strings are
    // registered, and thus may be non-determinisic across runs
    //
    // Compare to a smart enum
    //
    // As the atom registry is never garbage collected, unbounded numbers of
    // dynamically generated strings should not be registered.
    //
    // Compare weak dictionary
    //
    // An atom is a 64-bit value
    // high entropy
    // injective(?)
    // perfect hash
    // empty zero state
    
    // What do atoms save us?
    //
    // In a conventional hash table with a string key, we must:
    //
    // - (maybe) walk the string to hash it
    // - load the slot for the hash
    // - compare the found hash
    //   - compare the found hash with zero, go to the next slot
    // - walk both strings to compare them
    
    // which is three random accesses (the slot, two strings at least once each)
    
    // With an atom key
    // - load slot
    // - compare the found atom
    //   - compare zero, go to next slot
    // - match, done
    
    // which is one random access
    
    // If we can constexpr hash string literals, we do gain determinism
    // If we can constexpr perfect hash string literals, we get the best of
    // all worlds.  The first collision would be expected at about 2^32 strings,
    // Hairy though
    
    
    // The string pointer is itself a unique value, albeit one with unknown
    // entropy.  We could use it in a hash table only if we defensively hash it
    // for index generation, but it would be OK otherwise.  We could thus have
    // a hash set of interned strings with zero overhead.
    
    // We could also do some kind of horrific scheme to intern the strings
    // inside a fixed chunk of memory at high-entropy addresses
    
    struct Atom {

        u64 data;
        
        const char* to_string() const;
        
        static Atom from_string(const char*);
        Atom from_string(StringView s);
        
        explicit operator bool() const {
            return static_cast<bool>(data);
        }
        
        bool operator!() const {
            return !data;
        }
        
        void clear() {
            data = 0;
        }
        
        bool empty() const {
            return !data;
        }
        
        bool operator==(const Atom&) const = default;
        
    };
    
    
    // The AtomMap exploits the fact that Atoms are identity-hashed and that
    // zero is available as an empty slot marker
    
    // TODO: can we unify this with basic hash table?
    // TODO: AtomSet
        
    template<typename T>
    struct AtomMap {
        
        using Self = AtomMap;
        
        using key_type = Atom;
        using mapped_type = T;
        using value_type = std::pair<const Atom, T>;
        using size_type = wry::size_type;
        using difference_type = wry::difference_type;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = value_type*;
        using const_pointer = const value_type*;
        struct iterator;
        struct const_iterator;
        
        struct Slot {
            Atom first;
            union {
                T second;
            };
        };
        
        struct iterator {
            
            using Self = iterator;
            
            using difference_type = AtomMap::difference_type;
            using value_type = AtomMap::value_type;
            using pointer = AtomMap::pointer;
            using reference = AtomMap::reference;
            using iterator_category = std::forward_iterator_tag;
            
            Slot* base;
            Slot* sentinel;
            
            iterator(Slot* first, Slot* last)
            : base(first), sentinel(last) {
                while ((base != sentinel) && !base->first)
                    ++base;
            }
            
            Self operator++(int) {
                Self result(*this);
                ++*this;
                return result;
            }

            Self& operator++() {
                assert(base != sentinel);
                do {
                    ++base;
                } while ((base != sentinel) && !base->first);
            }

            value_type& operator*() const {
                assert((base != sentinel) && base->first);
                return reinterpret_cast<value_type&>(*base);
            }
            
            bool operator!=(const Self& other) const {
                return base != other.base;
            }

        };
        
        struct const_iterator {
            
            using Self = const_iterator;
            
            using difference_type = AtomMap::difference_type;
            using value_type = const AtomMap::value_type;
            using pointer = AtomMap::const_pointer;
            using reference = AtomMap::const_reference;
            using iterator_category = std::forward_iterator_tag;
            
            const Slot* base;
            const Slot* sentinel;
            
            const_iterator(const Slot* first, const Slot* last)
            : base(first), sentinel(last) {
                while ((base != sentinel) && !base->first)
                    ++base;
            }
            
            Self operator++(int) {
                Self result(*this);
                ++*this;
                return result;
            }
            
            Self& operator++() {
                assert(base != sentinel);
                do {
                    ++base;
                } while ((base != sentinel) && !base->first);
            }
            
            Self& operator*() const {
                assert((base != sentinel) && base->first);
                return reinterpret_cast<value_type&>(*base);
            }
            
            bool operator!=(const Self& other) const {
                return base != other.base;
            }
            
        };
        
        
        
        Slot* _begin; // allocation of 2**n elements
        u64 _mask;    // 2**n - 1
        int _shift;   // 64 - n
        size_type _size;   // occupied elements, < 2**n
        size_type _trigger; // resize threshold

        
        
        void _invariant() {
            auto n = _mask + 1;
            // non null allocation iff non zero capacity
            assert((bool) _begin == (bool) n);
            assert(!(_mask & n));
            assert((size_type) -1 >> _shift == _mask);
            assert(_size <= n);
            assert(_size < _trigger);
        }

        
        
        Slot* _end() const { return _begin + _slots(); }
        size_type _slots() const { return _mask + 1; }

        u64 _get_index(Atom key) const { return key.data >> _shift; }
        u64 _next_index(u64 index) const { return (index + 1) & _mask; }

        iterator _to(u64 index) { return iterator(_begin + index, _end()); }
        const_iterator _to(u64 index) const { return const_iterator(_begin + index, _end()); }
        const_iterator _cto(u64 index) const { return _to(index); }
        
        // Robin Hood uses displacement from preferred location to redistribute
        // slots to reduce average probe length
        u64 _displacement(u64 desired_index, u64 actual_index) const {
            return (actual_index - desired_index) & _mask;
        }
        
        // shift back by one slot any contiguous block of entries at the start
        // of the table, and move the last entry into the first slot
        void _relocate_backward_from_back() {
            // there must be at least one entry
            assert(!empty());
            // the last slot must be occupied
            assert(_begin[_mask].first);
            // find the first empty slot
            u64 j = 0;
            for (; _begin[j].first; ++j)
                assert(j != _mask);
            // assert it is indeed empty
            assert(!_begin[j].first);
            // relocate the first entry backward one slot
            std::memmove(_begin + 1, _begin, j * sizeof(Slot));
            // relocate the last entry to the first slot
            std::memcpy(_begin, _begin + _mask, sizeof(Slot));
            // [_mask] is empty but not cleared
            // the caller must restore the invariant
        }
        
        // shift back by one slot the contiguous block entries starting at [i]
        void _relocate_backward_from(u64 i) {
            u64 j = i;
            for (;;) {
                // j is a valid index, and [j] is occupied
                assert((j <= _mask) && _begin[j].first);
                if (j == _mask) {
                    // we have run off the end
                    _relocate_backward_from_back();
                    // [j] (aka the last slot) was relocated to [0]
                    // but it was not cleared
                    break;
                }
                ++j;
                assert(j <= _mask);
                if (!_begin[j].first)
                    // [j] is empty
                    break;
            }
            // [j] is logically empty but may not be marked as such
            std::memmove(_begin + i + 1, _begin + i, (j - i) * sizeof(Slot));
            // [i] is logically empty but not marked as such
            // the caller must restore the invariant
        }
        
        void _relocate_forward_into(u64 i); /* {
            assert(i <= _slots());
            u64 j = i, k;
            for (;;) {
                k = _next_index(j);
                if (!_begin[k].first)
                    break;
                j = k;
            }
            // we will move the range [i + 1, k) to [i, j)
            if (i < j) {
                std::memmove(_begin + i, _begin + i + 1, (k - i) * sizeof(Slot));
            } else if (j < i) {
                std::memmove(_begin + i, _begin + i + 1, (_mask - i) * sizeof(Slot));
                if (k) {
                    std::memcpy(_begin + _mask, _begin, sizeof(Slot));
                    std::memmove(_begin, _begin + 1, sizeof(k) * sizeof(Slot));
                }
            }
            _begin[k].clear();
        } */
        
                
        
        // find the location for the atom by linear probing
        // - if the atom exists, it is at this location
        // - the atom does not exist, this the location it should be inserted
        //   - if the location is occupied by another atom, that atom is less
        //     deserving of the slot
        u64 _find_index(Atom a) const {
            assert(a);
            auto index = _get_index(a);
            auto ideal = index;
            for (;;) {
                Atom b = _begin[index].first;
                if (!b)
                    return index;
                if (b == a)
                    return index;
                auto ideal_b = _get_index(b);
                if (_displacement(ideal, index) > _displacement(ideal_b, index))
                    return index;
                index = _next_index(index);
            }
            
        }

        // find the location of the atom by linear probing
        u64 _find_index_assume_present(Atom a) const {
            assert(a);
            auto index = _get_index(a);
            [[maybe_unused]] auto ideal = index;
            for (;;) {
                Atom b = _begin[index].first;
                assert(b); // impossible if a is present
                if (b == a)
                    return index;
                [[maybe_unused]] auto ideal_b = _get_index(b);
                assert(_displacement(ideal, index) <= _displacement(ideal_b, index));
                index = _next_index(index);
            }
        }

        // find the location to insert the atom by linear probing
        u64 _find_index_assume_absent(Atom a) const {
            assert(a);
            auto index = _get_index(a);
            auto ideal = index;
            for (;;) {
                Atom b = _begin[index].first;
                if (!b)
                    return index;
                assert(b != a); // impossible if a is absent
                size_type ideal_b = _get_index(b);
                if (_displacement(ideal, index) > _displacement(ideal_b, index))
                    return index;
                index = _next_index(index);
            }
        }

        // Grow the backing array
        void _grow() {

            u64 n = _mask + 1;
            Slot* old_allocation = _begin;
            Slot* first = _begin;
            Slot* last = _begin + n;
            
            // set the resize trigger to be 7/8 full
            n = n ? (n << 1) : 16;
            _mask = n - 1;
            --_shift;
            _trigger = _mask ^ (_mask >> 3);
            _begin = (Slot*) operator new(n * sizeof(Slot));
            
            // clear all the slots in the new Array
            for (Slot* first = _begin, * last = _begin + n; first != last; ++first)
                first->first.clear();

            _invariant();
                        
            for (; first != last; ++first) {
                u64 index = _find_index_assume_absent(first->first);
                Slot* p = _begin + index;
                if (p->first) {
                    assert(p->first != first->first);
                    _relocate_backward_from(index);
                }
                std::memcpy(p, first, sizeof(Slot));
            }
            
            // TODO: we are writing across the whole new array twice, once to
            // clear all slots, and then once to populate 7/16ths of all slots
            // instead, exploit the fact that the entries are in almost the same
            // order to progressively populate them

            operator delete(old_allocation);
        }
        
        
        
        // called before insert to grow the array if necessary
        void _ensure_can_insert_one() {
            assert(_size <= _trigger);
            if (_size != _trigger)
                return;
            _grow();
        }
                
        
        Self& swap(Self& other) {
            using std::swap;
            swap(_begin, other._begin);
            swap(_mask, other._mask);
            swap(_shift, other._shift);
            swap(_size, other._size);
            swap(_trigger, other._trigger);
        }
        
        AtomMap()
        : _begin(nullptr)
        , _mask(-1)
        , _shift(61)
        , _size(0)
        , _trigger(0) {
        }
        
        AtomMap(AtomMap&& other) {
            std::memcpy(this, other, sizeof(AtomMap));
            std::construct_at(&other);
        }
        
        AtomMap(const AtomMap& other) {
            for (const value_type& x : other) {
                insert(other);
            }
        }
        
        ~AtomMap() {
            Slot* first = _begin;
            Slot* const last = _end();
            for (; first != last; ++first)
                if (first->first)
                    std::destroy_at(&first->second);
            operator delete(_begin);
        }
        
        AtomMap& operator=(AtomMap&& other) {
            return AtomMap(std::move(other)).swap(*this);
        }

        AtomMap& operator=(const AtomMap& other) {
            return AtomMap(std::move(other)).swap(*this);
        }
        

        iterator begin() { return iterator(_begin, _begin + _slots()); }
        const_iterator begin() const { return const_iterator(_begin, _begin + _slots()); }
        const_iterator cbegin() const { return begin(); }
        
        iterator end() { Slot* p = _end(); return iterator(p, p); }
        const_iterator end() const { Slot* p = _end(); return const_iterator(p, p); }
        const_iterator cend() const { Slot* p = _end(); return const_iterator(p, p); }


        bool empty() const { return !_size; }
        size_type size() const { return _size; }
        
                
        void clear() {
            Slot* first = _begin;
            Slot* const last = _end();
            for (; first != last; ++first)
                if (first->first) {
                    first->first.clear();
                    std::destroy_at(&first->second);
                }
        }
        
        std::pair<iterator, bool> insert(const value_type& value) {
            _ensure_can_insert_one();
            auto index = _find_index(value.first);
            Slot* p = _begin + index;
            iterator i = iterator(p, _end());
            if (p->first == value.first) {
                return { i, false };
            } else {
                if (p->first) {
                    _relocate_backward_from(index);
                }
                p->first = value.first;
                std::construct_at(&p->second, value.second);
                ++_size;
                return {i, true };
            }
        }
        
        std::pair<iterator, bool> insert(value_type&& value) {
            _ensure_can_insert_one();
            auto index = _find_index(value.first);
            Slot* p = _begin + index;
            iterator i = iterator(p, _end());
            if (p->first == value.first) {
                return { i, false };
            } else {
                if (p->first) {
                    _relocate_backward_from(index);
                }
                p->first = value.first;
                std::construct_at(&p->second, std::move(value.second));
                ++_size;
                return {i, true };
            }
        }

        std::pair<iterator, bool> insert_or_assign(Atom key, auto&& value) {
            _ensure_can_insert_one();
            auto index = _find_index(key);
            Slot* p = _begin + index;
            iterator i = iterator(p, _end());
            if (p->first == key) {
                p->second = std::forward<decltype(value)>(value);
                return { i, false };
            } else {
                if (p->first) {
                    _relocate_backward_from(index);
                }
                p->first = key;
                std::construct_at(&p->second, std::forward<decltype(value)>(value));
                ++_size;
                return {i, true };
            }
        }
        
        std::pair<iterator, bool> try_emplace(Atom key, auto&&... args) {
            _ensure_can_insert_one();
            auto index = _find_index(key);
            Slot* p = _begin + index;
            iterator i = iterator(p, _end());
            if (p->first == key) {
                return { i, false };
            } else {
                if (p->first) {
                    _relocate_backward_from(index);
                }
                p->first = key;
                std::construct_at(&p->second, std::forward<decltype(args)>(args)...);
                ++_size;
                return {i, true };
            }
        }
        
        size_type erase(Atom key) {
            auto index = _find_index(key);
            if (_begin[index].first == key) {
                _relocate_forward_into(index);
                --_size;
                return 1;
            } else {
                return 0;
            }
        }
        
        const T& operator[](Atom key) const {
            return _begin[_find_index_assume_present(key)];
        }
        
        T& operator[](Atom key) {
            _ensure_can_insert_one();
            auto index = _find_index(key);
            if (_begin[index].first != key) {
                if (_begin[index].first)
                    _relocate_backward_from(index);
                _begin[index].first = key;
                std::construct_at(&_begin[index].second);
            }
            return _begin[index].second;
        }
        
        size_type count(Atom key) const {
            if (empty())
                return 0;
            auto index = _find_index(key);
            return _begin[index].first == key;
        }
        
        iterator find(Atom key) {
            auto index = _find_index(key);
            if (_begin[index] == key)
                return _to(index);
            else
                return end();
        }

        const_iterator find(Atom key) const {
            auto index = _find_index(key);
            if (_begin[index] == key)
                return _to(index);
            else
                return end();
        }
        
        iterator find_extant(Atom key) {
            return _to(_find_index(key));
        }
        
        const_iterator find_extant(Atom key) const {
            return _to(_find_index(key));
        }
        
        
    };

    
} // namespace wry::atom

#endif /* atom_hpp */
