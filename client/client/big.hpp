//
//  big.hpp
//  client
//
//  Created by Antony Searle on 17/10/2023.
//

#ifndef big_hpp
#define big_hpp

#include "array.hpp"

namespace wry {
    
    namespace big {
        
        using u32 = std::uint32_t;
        using u64 = std::uint64_t;
        using i64 = std::uint64_t;

        // bignum tools
        
        inline void add(const u32*& a, const u32* b, const u32*& c, u32*& d, u32& carry) {
            while (a != b) {
                u64 x = (u64) *a++ + (u64) *b++ + (u64) carry;
                *d++ = (u32) x;
                carry = (u32) (x >> 32);
            }
        }

        inline std::tuple<const u64*, u64*, u64> add(const u64* first, const u64* last, const u64* first2, u64* d_first, u64 carry_in) {
            u64 carry_out = {};
            assert(carry_in <= 1);
            while (first != last) {
                // u64 x = (u64) *a++ + (u64) *b++ + (u64) carry;
                // u32 z = 0;
                // bool f = __builtin_add_overflow(*a++, *b++, &z);
                // bool g = __builtin_add_overflow(z, carry, d++);
                // carry = (u32) (x >> 32);
                *d_first++ = __builtin_addcll(*first++, *first2++, carry_in, &carry_out);
                carry_in = carry_out;
            }
            return {first2, d_first, carry_out};
        }

        inline void add(const u32*& a, const u32* b, u32*& d, u64& carry) {
            while (a != b) {
                assert(carry <= 0xFFFFFFFF00000000);
                carry += (u64) *a++;
                *d++ = (u32) carry;
                carry >>= 32;
            }
        }
        
        inline void add(u32*& a, u32* b, u64& carry) {
            while (a != b) {
                *a++ = (u32) carry;
                carry >>= 32;
            }
        }
        
        template<typename InputIt1, typename InputIt2>
        struct adder {
            
            InputIt1 a;
            InputIt2 b;
            u64 c;
            u64 carry;

            void _compute() {
                u64 s = {};
                u64 c1 = __builin_add_overflow(*a, *b, &s);
                u64 c2 = __builin_add_overflow(s, carry, &s);
                c = s;
                carry = c1 | c2;
            }
            
            adder(InputIt1 i, InputIt2 j)
            : a(i)
            , b(j)
            , carry(0) {
                _compute();
            }
            
            adder& operator++() {
                ++a;
                ++b;
                _compute();
                return *this;
            }
            
            auto operator++(int) {
                auto x = *this;
                operator++();
                return x;
            }
            
            u64 operator*() const {
                return c;
            }
            
            bool operator!=(auto&& last) const {
                return a != std::forward<decltype(last)>(last);
            }
            
        }; // struct adder
        
        
        template<typename InputIt1, typename InputIt2>
        struct subtractor {
            InputIt1 a;
            InputIt2 b;
            u64 c;
            u64 carry;

            void _compute() {
                u64 s = {};
                u64 c1 = __builin_sub_overflow(*a, *b, &s);
                u64 c2 = __builin_sub_overflow(s, carry, &s);
                c = s;
                carry = c1 | c2;
            }
            
            subtractor(InputIt1 i, InputIt2 j)
            : a(i)
            , b(j)
            , carry(0) {
                _compute();
            }
            
            subtractor& operator++() {
                ++a;
                ++b;
                _compute();
                return *this;
            }
            
            auto operator++(int) {
                auto x = *this;
                operator++();
                return x;
            }
            
            u64 operator*() const {
                return c;
            }
            
            bool operator!=(auto&& last) const {
                return a != std::forward<decltype(last)>(last);
            }
            
        }; // struct subtractor
        
        template<typename InputIt>
        struct multiplier {
            InputIt a;
            u32 b;
            u64 c;

            void _compute() {
                c += (u64) *a * (u64) b;
            }
            
            multiplier(InputIt i, u32 j, u64 k = 0)
            : a(i)
            , b(j)
            , c(k) {
                _compute();
            }
            
            multiplier& operator++() {
                ++a;
                c >>= 32;
                _compute();
                return *this;
            }
            
            auto operator++(int) {
                auto x = *this;
                operator++();
                return x;
            }
            
            u32 operator*() const {
                return (u32) c;
            }
            
            bool operator!=(auto&& last) const {
                return a != std::forward<decltype(last)>(last);
            }

        }; // struct multiplier
        
        template<typename InputIt>
        struct shifter {
            
            InputIt a;
            int b;
            u64 c;
            
            void _compute() {
                c |= ((u64) *a) << b;
            }
            
            shifter(InputIt i, int j, u64 k = 0)
            : a(i)
            , b(j)
            , c(k) {
                assert(b < 32);
            }
            
            shifter& operator++() {
                ++a;
                c >>= 32;
                _compute();
                return *this;
            }
            
            auto operator++(int) {
                auto x = *this;
                operator++();
                return x;
            }
            
            u32 operator*() const {
                return (u32) c;
            }

            bool operator!=(auto&& last) const {
                return a != std::forward<decltype(last)>(last);
            }
                        
        }; // struct shifter
        
        
        
        // d... = a...b - c...
        
