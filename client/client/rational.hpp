//
//  rational.hpp
//  client
//
//  Created by Antony Searle on 13/10/2023.
//

#ifndef rational_hpp
#define rational_hpp

#include <cassert>
#include <numeric>
#include "stdint.hpp"
#include "utility.hpp"

namespace wry {
    
    template<typename T>
    T binary_gcd(T a, T b) {
        if (a < 0)
            a = -a;
        if (b < 0)
            b = -b;
        if (a == 0)
            return b;
        if (b == 0)
            return a;
        // save common factors of two
        int d = __builtin_ctzll(a | b);
        // remove any factors of two
        a >>= __builtin_ctzll(a);
        b >>= __builtin_ctzll(b);
        // a and b are now odd
        while (a != b) {
            assert(a > 0);
            assert(a & 1);
            assert(b > 0);
            assert(b & 1);
            if (a < b) {
                using std::swap;
                swap(a, b);
            }
            a -= b;
            a >>= __builtin_ctzll(a);
        }
        return a << d;
    }
    
    
    template<typename T = long long>
    struct rational {
        
        T a, b;
        
        rational() : a(0), b(1) {}
        rational(const rational&) = default;
        
        bool _invariant() {
            return (b != 0) && (binary_gcd(a, b) == 1);
        }
        
        void _reduce() {
            assert(b != 0);
            if (a == 0) {
                b = 1;
                return;
            }
            int c = __builtin_ctzll(a | b);
            a >>= c;
            b >>= c;
            if (b < 0) {
                b = -b;
                a = -a;
            }
            T u = a;
            T v = b;
            if (u < 0)
                u = -u;
            u >>= __builtin_ctzll(u);
            v >>= __builtin_ctzll(v);
            while (u != v) {
                if (u < v) {
                    using std::swap;
                    swap(u, v);
                }
                u -= v;
                u >>= __builtin_ctzll(u);
            }
            a /= u;
            b /= u;
        }
        
        rational(T numerator, T denominator)
        : a(numerator)
        , b(denominator) {
            _reduce();
        }

        static T add(T a, T b) {
            T c = {};
            if(__builtin_add_overflow(a, b, &c))
                throw EOVERFLOW;
            return c;
        }
        
        static T sub(T a, T b) {
            T c = {};
            if (__builtin_sub_overflow(a, b, &c))
                throw EOVERFLOW;
            return c;
        }

        static T mul(T a, T b) {
            T c = {};
            if (__builtin_mul_overflow(a, b, &c))
                throw EOVERFLOW;
            return c;
        }
        
        rational& operator++() {
            a = add(a, b);
            _reduce();
            return *this;
        }

        rational& operator--() {
            a = sub(a, b);
            _reduce();
            return *this;
        }

        rational operator+(rational y) {
            T c = mul(a, y.b);
            T d = mul(y.a, b);
            T e = add(c, d);
            T f = mul(b, y.b);
            return rational(e, f);
        }
        
        rational operator-(rational y) {
            T c = mul(a, y.b);
            T d = mul(y.a, b);
            T e = sub(c, d);
            T f = mul(b, y.b);
            return rational(e, f);
        }
        rational operator*(rational y) {
            return rational(mul(a, y.a), mul(b, y.b));
        }

        rational operator/(rational y) {
            return rational(mul(a, y.b), mul(b, y.a));
        }
        
        rational& operator+=(rational y) {
            return operator=(*this + y);
        }

        rational& operator-=(rational y) {
            return operator=(*this - y);
        }
        
        rational& operator*=(rational y) {
            a *= y.a;
            b *= y.b;
            _reduce();
            return *this;
        }
        
        rational& operator/=(rational y) {
            assert(y.a);
            a *= y.b;
            b *= y.a;
            _reduce();
            return *this;
        }

    };
    
    
    
} // namespace wry

#endif /* rational_hpp */
