//
//  stable_priority_queue.hpp
//  client
//
//  Created by Antony Searle on 5/11/2023.
//

#ifndef stable_priority_queue_hpp
#define stable_priority_queue_hpp

#include <cassert>
#include <cstddef>
#include <functional>
#include <algorithm>

#include "assert.hpp"
#include "memory.hpp"

namespace wry {
    
    // merge sorted ranges [first1, last1) and [first2, last2) into
    // [d_first, ...), where either [first1, last1) or [first2, last2) occupies
    // the right half of the destination, and the other range is disjoint
    
    // when equivalent elements are present, the merge is stable, with [first1,
    // last1) being before [first2, last2); this is why we need both forms
    
    // compare std::merge, std::inplace_merge
    
    template<typename T, typename Compare>
    const T* half_inplace_merge1(const T* first1, const T* last1,
                                 const T* first2, const T* last2,
                                 T* d_first, const Compare& comparator) {
        precondition(d_first == first1 - std::distance(first2, last2));
        for (;;) {
            if (first1 == last1) {
                return relocate(first2, last2, d_first);
            }
            if (first2 == last2) {
                assert(first1 == d_first);
                return last1;
            }
            if (comparator(*first2, *first1)) {
                relocate(first2++, d_first++);
            } else {
                relocate(first1++, d_first++);
            }
        }
    }
    
    template<typename T, typename Compare>
    const T* half_inplace_merge2(const T* first1, const T* last1,
                                 const T* first2, const T* last2,
                                 T* d_first, const Compare& comparator) {
        precondition(d_first == first2 - std::distance(first1, last1));
        for (;;) {
            if (first1 == last1) {
                assert(d_first == first2);
                return last2;
            }
            if (first2 == last2) {
                return relocate(first1, last1, d_first);
            }
            if (comparator(*first2, *first1)) {
                relocate(first2++, d_first++);
            } else {
                relocate(first1++, d_first++);
            }
        }
    }
    
    
    // Extracts the min element, breaking ties by seniority
    //
    // Amortized O(log n) operations
    //
    // https://cstheory.stackexchange.com/questions/593/is-there-a-stable-heap
    //
    // We allocate a contiguous array of 2^capacity slots, partitioned into
    // subarrays [2^i, 2^{i+1}), and an array of [0, capacity) subarray
    // occupancies.  Each subarray contains stable_sorted elements in
    // [2^{i+1}-sizes[i], 2^{i+1}), i.e. ties are broken older-first.  All
    // elements in subarray i+1 are older than subarray i.
    //
    // To extract an element, we walk i = _capacity-1 .. 0 to find the least,
    // oldest element, which will be the first element of some array, and the
    // highest i of equivalent elements.  O(log n)
    //
    // To insert an element, if occupancy[i] == 0 we put it there.
    // Otherwise, we walk up i counting the occupants until there are less than
    // 2^i elements present.  Then we merge all arrays 0..i-1 into array i.
    // If there is no such i, we double the allocation.
    //
    // For a string of insertions, half the time we insert into i=0, O(1), quarter
    // of the time we merge into empty i=1, O(2), eighth i=2 O(2+4).  So, we
    // have P(i) = 2^{-i-1} and cost(i) = 2^{i+1} thus amortized
    // sum P(i) * cost(i) = sum 1 = _capacity = O(log2 N)
    //
    // For a mixture of operations, we have reduced sizes so makes everything
    // cheaper
    //
    // If we expand and contract, then does our O(log2 N) get pegged at the max
    // rather than current size?
    //
    // Compare with a btree, also log2 N, stable sort

    template<typename T, typename Compare = std::less<T>>
    struct StablePriorityQueue {

        std::size_t _capacity = 0;
        std::size_t* _sizes = nullptr;
        T* _elements = nullptr;
        std::size_t _size = 0;
        Compare _comparator;
        
        // the number of available elements is
        //     0 when (_capacity == 0)
        //     (1 << _capacity) otherwise
        
        // In elements, we have packed arrays
        // 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
        // / a b b c c c c d d d d d d d d e e e e e e e e e e e e e e e e f f

        // merge where d_first = first1 - (last2 - first2), i.e. overlapping
        // ranges

