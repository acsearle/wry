//
//  flatmap.hpp
//  client
//
//  Created by Antony Searle on 25/12/2023.
//

#ifndef flatmap_hpp
#define flatmap_hpp

#include "algorithm.hpp"
#include "array.hpp"

namespace wry {
    
    // open / flat hash table (see table)
    //
    // O(1) lookup (but, large constant factor for hashing, and a cache miss?)
    // non-deterministic iteration (and, O(storage) not O(N))
    
    // flat_map maintains sorted elements in a flat array
    //
    // find is O(log N)
    // insert and erase are O(N)
    //
    // contiguous memory makes iteration and find fast for small maps
    
    template<typename Key, typename T>
    struct flat_map {
        Array<std::pair<Key, T>> base;
        template<typename K>
        T& operator[](K&& k) {
            using std::begin;
            using std::end;
            auto first = begin(base);
            auto last = end(base);
            first = std::lower_bound(first, last, k);
            if ((first == last) || (k < first->first)) {
                // no equivalent element exists
                first = base.emplace(first, k, T{})->second;
            }
            return first->second;
        }
    };
    
    // unsorted_map preserves order of insertion at the cost of O(N) lookups
    // This odd behavior is intended to help stabilizing file parsing
    // It may be a bad idea

    template<typename Key, typename T, typename A = Array<std::pair<Key, T>>>
    struct unsorted_map {
        
        A base;
        
        template<typename K>
        T& operator[](K&& k) {
            using std::begin;
            using std::end;
            auto first = begin(base);
            auto last = end(base);
            first = std::find_if(first,
                                 last,
                                 [&k] (std::pair<Key, T>& x) {
                return x.first == k;
            });
            if (first == last)
                first = base.emplace(first, std::forward<K>(k), T{});
            return first->second;
        }
        
    };
    
} // namespace wry

#endif /* flatmap_hpp */
