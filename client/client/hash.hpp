//
//  hash.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef hash_hpp
#define hash_hpp

#include <cstddef>
#include <cstring>
#include <cmath>
#include <tuple>
#include <utility>

#include "assert.hpp"
#include "stdint.hpp"

namespace wry {
    
    template<typename T>
    auto sqr(const T& t) {
        return t * t;
    }
    
    class rand {
        
        uint64_t _x;
        
    public:
        
        explicit rand(uint64_t seed = 0)
        : _x(4101842887655102017ull) {
            _x ^= seed;
        }
        
        uint64_t operator()() {
            _x ^= _x >> 21; _x ^= _x << 35; _x ^= _x >> 4;
            return _x * 2685821657736338717ull;
        }
        
        using result_type = uint64_t;
        static constexpr uint64_t min() { return 1; }
        static constexpr uint64_t max() { return -1ull; }
        
    };
    
    template<typename T = rand>
    struct uniform_distribution {
        T _x;
        explicit uniform_distribution(uint64_t seed = 0)
        : _x(seed) {
        }
        double operator()() {
            return _x() * 5.42101086242752217e-20;
        }
    };
    
    template<typename T = uniform_distribution<>>
    class normal_distribution {
        
        T _x;
        
    public:
        
        explicit normal_distribution(uint64_t seed = 0) : _x(seed) {}
        
        double operator()() {
            // normal deviate by ratio of uniforms
            double u, v, q;
            do {
                u = _x();
                v = 1.7156 * (_x() - 0.5);
                auto x = u - 0.449871;
                using std::abs;
                auto y = abs(v) + 0.386595;
                q = sqr(x) + y * (0.19600 * y - 0.25472 * x);
            } while ((q > 0.27597)
                     && ((q > 0.27846)
                         || (sqr(v) > -4.0 * log(u) * sqr(u))));
            return v / u;
        }
        
    };
    
    // Numerical Recipes 7.1.4
    //
    // "a random hash of the integers, one that passes serious tests for
    // randomness, even for very ordered sequences of [input]", i.e. hash(i++)
    // is a high-quality random number generator
    //
    // hash(x) is also an injective function
    //
    // unlike libc++'s trivial std::hash implementation, suitable for direct
    // use in hash tables (in non-adversarial environments)
    //
    // TODO: obsolete for hashing?
            
    inline constexpr uint64_t hash(uint64_t x) {
        x = x * 3935559000370003845ull + 2691343689449507681ull;
        x ^= x >> 21; x ^= x << 37; x ^= x >> 4;
        x *= 4768777513237032717ull;
        x ^= x << 20; x ^= x >> 41; x ^= x << 5;
        return x;
    }
            
    inline constexpr uint32_t hash32(uint32_t x) {
        // NR does not provide a 32-bit version of hash(), but we construct one
        // from the 32-bit versions of the components.  Test to make sure we
        // haven't gotten unlucky.
        x = x * 2891336453u + 1640531513u;
        x ^= x >> 13; x ^= x << 17; x ^= x >> 5;
        x *= 1597334677u;
        x ^= x <<  9; x ^= x >> 17; x ^= x << 6;
        return x;
    }
                        
    // Interleave bits to achieve a 1D indexing of 2D space with decent
    // locality properties.  Good for spatial hashing
    //
    // https://en.wikipedia.org/wiki/Z-order_curve
    
    constexpr uint64_t _morton_expand(uint64_t x) noexcept {
        precondition(x == (x & 0x00000000FFFFFFFF));
        x = (x | (x << 16)) & 0x0000FFFF0000FFFF;
        x = (x | (x <<  8)) & 0x00FF00FF00FF00FF;
        x = (x | (x <<  4)) & 0x0F0F0F0F0F0F0F0F;
        x = (x | (x <<  2)) & 0x3333333333333333;
        x = (x | (x <<  1)) & 0x5555555555555555;
        return x;
    }
    
    constexpr uint64_t _morton_contract(uint64_t x) noexcept {
        precondition(x == (x & 0x5555555555555555));
        x = (x | (x >>  1)) & 0x3333333333333333;
        x = (x | (x >>  2)) & 0x0F0F0F0F0F0F0F0F;
        x = (x | (x >>  4)) & 0x00FF00FF00FF00FF;
        x = (x | (x >>  8)) & 0x0000FFFF0000FFFF;
        x = (x | (x >> 16)) & 0x00000000FFFFFFFF;
        return x;
    }
    
    constexpr uint64_t morton(uint64_t x, uint64_t y) noexcept {
        return _morton_expand(x) | (_morton_expand(y) << 1);
    }
    
