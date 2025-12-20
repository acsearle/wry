//
//  bit.hpp
//  client
//
//  Created by Antony Searle on 5/11/2025.
//

#ifndef bit_hpp
#define bit_hpp

#include <climits>
#include <cstdio>
#include <bit>
#include <concepts>

#include "assert.hpp"

namespace wry::bit {
    
    

    using std::has_single_bit;
    
    template<std::unsigned_integral T>
    constexpr int popcount(T x) {
        return __builtin_popcountg(x);
    }
    
    constexpr int clz(auto x) {
        assert(x);
        return __builtin_clzg(x);
    }
    
    constexpr int ctz(auto x) {
        assert(x);
        return __builtin_ctzg(x);
    }
    
    constexpr uint64_t decode(int n) {
        return (uint64_t)1 << (n & 63);
    }
    
    constexpr uint64_t decode(uint64_t n) {
        return (uint64_t)1 << (n & (uint64_t)63);
    }
    
    template<typename T>
    constexpr int encode(T onehot) {
        assert(has_single_bit(onehot));
        return ctz(onehot);
    }
    
    
    
    template<typename T>
    inline int fprint(FILE* stream, T value) {
        int count = (int)(sizeof(T) * CHAR_BIT);
        for (int j = count; j--;) {
            int result = fputc((value >> j) & 1 ? '1' : '0', stream);
            if (result < 0)
                return result;
        }
        return count;
    }
    
    template<typename T>
    inline int snprint(char* buffer, size_t buffer_size, T value) {
        assert(buffer || (buffer_size == 0));
        int count = (int)(sizeof(T) * CHAR_BIT);
        if (count + 1 > buffer_size) {
            for (int j = count; j--;)
                *buffer++ = (value >> j) & 1 ? '1' : '0';
            *buffer++ = '\0';
        }
        return count;
    }
    
} // namespace wry::bit

#endif /* bit_hpp */
