//
//  hash.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef hash_hpp
#define hash_hpp

#include <arm_neon.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <cstring>
#include <tuple>
#include <utility>

#include "assert.hpp"
#include "stdint.hpp"

namespace wry {

    // splitmix64: the output function of Steele, Lea and Flood's SplitMix64
    // generator (2014), which is Stafford's Mix13 variant of the murmur3
    // 64-bit finalizer.  A bijection on the 64-bit words, with avalanche at
    // the measurement noise floor (2026-09-28), for two multiplies and three
    // xorshifts.  The generator adds 0x9e3779b97f4a7c15 to its state before
    // each call; this is the mixing function alone, so zero is a fixed
    // point: splitmix64(0) == 0.
    //
    // Pinned: term_hash (language-visible) and transaction_priority
    // (simulation-visible) are defined through it, so its constants must
    // never change.

    inline constexpr uint64_t splitmix64(uint64_t x) {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ull;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebull;
        x ^= x >> 31;
        return x;
    }

    // ranhash: Numerical Recipes (3rd ed.) 7.1.4, Ranhash::int64: an LCG
    // step, a xorshift triple, an odd multiply and a second xorshift triple.
    // Every step is invertible, so it is a bijection on the 64-bit words,
    // and NR's claim that ranhash(i++) passes statistical tests holds.
    //
    // It is not a good bit mixer, though.  Measured against the strict
    // avalanche criterion (2026-09-28): flipping some input bits flips some
    // output bits with probability exactly 1 (bias 0.5; rms bias 0.067 over
    // all bit pairs), where splitmix64 sits at the noise floor; and it costs
    // about 5.5 ns per dependent call against splitmix64's 3.8.  The cause
    // is structural: the only right shift in the final triple is by 41, so
    // output bits 23 and up receive nothing from above, and the lowest bit
    // changed by the last multiply, which always changes, reaches the output
    // unmixed whenever it lies at 23 or above.  Retained for reference; it
    // was hash until 2026-09-30.

    inline constexpr uint64_t ranhash(uint64_t x) {
        x = x * 3935559000370003845ull + 2691343689449507681ull;
        x ^= x >> 21; x ^= x << 37; x ^= x >> 4;
        x *= 4768777513237032717ull;
        x ^= x << 20; x ^= x >> 41; x ^= x << 5;
        return x;
    }

    // The general-purpose 64-bit hash, for process-local uses such as table
    // keys.  Nothing simulation-visible may depend on it, so it is free to
    // change; today it is splitmix64, which makes hash(0) == 0.