    constexpr uint64_t morton2(uint64_t x) noexcept {
        // We use the XOR-trick to swap bit ranges.  We achive interleaving
        // by swapping the middle quarters of the bit range, and then recursing
        // down.
        uint64_t b = (x ^ (x >> 16)) & 0x00000000FFFF0000;
        x ^= b | (b << 16);
        b = (x ^ (x >> 8)) & 0x0000FF000000FF00;
        x ^= b | (b << 8);
        b = (x ^ (x >> 4)) & 0x00F000F000F000F0;
        x ^= b | (b << 4);
        b = (x ^ (x >> 2)) & 0x0C0C0C0C0C0C0C0C;
        x ^= b | (b << 2);
        b = (x ^ (x >> 1)) & 0x2222222222222222;
        x ^= b | (b << 1);
        return x;
    }
    
    constexpr uint64_t morton2_reverse(uint64_t x) noexcept {
        // Reverse the operation
        uint64_t b = (x ^ (x >> 1)) & 0x2222222222222222;
        x ^= b | (b << 1);
        b = (x ^ (x >> 2)) & 0x0C0C0C0C0C0C0C0C;
        x ^= b | (b << 2);
        b = (x ^ (x >> 4)) & 0x00F000F000F000F0;
        x ^= b | (b << 4);
        b = (x ^ (x >> 8)) & 0x0000FF000000FF00;
        x ^= b | (b << 8);
        b = (x ^ (x >> 16)) & 0x00000000FFFF0000;
        x ^= b | (b << 16);
        return x;
    }
    
    // hash bytes
    
    inline uint64_t hash_combine(const void* src, ptrdiff_t bytes, uint64_t already_hashed = 0) {
        while (bytes >= 8) {
            uint64_t x;
            std::memcpy(&x, src, 8);
            already_hashed = hash(already_hashed ^ x);
            src = ((const unsigned char*) src) + 8;
            bytes -= 8;
        }
        if (bytes) {
            uint64_t x = 0;
            std::memcpy(&x, src, bytes);
            already_hashed = hash(already_hashed ^ x);
        }
        return already_hashed;
    }
    
    
    
    
    
    // constexpr FNV-1a hash for strings
    // https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
    
    // constexpr hashing can't use memcpy or reinterpret_cast
    
    // primary virtue is simplicity.  the top byte will be unaffected
    // (barring carry) by the last two bytes of the input; hash tables should
    // use the lsbs, possibly ignoring the last one?  seems to not be a problem
    // given widespread use in the past
    
    // we can compute the hash of a long string piecewise by chaining the hash
    // state
    
    constexpr uint64_t FNV1A_OFFSET_BASIS = 0xcbf29ce484222325;
    constexpr uint64_t FNV1A_PRIME = 0x00000100000001b3;
    
    constexpr uint64_t fnv1a(const char* str, uint64_t hash = FNV1A_OFFSET_BASIS) {
        assert(str);
        for (; *str; ++str) {
            hash ^= (unsigned char) *str;
            hash *= FNV1A_PRIME;
        }
        return hash;
    }
    
    // TODO: hacked to fix inconsistency bug where Strings and const char* were
    // hashed differently.  If we want to use fnv1a, we need two slightly
    // different versions that are sized and zero-terminated respectively
    constexpr uint64_t hash(const char* str) {
        // return fnv1a(str);
        return hash_combine(str, strlen(str));
    }
    
    // Hashed strings give us a conventional hash of the string's contents that
    // is independent of the string identity (requiring no lookup) but only
    // fast for mismatch
    
    constexpr uint64_t UINT64_T_MSB = 0x8000000000000000;
        
    struct hashed_str {
        
        uint64_t _hash;
        const char* _str;
        
        explicit constexpr hashed_str(const char* str)
        : _hash(fnv1a(str) | UINT64_T_MSB)
        , _str(str) {
        }
        
        constexpr bool empty() const {
            return !_hash;
        }
        
        constexpr uint64_t hash() const {
            return _hash;
        }
        
        constexpr const char* str() const {
            return _str;
        }
        
        constexpr bool operator==(const hashed_str& other) const {
            if (_hash != other._hash)
                return false;
            // Overwhelmingly likely to be a match, but strcmp will have to
            // take two cache misses to load and iterate over both strings to
            // prove it.  It seems worth comparing the pointers in the hope
            // that they point to the same copy of the string.
            if (_str == other._str)
                return true;
            // Unlike strcmp, __builtin_strcmp is constexpr
            return __builtin_strcmp(_str, other._str);
        }
        
    };
    
    constexpr hashed_str operator""_hashed(const char* str, std::size_t size) {
        return hashed_str(str);
    }
    
    // TODO: reversible hash
    // - For hash tables whose members are (nonnull) pointers, we could
    //   - store only the pointers and hash on demand
    //   - store only a reversible hash of the pointers and reverse on demand
    //   - work entirely with the hash as an ID with high entropy, only reversing
    //     it when we need to load through the pointer
        
} // namespace wry

#endif /* hash_hpp */
