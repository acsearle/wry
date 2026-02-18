//
//  string.cpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#include "string.hpp"
#include "table.hpp"

namespace wry {
    /*
    // stores InternedStrings, which know their own hash, and are reference
    // counted.
    
    // When walking the set, we clean up any objects not referenced by
    // anything else
    
    struct WeakHashSet {
        
        std::mutex _mutex;
        InternedString::Implementation** _begin = nullptr;
        uint64_t _mask = -1;
        int _shift = 61;
        uint64_t _count = 0;
        uint64_t _trigger = 0;
        
        uint64_t count() const {
            return _count;
        }
        
        uint64_t size() const {
            return _mask + 1;
        }
        
        InternedString::Implementation** begin() { return _begin; }
        InternedString::Implementation** end() { return _begin + size(); }
        
        uint64_t _get_index(uint64_t h) const {
            return h >> _shift;
        }
        
        uint64_t _next_index(uint64_t i) const {
            return (i + 1) & _mask;
        }
        
        uint64_t _displacement(uint64_t desired, uint64_t actual) const {
            return (actual - desired) & _mask;
        }
        
        InternedString::Implementation* find_or_insert(uint64_t h, StringView v) {
            if (_count == _trigger)
                resize();
            uint64_t ih = _get_index(h); // ideal
            uint64_t i = ih;
            uint64_t g, ig;
        alpha:
            if (!_begin[i])
                // found empty slot
                goto beta;
            // found nonempty slot
            g = _begin[i]->_hash;
            if ((g == h) 
                && (StringView(_begin[i]->_begin, _begin[i]->_size) == v))
                // found exact match
                return _begin[i];
            ig = _get_index(g);
            if (_displacement(ih, i) > _displacement(ig, i)) {
                // if the string existed, it would be here
                // make a hole
                _relocate_backward_from(i);
                goto beta;
            }
            if (!_begin[i]->_count.load(std::memory_order_relaxed)) {
                // the occupant is only weakly referenced
                free(_begin[i]);
                _relocate_forward_into(i);
                --_count;
                goto alpha;
            }

            
        beta:
            // insert a new string at [i]
            ++_count;
            auto n = v.chars.size();
            _begin[i] = (InternedString::Implementation*)
                malloc(sizeof(InternedString::Implementation) + n + 1);
            _begin[i]->_count = 1;
            _begin[i]->_hash = h;
            _begin[i]->_size = n;
            memcpy(_begin[i]->_begin, v.chars.data(), n);
            _begin[i]->_begin[n] = 0;
            return _begin[i];
        }
        
        
        
        InternedString::Implementation** find(std::uint64_t h, StringView v) {
            if (!_count)
                return nullptr;
            uint64_t ih = _get_index(h); // ideal
            uint64_t i = ih;
            for (;;) {
                if (!_begin[i])
                    return nullptr; // found vacancy
                uint64_t g = _begin[i]->_hash;
                if ((g == h) && (StringView(_begin[i]->_begin, _begin[i]->_size) == v))
                    return _begin + i; // found exact match
                uint64_t ig = _get_index(g);
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
    
        
    
    InternedString::Implementation* InternedString::_oracle(StringView v) {
        
        
        
    }
     
     */
        
}
