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
    
    // Half-in-place stable merge of two sorted, disjoint input ranges
    // [first1, last1) and [first2, last2) into [d_first, d_last).  Exactly
    // one of the inputs must be "in place", meaning one of its endpoints
    // coincides with a matching endpoint of the destination:
    //
    //     first1 == d_first   // input 1 flush with head of dest
    //     first2 == d_first   // input 2 flush with head of dest
    //     last1  == d_last    // input 1 flush with tail of dest
    //     last2  == d_last    // input 2 flush with tail of dest
    //
    // At least one of these must hold; the other input is disjoint scratch.
    // (If two hold, the in-place range already fills dest and scratch is
    // empty; the merge is trivially a no-op.)
    //
    // Like memcpy vs memmove, the caller doesn't pick a variant -- the
    // overlap geometry alone determines the safe walk direction.  Tail-
    // aligned overlap means forward walk; head-aligned means backward walk.
    //
    // Stability: on ties, an element from [first1, last1) comes earlier in
    // the output than an element from [first2, last2).
    //
    // Compare std::merge (disjoint dst) and std::inplace_merge (adjacent
    // ranges, allocating scratch internally).

    template<typename T, typename Compare>
    void half_inplace_merge(const T* first1, const T* last1,
                            const T* first2, const T* last2,
                            T* d_first, T* d_last,
                            const Compare& comparator) {
        precondition(std::distance(first1, last1) + std::distance(first2, last2)
                  == std::distance(d_first, d_last));
        const bool head_overlap = (first1 == d_first) || (first2 == d_first);
        const bool tail_overlap = (last1  == d_last ) || (last2  == d_last );
        precondition(head_overlap || tail_overlap);
        // If both hold, the in-place range fills dest (scratch is empty) and
        // either walk is a trivial no-op; we arbitrarily prefer forward.

        if (tail_overlap) {
            // Forward walk: writer trails whichever input is at the tail.
            // Ties -> take from first1 so first1's tied element lands earlier.
            while ((first1 != last1) && (first2 != last2)) {
                if (comparator(*first2, *first1)) {
                    relocate(first2++, d_first++);
                } else {
                    relocate(first1++, d_first++);
                }
            }
            // At most one range remains; flush it only if it isn't already
            // in place at d_first.
            if ((first1 != last1) && (first1 != d_first))
                relocate(first1, last1, d_first);
            if ((first2 != last2) && (first2 != d_first))
                relocate(first2, last2, d_first);
        } else {
            // Backward walk: writer trails whichever input is at the head.
            // Ties -> take from last2 (first2's tail) so first1 lands earlier.
            while ((first1 != last1) && (first2 != last2)) {
                if (!comparator(*(last2 - 1), *(last1 - 1))) {
                    --last2; --d_last;
                    relocate(last2, d_last);
                } else {
                    --last1; --d_last;
                    relocate(last1, d_last);
                }
            }
            if ((first1 != last1) && (last1 != d_last))
                relocate_backward(first1, last1, d_last);
            if ((first2 != last2) && (last2 != d_last))
                relocate_backward(first2, last2, d_last);
        }
    }


    // Stable Priority Queue
    //
    // Extracts the min element, breaking ties by seniority.  Amortized O(log n)
    // operations
    //
    // https://cstheory.stackexchange.com/questions/593/is-there-a-stable-heap
    //
    // We allocate a contiguous array of 2^capacity slots, partitioned into
    // subarrays [2^i, 2^{i+1}), and a separate array of [0, capacity) storing
    // the occupancy of the first's subarrays.  Each subarray contains
    // stable_sorted elements in [2^{i+1}-sizes[i], 2^{i+1}), i.e. ties are
    // broken older-first.  All elements in subarray i+1 are older than
    // subarray i.
    //
    // To extract an element, we walk i = _capacity-1 .. 0 to find the least,
    // oldest element, which will be the first element of some Array, and the
    // highest i of equivalent elements.  O(log n)
    //
    // To insert an element, if size[0] == 0 we put it there.  Otherwise, we
    // walk up i counting the occupants until there are less than 2^i elements
    // present.  Then we merge all arrays 0..i-1 into Array i.  If there is no
    // such i, we double the allocation.
    //
    // For a string of insertions, half the time we insert into i=0, O(1), quarter
    // of the time we merge into empty i=1, O(2), eighth i=2 O(2+4).  So, we
    // have P(i) = 2^{-i-1} and cost(i) = 2^{i+1} thus amortized
    // sum P(i) * cost(i) = sum 1 = _capacity = O(log N)
    //
    // For a mixture of operations, we have reduced sizes so makes everything
    // cheaper.
    //
    // If we expand and contract, then does our O(log N) get pegged at the max
    // rather than current size?
    //
    // Alternatives are min heap on (time, insertion count) and multimap on
    // time
    //
    // heap and multimap give us O(1) lookup of the first element, but it
    // is O(log n) to erase it
    //
    // SPQ gives us O(log n) to find the first element but O(1) to erase it
    //
    // all of the pops require touching O(log n) pieces of memory, but
    // at least SPQ only writes back to O(2) of those locations

    template<typename T, typename Compare = std::less<T>>
    struct StablePriorityQueue {

        std::size_t _capacity = 0;
        std::size_t* _sizes = nullptr;
        T* _elements = nullptr;
        T* _heads = nullptr;
        std::size_t _size = 0;
        Compare _comparator;

        // the number of available elements is
        //     0 when (_capacity == 0)
        //     (1 << _capacity) otherwise

        // In elements, we have packed arrays
        // 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
        // / a b b c c c c d d d d d d d d e e e e e e e e e e e e e e e e f f
        //
        // Cache-aware twist: each occupied sub-array's head element lives in
        // the parallel _heads cache, not in _elements.  For occupied i:
        //     _heads[i]                                -- the head element
        //     _elements[last - (_sizes[i] - 1) .. last) -- the _sizes[i] - 1
        //                                                 tail elements
        //     _elements[last - _sizes[i]]              -- an empty placeholder
        //                                                 slot, reserved for
        //                                                 merges to slot the
        //                                                 head back into
        // where last = _elements + 2^{i+1}.  For unoccupied i (_sizes[i] == 0),
        // _heads[i] is empty and the whole sub-array slot is available.
        //
        // stable_extract_min thus scans _heads/_sizes (two dense linear
        // arrays, no pointer chasing into _elements) and only touches
        // _elements once, to refill _heads[bj] from the exposed tail.
        //
        // Insert paths uphold the invariant by relocating _heads[j] into the
        // placeholder slot before merging, and relocating the merged head
        // back out into _heads[i] afterwards.

        // merge where d_first = first1 - (last2 - first2), i.e. overlapping
        // ranges

        T* _insert_merge(T value, std::size_t i, T* elements, T* last2) {
            // merge everything into the empty ith Array
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
                half_inplace_merge(first1, last1, first2, last2,
                                   d_first, last2, _comparator);
                first2 = d_first;
            }
            _sizes[i] = n;
            return first2;
        }
        
        void _insert_expand(T value) {
            std::size_t i = _capacity;
            ++_capacity;
            T* p = static_cast<T*>(operator new(sizeof(T) << _capacity));
            std::size_t* q = static_cast<std::size_t*>(operator new(sizeof(std::size_t) * _capacity));
            T* h = static_cast<T*>(operator new(sizeof(T) * _capacity));

            // our reallocation gives us lots of free space to use
            T* last2 = p + (std::size_t{2} << i);
            T* first2 = last2 - 1;
            std::construct_at(first2, std::move(value));
            for (std::size_t j = 0; j != i; ++j) {
                T* last1 = _elements + (std::size_t{2} << j);
                T* first1 = last1 - _sizes[j];
                T* d_first = first2 - _sizes[j];
                // Restore the cached head into its placeholder slot so that
                // [first1, last1) is a contiguous sorted run of _sizes[j].
                if (_sizes[j])
                    relocate(_heads + j, first1);
                half_inplace_merge(first1, last1, first2, last2,
                                   d_first, last2, _comparator);
                first2 = d_first;
                q[j] = 0;
            }
            q[i] = std::distance(first2, last2);
            // Lift the merged head out of _elements into the new _heads cache.
            // q[i] >= 1 because we just constructed the new value.
            relocate(first2, h + i);
            operator delete(_elements);
            operator delete(_sizes);
            operator delete(_heads);
            _elements = p;
            _sizes = q;
            _heads = h;
        }
        
        void _insert_consolidate(T value, std::size_t i) {
            T* last3 = _elements + (std::size_t{2} << i);
            T* last2 = last3 - _sizes[i];
            T* first2 = last2 - 1;
            std::construct_at(first2, std::move(value));
            for (std::size_t j = 0; j != i; ++j) {
                T* last1 = _elements + (std::size_t{2} << j);
                T* first1 = last1 - _sizes[j];
                T* d_first = first2 - _sizes[j];
                // Restore the cached head into its placeholder slot so that
                // [first1, last1) is a contiguous sorted run of _sizes[j].
                if (_sizes[j])
                    relocate(_heads + j, first1);
                half_inplace_merge(first1, last1, first2, last2,
                                   d_first, last2, _comparator);
                first2 = d_first;
                _sizes[j] = 0;
            }
            // Restore sub-array i's cached head (if any) into the placeholder
            // slot at last2 so [last2, last3) is a contiguous sorted run.
            if (_sizes[i])
                relocate(_heads + i, last2);
            // now we have [first2, last2, last3), with
            //   sA = last2 - first2  elements of "growing" in place on the left
            //   sB = last3 - last2   elements of sub-array i in place on the right
            // We need a stable merge into [first2, last3) where sub-array i's
            // ties come first (it is the oldest of the ranges in play).
            //
            // Doing this without *any* extra relocation would require a true
            // O(1)-space in-place merge of two adjacent sorted ranges
            // (Kronrod / Hwang-Lin); instead we relocate the *smaller* of the
            // two sides to the empty low slots [_elements, _elements + 2^i)
            // and merge in the direction that keeps the larger side in place.
            //
            // Cost: sA + sB + min(sA, sB) moves, vs 2*sA + sB previously.
            std::size_t sA = std::distance(first2, last2);
            std::size_t sB = std::distance(last2, last3);
            // B (sub-array i) is older, so it must be first1 in the unified
            // API to win ties.  Whether B stays in place at the tail or A
            // stays in place at the head is determined by which side is
            // smaller and thus cheaper to evacuate.
            if (sA <= sB) {
                // evacuate A (left) to scratch; B stays in place at the tail
                relocate(first2, last2, _elements);
                half_inplace_merge(last2, last3,              // older B (in place, tail)
                                   _elements, _elements + sA, // newer A (scratch)
                                   first2, last3, _comparator);
            } else {
                // evacuate B (right) to scratch; A stays in place at the head
                relocate(last2, last3, _elements);
                half_inplace_merge(_elements, _elements + sB, // older B (scratch)
                                   first2, last2,             // newer A (in place, head)
                                   first2, last3, _comparator);
            }
            _sizes[i] = std::distance(first2, last3);
            // Lift the merged head back out into the _heads cache.
            // first2 < last3 always: we just inserted at least one element.
            relocate(first2, _heads + i);
        }
        
        
        void insert(T value) {
            ++_size;
            // seek the first empty Array
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
            // Scan the two dense linear arrays (_heads and _sizes) looking
            // for the winning head.  No pointer chasing into _elements.
            // Iterating high -> low preserves older-first stability: on a
            // tie, the first (higher i) head we see wins.
            T* best = nullptr;
            std::size_t bj = 0;
            for (std::size_t i = _capacity; i--;) {
                if (_sizes[i]) {
                    if (!best || _comparator(_heads[i], *best)) {
                        best = _heads + i;
                        bj = i;
                    }
                }
            }
            T result{std::move(*best)};
            std::destroy_at(best);
            assert(_sizes[bj]);
            --_sizes[bj];
            // Refill _heads[bj] from the freshly-exposed first tail element
            // in _elements, restoring the invariant.  This is the single
            // cold load.  The destination placeholder in _elements is the
            // slot we just relocated out of.
            if (_sizes[bj]) {
                T* last = _elements + (std::size_t{2} << bj);
                relocate(last - _sizes[bj], _heads + bj);
            }
            return result;
        }
                
    }; // struct StablePriorityQueue    
    
} // namespace wry

#endif /* stable_priority_queue_hpp */