        T* _insert_merge(T value, std::size_t i, T* elements, T* last2) {
            // merge everything into the empty ith array
            std::size_t n = 1;
            T* first2 = last2 - n;
            std::construct_at(first2, std::move(value));
            // merge all the sub-arrays into it
            for (std::size_t j = 0; j != i; ++j) {
                T* last1 = _elements + (std::size_t{2} << j);
                assert((0 < _sizes[j]) && (_sizes[j] <= (std::size_t{1} << j)));
                T* first1 = last1 - _sizes[j];
                T* d_first = first2 - _sizes[j];
                n += _sizes[j];
                _sizes[j] = 0;
                half_inplace_merge2(first1, last1, first2, last2, d_first, _comparator);
                first2 = d_first;
            }
            _sizes[i] = n;
            return first2;
        }
        
        void _insert_expand(T value) {
            printf("expanding from %zd with _size=%zd, load factor %g\n", _capacity, _size, (double) _size / (1 << _capacity));
            std::size_t i = _capacity;
            ++_capacity;
            T* p = static_cast<T*>(operator new(sizeof(T) << _capacity));
            std::size_t* q = static_cast<std::size_t*>(operator new(sizeof(std::size_t) * _capacity));

            // our reallocation gives us lots of free space to use
            T* last2 = p + (std::size_t{2} << i);
            T* first2 = last2 - 1;
            std::construct_at(first2, std::move(value));
            for (std::size_t j = 0; j != i; ++j) {
                T* last1 = _elements + (std::size_t{2} << j);
                T* first1 = last1 - _sizes[j];
                T* d_first = first2 - _sizes[j];
                [[maybe_unused]] const T* d_last =
                half_inplace_merge2(first1, last1, first2, last2, d_first, _comparator);
                assert(d_last == last2);
                first2 = d_first;
                q[j] = 0;
            }
            q[i] = std::distance(first2, last2);
            operator delete(_elements);
            operator delete(_sizes);
            _elements = p;
            _sizes = q;
        }
        
        void _insert_consolidate(T value, std::size_t i) {
            printf("consolidating into _sizes[%zd]=%zd\n", i, _sizes[i]);

            T* last3 = _elements + (std::size_t{2} << i);
            T* last2 = last3 - _sizes[i];
            T* first2 = last2 - 1;
            std::construct_at(first2, std::move(value));
            for (std::size_t j = 0; j != i; ++j) {
                T* last1 = _elements + (std::size_t{2} << j);
                T* first1 = last1 - _sizes[j];
                T* d_first = first2 - _sizes[j];
                [[maybe_unused]] const T* d_last =
                half_inplace_merge2(first1, last1, first2, last2, d_first, _comparator);
                assert(d_last == last2);
                first2 = d_first;
                _sizes[j] = 0;
            }
            // now we have [first2, last2, last3)
            // FIXME: we can do the last-but-one merge in-place instead of this
            // extra relocation
            relocate(first2, last2, _elements);
            half_inplace_merge1(last2, last3, _elements, _elements + std::distance(first2, last2), first2, _comparator);
            _sizes[i] = std::distance(first2, last3);
            
        }
        
        
        void insert(T value) {
            ++_size;
            // seek the first empty array
            std::size_t i = 0;
            std::size_t j = 1;
            std::size_t n = 1;
            
            for (;;) {
                if (i == _capacity) {
                    return _insert_expand(std::move(value));
                }
                n += _sizes[i];
                if (n <= j) {
                    // we can fit all the elements in the range [j, 2j]
                    return _insert_consolidate(std::move(value), i);
                }
                ++i;
                j <<= 1;
            }
        
        }
        
        T stable_extract_min() {
            --_size;
            T* best = nullptr;
            std::size_t j = 0;
            for (std::size_t i = _capacity; i--;) {
                if (_sizes[i]) {
                    T* last = _elements + (std::size_t{2} << i);
                    T* first = last - _sizes[i];
                    if (!best || _comparator(*first, *best)) {
                        best = first;
                        j = i;
                    }
                }
            }
            T result{std::move(*best)};
            std::destroy_at(best);
            assert(_sizes[j]);
            --_sizes[j];
            return result;
        }
                
    }; // struct StablePriorityQueue    
    
} // namespace wry

#endif /* stable_priority_queue_hpp */