        inline void sub(const u32*& a, const u32* b, const u32*& c, u32*& d, i64& carry) {
            assert(carry <= 0x7FFFFFFF00000000);
            assert(carry >= 0x80000000FFFFFFFF);
            while (a != b) {
                carry += (i64) (u64) *a++ - (i64) (u64) *c++;
                *d++ = (u32) carry;
                carry >>= 32;
            }
        }
        
        inline void sub(const u32*& a, const u32* b, u32*& d, i64& carry) {
            assert(carry <= 0x7FFFFFFF00000000);
            while (a != b) {
                carry += (i64) (u64) *a++;
                *d++ = (u32) carry;
                carry >>= 32;
            }
        }
        
        inline void subip(u32*& a, const u32* b, i64& carry) {
            while ((a != b) && carry) {
                carry += (i64) (u64) *a;
                *a++ = (u32) carry;
                carry >>= 32;
            }
        }
        
        inline void sub(u32*& a, const u32* b, i64& carry) {
            while (a != b) {
                *a++ = (u32) carry;
                carry >>= 32;
            }
        }
        
        // e... = a...b * c + d...
        
        inline void mul(const u32*& a, const u32*b, const u32 c, const u32*& d, u32* e, u64& carry) {
            // (2^32 - 1) * (2^32 - 1)
            // = 2^64 - 2^33 + 1
            // = (2^64 - 1) - 2 * (2^32 - 1)
            assert(carry <= 0x00000000FFFFFFFF);
            while (a != b) {
                carry += (u64) *a++ * (u64) c + *d++;
                *e++ = (u32) carry;
                carry >>= 32;
            }
        }
        
        inline void lsl(const u32*& a, const u32* b, int c, u32*& d, u64& carry) {
            assert(!(carry >> c));
            while (a != b) {
                carry |= (u64) *a++ << c;
                *d++ = (u32) carry;
                carry >>= 32;
            }
        }
        
        inline int popcount(const u64* a, const u64* b) {
            int c = 0;
            while (a != b) {
                c += __builtin_popcountll(*a++);
            }
            return c;
        }
        
        // on output, a points to the limb containing the first set bit
        inline int ctz(const u64*& a, const u64* b) {
            int c = 0;
            for (;;) {
                if (a == b)
                    // all zeros
                    return c;
                if (u64 d = *a)
                    // found first set
                    return c + __builtin_ctzll(d);
                c += 64;
                ++a;
            }
        }

        // on output, a points to the lib containing the first set bit, and
        // the returned value is the zero-based index of the bit in that limb
        // if no such bit, a = b
        inline int ffs(const u64*& a, const u64* b) {
            for (;;) {
                if (a == b)
                    // all zeros
                    return 0;
                if (u64 d = *a)
                    // found first set
                    return __builtin_ctzll(d);
                ++a;
            }
        }
        
        
        struct UnsignedInteger {
            
            Array<u64> _limbs;
            
            bool _invariant() {
                return _limbs.empty() || _limbs.back();
            }
            
            void _canonicalize() {
                while (!_limbs.empty() && !_limbs.back())
                    _limbs.pop_back();
            }
            
        };
        
        /*
        UnsignedInteger operator+(const UnsignedInteger& a, const UnsignedInteger& b) {
            if (a._limbs.size() > b._limbs.size())
                return b + a;
            UnsignedInteger c;
            c._limbs.reserve(b._limbs.size() + 1);
            const u32* aa = a._limbs.begin();
            const u32* ab = a._limbs.end();
            const u32* bc = b._limbs.begin();
            const u32* bb = b._limbs.end();
            u32* cd = c._limbs._end;
            u32* cb = c._limbs._allocation_end;
            u64 carry = 0;
            add(aa, ab, bc, cd, carry);
            add(bc, bb, cd, carry);
            add(cd, cb, carry);
            assert(carry == 0);
            return c;
        }
         */
        
        UnsignedInteger operator+(const UnsignedInteger& a, const UnsignedInteger& b) {
            if (a._limbs.size() > b._limbs.size())
                return b + a;
            UnsignedInteger c;
            c._limbs.reserve(b._limbs.size() + 1);
            const u64* p = a._limbs._begin;
            const u64* q = b._limbs._begin;
            u64 carry = 0;
            while (p != a._limbs._end)
                *c._limbs._end++ = __builtin_addcll(*p++, *q++, carry, &carry);
            while (q != b._limbs._end)
                carry = (u64) __builtin_add_overflow(*q++, carry, c._limbs._end++);
            if (carry)
                *c._limbs._end++ = carry;
            return c;
        }
        
        UnsignedInteger operator-(const UnsignedInteger& a, const UnsignedInteger& b) {
            assert(!(a._limbs.size() < b._limbs.size()));
            UnsignedInteger c;
            c._limbs.reserve(a._limbs.size());
            const u64* p = a._limbs._begin;
            const u64* q = b._limbs._begin;
            u64 carry = 0;
            while (p != a._limbs._end)
                *c._limbs._end++ = __builtin_subcll(*p++, *q++, carry, &carry);
            while (q != b._limbs._end)
                carry = (u64) __builtin_sub_overflow(*q++, carry, c._limbs._end++);
            assert(!carry);
            c._canonicalize();
            return c;
        }
        
        


    } // namespace big
    
} // namespace wry

#endif /* big_hpp */
