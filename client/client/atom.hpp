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
    // Atoms construct a perfect hash of strings on the fly, up to 2^64 unique
    // strings.  Converting to and from Atoms is relatively expensive, but
    // using them as the keys of an AtomMap is cheap.  Typical usage would
    // (de)serialize strings but work with atoms.
    //
    // The mapping varies between runs.
    
    struct Atom {

        u64 data;
        
        const char* to_string() const;
        
        static Atom from_string(const char*);
        Atom from_string(StringView s);
        
        bool operator==(const Atom&) const = default;
        
    };
    
    
    // This hash map is allowed to assume that collisions are impossible and
    // that zero is not a valid hash
    
    template<typename T>
    struct AtomMap {
        
        using size_type = std::size_t;
        
        struct Entry {
            u64 atom;
            union {
                T maybe;
            };
        };
        
        Entry* _begin;
        u64 _mask;
        int _shift;
        u64 _count;
        u64 _trigger;
        
        AtomMap& swap(AtomMap& other) {
            using std::swap;
            swap(_begin, other._begin);
            swap(_mask, other._mask);
            swap(_shift, other._shift);
            swap(_count, other._count);
            swap(_trigger, other._trigger);
        }
        
        AtomMap()
        : _begin(nullptr)
        , _mask(-1)
        , _shift(61)
        , _count(0)
        , _trigger(0) {
        }
        
        AtomMap(AtomMap&& other) {
            std::memcpy(this, other, sizeof(AtomMap));
            std::construct_at(&other);
        }
        
        ~AtomMap() {
            for (auto& entry : *this)
                if (entry.atom)
                    std::destroy_at(&entry.maybe);
        }
        
        
        size_type count() {
            return count();
        }
        
        size_type size() {
            return _mask + 1;
        }
        
        Entry* begin() {
            return _begin;
        }
        
        Entry* end() {
            return _begin + size();
        }
        
        void clear() {
            for (Entry& e : *this) {
                if (e.atom) {
                    e.atom = 0;
                    std::destroy_at(&e.maybe);
                }
            }
        }
        
        u64 _get_index(u64 hash) const {
            return hash >> _shift;
        }
        
        u64 _next_index(u64 index) const {
            return (index + 1) & _mask;
        }
        
        u64 _displacement(u64 desired, u64 actual) const {
            return (actual - desired) & _mask;
        }
        
        // Robin Hood hashing keeps linear probe lengths short by displacing
        // elements backwards when a pending insertion has a better claim on a
        // slot

        void _relocate_backward_from_begin() {
            u64 j = 0;
            for (; _begin[j].atom; ++j) {
                assert(j != _mask);
            }
            assert(!_begin[j].atom);
            // relocate the leading elements back one slot
            std::memmove(_begin + 1, _begin, j * sizeof(Entry));
            // relocate the last element to the first slot
            std::memcpy(_begin, _begin + _mask, sizeof(Entry));
        }

        void _relocate_backward_from(u64 i) {
            u64 j = i;
            for (;;) {
                assert((j <= _mask) && _begin[j].atom);
                if (j == _mask) {
                    // the index has wrapped
                    _relocate_backward_from_begin();
                    // _begin[j] was relocated to _begin[0]
                    break;
                }
                ++j;
                assert(j <= _mask);
                if (!_begin[j].atom)
                    break;
            }
            // _begin[j] is logically empty
            std::memmove(_begin + i + 1, _begin + i, (j - i) * sizeof(T));
            
        }
        
        
        // Finds where an atom appears, or where it would be if it was present
        //
        // Can be used to find
        //
        // Can be used to insert (maybe resize before, maybe emplace afterwards)
        
        u64 _locate(u64 atom) const {
            assert(atom);
            size_type ideal = atom >> _shift;
            size_type index = ideal;
            for (;;) {
                Entry* pointer = _begin + index;
                u64 occupant = pointer->atom;
                if (!occupant) {
                    // slot is empty
                    return index;
                }
                if (occupant == atom) {
                    // atoms match, authoritative
                    return index;
                }
                size_type ideal2 = _get_index(occupant);
                if (_displacement(ideal, index) > _displacement(ideal2, index)) {
                    // if the sought value existed, it would have displaced this
                    // one
                    return index;
                }
                index = _next_index(index);
            }
        }
        
        
        // when we know that the key exists we can find it much faster
        // but, if we are wrong, will never terminate
        
        u64 _locate_extant(u64 atom) const {
            assert(atom);
            size_type index = atom >> _shift;
            for (;;) {
                if (_begin[index].atom == atom)
                    return index;
                assert(_begin[index].atom);
                index = _next_index(index);
            }
        }
        void _maybe_resize() {
            assert(_count <= _trigger);
            if (_count != _trigger)
                return;
            u64 n = _mask + 1;
            Entry* old = _begin;
            Entry* first = _begin;
            Entry* last = _begin + n;
            n = n ? (n << 1) : 16;
            _mask = n - 1;
            --_shift;
            assert((((uint64_t) -1) >> _shift) == _mask);
            _trigger = _mask ^ (_mask >> 3); // resize at 7/8 full
            assert(_count < _trigger);
            _begin = (Entry*) operator new(n * sizeof(Entry));
            for (auto& entry : *this)
                entry.atom = 0;
            for (; first != last; ++first) {
                u64 index = _locate(first->atom);
                Entry* pointer = _begin + index;
                if (pointer->atom) {
                    assert(pointer->atom != first->atom);
                    _relocate_backward_from(index);
                }
                std::memcpy(pointer, first, sizeof(Entry));
            }
            operator delete(old);
        }
        
        Entry* find(Atom a) const {
            u64 index = _locate(a.data);
            Entry* pointer = _begin + index;
            return (pointer->atom == a.data) ? pointer : nullptr;
        }
        
        T& find_extant(Atom a) {
            return _begin[_locate_extant(a.data)].maybe;
        }

        const T& find_extant(Atom a) const {
            return _begin[_locate_extant(a.data)].maybe;
        }
        
        const T& operator[](Atom a) const {
            return find_extant(a);
        }

        Entry* emplace(Atom a, auto&&... args) {
            _maybe_resize();
            u64 index = _locate(a.data);
            Entry* pointer = _begin + index;
            if (pointer->atom) {
                assert(pointer->atom != a.data);
                _relocate_backward_from(index);
            }
            pointer->atom = a.data;
            std::construct_at(&pointer->maybe, std::forward<decltype(args)>(args)...);
            return pointer;
        }
        
        std::pair<Entry*,  bool> insert_or_assign(Atom a, auto&& value) {
            _maybe_resize();
            u64 index = _locate(a);
            Entry* pointer = _begin + index;
            if (pointer->atom == a.data) {
                pointer->maybe = std::forward<decltype(value)>(value);
                return { pointer, false };
            } else {
                if (pointer->atom)
                    _relocate_backward_from(index);
                pointer->atom = a.data;
                std::construct_at(&pointer->maybe, std::forward<decltype(value)>(value));
                return { pointer, true };
            }
        }
        
    };

    
} // namespace wry::atom

#endif /* atom_hpp */
