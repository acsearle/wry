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

#include "memory.hpp"

namespace wry {

    template<typename T, typename Compare = std::less<T>>
    struct StablePriorityQueue {

        std::size_t _capacity = 0;
        std::size_t* _sizes = nullptr;
        T* _elements = nullptr;
        Compare _comparator;

        // the number of available elements is
        //     0 when (_capacity == 0)
        //     (1 << _capacity) otherwise
        
        // In elements, we have packed arrays
        // 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
        // a b b c c c c d d d d d d d d e e e e e e e e e e e e e e e e f f
        
        // compare std::merge
        void merge(T* first1, T* last1,
                   T* first2, T* last2,
                   T* d_first) {
            assert(d_first == first1 - (last2 - first2));
            for (;;) {
                if (first1 == last1) {
                    assert(first2 != d_first);
                    wry::relocate(first2, last2, d_first);
                    return;
                }
                if (first2 == last2) {
                    assert(first1 == d_first);
                    return;
                }
                if (_comparator(*first1, *first2)) {
                    wry::relocate(first1++, d_first++);
                } else {
                    wry::relocate(first2++, d_first++);
                }
            }
        }
        
        void insert(T value) {
            DUMP(value);
            // seek the first empty array
            std::size_t i = 0;
            T* new_elements = _elements;
            
            for (;;) {
                if (i == _capacity) {
                    ++_capacity;
                    new_elements = static_cast<T*>(operator new(sizeof(T) << _capacity));
                    if (!_sizes) {
                        assert(i == 0);
                        _sizes = static_cast<std::size_t*>(operator new(sizeof(std::size_t) * 64));
                    }
                    _sizes[i] = 0;
                    break;
                }
                if (_sizes[i] != 0) {
                    ++i;
                    continue;
                }
                break;
            }
            //TODO: consider consolidation into a partially empty array
            //TODO: consider consolidation into an earlier array
            
            DUMP(i);
            
            // merge everything into the empty ith array
            std::size_t n = 1;
            T* last1 = new_elements + (std::size_t{2} << i);
            T* first1 = last1 - n;
            std::construct_at(first1, std::move(value));
            // merge all the sub-arrays into it
            for (std::size_t j = 0; j != i; ++j) {
                T* last2 = _elements + (std::size_t{2} << j);
                assert((0 < _sizes[j]) && (_sizes[j] <= (std::size_t{1} << j)));
                T* first2 = last2 - _sizes[j];
                T* d_first = first1 - _sizes[j];
                n += _sizes[j];
                _sizes[j] = 0;
                merge(first1, last1, first2, last2, d_first);
                first1 = d_first;
            }
            _sizes[i] = n;
            if (_elements != new_elements) {
                operator delete(_elements);
                _elements = new_elements;
            }
        }
        
        T stable_extract_min() {
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
        
        
        
        
        
        
    };
    
    
} // namespace wry


#endif /* stable_priority_queue_hpp */