    inline constexpr uint64_t hash(uint64_t x) {
        return splitmix64(x);
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
    
    inline uint64_t _morton_expand_neon(uint64_t x) {
        return vmull_p64(x, x);
    }

    inline uint64_t _morton_from_xy_neon(int64_t x, int64_t y) {
        return vmull_p64(x, (int64_t)x << 1) | vmull_p64(y, y);
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


    struct hash_bytes_Hasher {

        uint64_t _state = 0;
        uint64_t _buffer = 0;
        uint64_t _count = 0;

        hash_bytes_Hasher& _write(unsigned char const* p, uint64_t n) {
            unsigned char* c = (unsigned char*)&_buffer;
            // Bytes consumed but not yet hashed
            uint64_t a = _count & 7;
            if (a) {
                // Copy into buffer
                size_t b = std::min(8 - a, n);
                std::memcpy(c + a, p, b);
                p += b;
                _count += b;
                n -= b;
                a = _count & 7;
                if (a) {
                    // Not enough data to complete an 8-byte chunk
                    return *this;
                }
                _state = hash(_state ^ _buffer);
            }
            // Consume 8-byte chunks
            while (n >= 8) {
                assert(!(_count & 7));
                std::memcpy(&_buffer, p, 8);
                p += 8;
                _count += 8;
                n -= 8;
                _state = hash(_state ^ _buffer);
            }
            // Copy remaining bytes into buffer
            std::memcpy(&_buffer, p, n);
            _count += n;
            // Done
            return *this;
        }

        hash_bytes_Hasher& write(void const* p, std::size_t n) {
            return _write((unsigned char const*)p, (uint64_t)n);
        }

        uint64_t finish() const {
            // Bytes that remain unprocessed
            size_t a = _count & 7;
            size_t b = _count;
            // Replace the high bytes of count.
            std::memcpy((unsigned char*)(&b + 1) - a, &_buffer, a);
            return hash(_state ^ b);
        }

    };

    // TODO: Could be a buffering adapter generic over 8-byte-chunked Hashers



    // hash_combine: fold one 64-bit value into a running 64-bit state, in
    // the shape Boost has used since 1.81: mix the sum of the state, a
    // constant and the value.  The sum rather than an xor matters.  With an
    // xor, a state equal to its value collapses to mix(0) == 0, and from a
    // zero state a run of zero values never leaves zero; with the constant
    // added, neither happens.  Measured 2026-09-30: avalanche over the 128
    // input bits at the noise floor, no collisions over all pairs of 12-bit
    // values or triples of 8-bit values from a zero seed, about 4 ns per
    // call.  Order-sensitive by construction.  There is no finalization; a
    // streaming hasher that folds in its count is the pending redesign.

    inline constexpr uint64_t hash_combine(uint64_t seed, uint64_t value) {
        return splitmix64(seed + 0x9e3779b97f4a7c15ull + value);
    }

    struct CombineHasher {
        uint64_t _state = 0;
        CombineHasher& write_u64(uint64_t i) {
            _state = hash_combine(_state, i);
            return *this;
        }
        uint64_t finish() const {
            return _state;
        }
    };

    // ---- Content hashes ----------------------------------------------
    //
    // content_hash(x) is a 64-bit word that is a function of x's content
    // alone: never of an address, a trie shape, or a build's layout, so two
    // processes holding the same state agree on it, and a loaded save
    // hashes as the state it was saved from.  It is the value side of the
    // per-entry hash below, and the word that stands in for a reference in
    // a hashed record (io/save_format.hpp), so it need not itself be well
    // mixed: every consumer mixes it with its position or code.  For an
    // integer it is the integer; for a Term the save encoding of an inline
    // word or the referent's content hash (term.hpp); for a garbage
    // collected object a cached hash of its saved body.  Overloads are
    // found by argument-dependent lookup, so each lives beside its type.

    template<std::integral T>
    constexpr uint64_t content_hash(T x) {
        return (uint64_t)x;
    }

    template<typename T> requires std::is_enum_v<T>
    constexpr uint64_t content_hash(T x) {
        return (uint64_t)x;
    }

    inline uint64_t content_hash(std::string_view x) {
        return hash_bytes_Hasher{}.write(x.data(), x.size()).finish();
    }

    // ---- Map and set entry hashes ------------------------------------
    //
    // A map or set hashes to the 64-bit wrapping SUM over its entries of
    // entry_hash(code, content_hash(value)), with a set member's value
    // hash zero.  The sum is commutative and associative, so a trie can
    // cache it per node (a node's hash is the sum of its children's), an
    // erase is a subtraction, and the result is a function of the entry
    // set alone: independent of trie shape, insertion order and symbol
    // width, and equal to the sum over the flat code-ordered entry record
    // the save writes.  The per-entry mix must be strongly non-linear in
    // both arguments, or moving one unit of value from cell a to cell b
    // would leave the sum unchanged; two chained Boost-shaped combines
    // give two splitmix64 rounds over each argument.  See
    // container/array_mapped_trie.hpp for the node-level maintenance.
    //
    // Pinned like splitmix64: the World hash is compared across peers and
    // across save/load, so the constants and the shape must not change
    // without a protocol/save version bump.

    inline constexpr uint64_t code_hash(uint64_t code) {
        return hash_combine(0, code);
    }

    inline constexpr uint64_t code_hash(__uint128_t code) {
        return hash_combine(hash_combine(0, (uint64_t)code), (uint64_t)(code >> 64));
    }

    template<typename Code>
    inline constexpr uint64_t entry_hash(Code code, uint64_t value_hash) {
        return hash_combine(code_hash(code), value_hash);
    }

    struct hash_u64_Hasher {
        uint64_t _state = 0;
        hash_u64_Hasher& write_u64(uint64_t i) {
            _state = hash_combine(_state, i);
            return *this;
        }
        uint64_t finish() const {
            return _state;
        }
    };

    // Rust-style Hashers

    struct FxHasher {
        uint64_t state = 0;
        void write_u64(uint64_t i) {
            state = (__builtin_rotateleft64(state, 5) ^ i) * 0x517cc1b727220a95;
        };
        uint64_t finish() const {
            return state;
        }
    };

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

    struct Fnv1aHasher {
        uint64_t _state = FNV1A_OFFSET_BASIS;
        void write(void const* p, std::size_t n) {
            auto q = (unsigned char const*)p;
            while (n--) {
                _state ^= *q++;
                _state *= FNV1A_PRIME;
            }
        }
        uint64_t finish() const {
            return _state;
        }
    };


    // TODO: hacked to fix inconsistency bug where Strings and const char* were
    // hashed differently.  If we want to use fnv1a, we need two slightly
    // different versions that are sized and zero-terminated respectively
    constexpr uint64_t hash(const char* str) {
        // return fnv1a(str);
        return hash_bytes_Hasher{}.write(str, strlen(str)).finish();
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
    
} // namespace wry

#endif /* hash_hpp */
