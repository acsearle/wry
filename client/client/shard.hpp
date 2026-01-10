//
//  shard.hpp
//  client
//
//  Created by Antony Searle on 1/1/2026.
//

#ifndef shard_hpp
#define shard_hpp

#include <new>
#include <cstddef>

namespace wry {
    
    constexpr std::size_t SHARD_COUNT = 4;
    constinit thread_local std::size_t _this_thread_shard_index = {};
    
    template<typename T>
    struct Sharded {
        
        struct alignas(std::hardware_destructive_interference_size) Padded {
            T _data;
        };
        
        Padded _data[SHARD_COUNT] = {};
        
        T const& operator*() const {
            return _data[_this_thread_shard_index]._data;
        }
        
        T const* operator->() const {
            return &(_data[_this_thread_shard_index]._data);
        }
        
    };
    
    
    // Example:
    // ShardedCounter competes with an Atomic<ptrdiff_t>
    
    struct ShardedCounter {
        struct alignas(std::hardware_destructive_interference_size) Padded {
            std::ptrdiff_t _data;
        };
        Padded _data[SHARD_COUNT];
        
        // threads may perform commutative operations on the count without data
        // races or false sharing, but they cannot observe the count
        
        // commutative operation set: addition, subtraction, with wrapping
        void operator++() {
            ++_data[_this_thread_shard_index]._data;
        }
        void operator--() {
            --_data[_this_thread_shard_index]._data;
        }
        void operator++(int) {
            _data[_this_thread_shard_index]._data++;
        }
        void operator--(int) {
            _data[_this_thread_shard_index]._data--;
        }
        void operator+=(std::ptrdiff_t n) {
            _data[_this_thread_shard_index]._data += n;
        }
        void operator-=(std::ptrdiff_t n) {
            _data[_this_thread_shard_index]._data -= n;
        }
        
        // alternative commutative operations on integers (that are not counters):
        // { OR } (models addition to a set, idempotent)
        // { XOR } (models parity of multiset?)
        // { AND } (models erasure from a set, idempotent)
        // { multiplication } (but not division)
                
        // all updates must happen-before reduce is called
        std::ptrdiff_t reduce() const {
            std::ptrdiff_t n = {};
            for (Padded const& x : _data)
                n += x._data;
            return n;
        }
    };
    
    // Notice that for final optimization we may want to pack different sharded
    // things for a particular thread into the same cache line
    
} // namespace wry



#endif /* shard_hpp */
