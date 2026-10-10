//
//  random.hpp
//  client
//
//  Created by Antony Searle on 28/9/2026.
//

#ifndef random_hpp
#define random_hpp

#include <cmath>

#include "stdint.hpp"

namespace wry {

    template<typename T>
    auto sqr(const T& t) {
        return t * t;
    }

    // Numerical Recipes (3rd ed.) 7.1.3, Ranq1: a 64-bit xorshift generator
    // with the shift triple (21, 35, 4) followed by a multiplicative output
    // scrambler, the construction later named xorshift64* by Vigna.
    //
    // The xorshift step is a full-period linear map on the nonzero 64-bit
    // words, so the state visits all 2^64 - 1 of them and is never zero, and
    // the multiplier is odd, so the output is never zero either: the range
    // is [1, 2^64 - 1], which is what min() and max() report.
    //
    // The one hazard is the zero state, a fixed point that emits zeros
    // forever.  The constructor xors the seed into a fixed constant, so the
    // seed equal to that constant (4101842887655102017) is forbidden.  NR3
    // additionally advances the state once after seeding; that step is
    // omitted here, so the first draw differs from NR3's for the same seed.
    //
    // Apart from the final multiply the output is linear in the state, so
    // this is a quick generator, not a statistically strong one, and it is
    // not a cryptographic one.  NR3 suggests it for up to about 10^12 draws.
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

    // Uniform on (0, 1]: the 64-bit draw scaled by 2^-64.  The draw is never
    // zero, so neither is the result; the topmost draws round up to exactly
    // 1.0, with probability 2^-54.
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

    // Normal deviate by Leva's ratio-of-uniforms method (ACM TOMS 18(4),
    // 1992), as in NR3 7.3.9 Normaldev.  A candidate (u, v) is drawn from
    // the rectangle (0, 1] x [-sqrt(2/e), sqrt(2/e)] (1.7156 is that
    // width); the quadratic q tests the candidate against two bounding
    // curves of the acceptance region, so the exact test, which needs the
    // logarithm, runs for only about one candidate in a hundred.  The
    // deviate is the ratio v / u.
    template<typename T = uniform_distribution<>>
    class normal_distribution {

        T _x;

    public:

        explicit normal_distribution(uint64_t seed = 0) : _x(seed) {}

        double operator()() {
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

} // namespace wry

#endif /* random_hpp */
