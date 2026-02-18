//
//  inline_ring_buffer.hpp
//  client
//
//  Created by Antony Searle on 10/1/2026.
//

#ifndef inline_ring_buffer_hpp
#define inline_ring_buffer_hpp

#include "bit.hpp"
#include "stddef.hpp"

namespace wry {
    
    
    template<typename T, size_t N, size_t MASK = N-1>
    struct InlineRingBuffer {
        
        static_assert(std::has_single_bit(N), "InlineRingBuffer capacity must be a power of two");
        
        size_t _offset = 0;
        T _array[N] = {};
        
        void push_front(T value) {
            _array[--_offset &= MASK] = value;
        }
        
        const T& operator[](std::ptrdiff_t i) const {
            assert((0 <= i) && (i < N));
            return _array[(_offset + i) & MASK];
        }
        
        T& front() {
            return _array[_offset & MASK];
        }
        
    }; // struct InlineRingBuffer<T, N, MASK>
    
    
} // namespace

#endif /* inline_ring_buffer_hpp */
