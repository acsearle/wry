//
//  number_caption.cpp
//  client
//
//  Created by Antony Searle on 2026-11-08.
//

#include "number_caption.hpp"
#include "test.hpp"

namespace wry {

    std::string number_caption(std::int64_t value) {
        // The magnitude in unsigned arithmetic, so INT64_MIN negates
        const std::uint64_t magnitude = value < 0
            ? std::uint64_t{0} - (std::uint64_t)value
            : (std::uint64_t)value;
        std::string result = value < 0 ? "-" : "";
        if (magnitude < 10000) {
            result += std::to_string(magnitude);
            return result;
        }
        // The largest unit whose mantissa is at least one; 10^18 (E) is
        // the last, and UINT64_MAX / 10^18 is 18, so the mantissa stays
        // under 1000 at every step taken
        static constexpr char kSuffix[] = "kMGTPE";
        int k = 0;
        std::uint64_t unit = 1000;
        while (k + 1 < 6 && magnitude / (unit * 1000) >= 1) {
            unit *= 1000;
            ++k;
        }
        const std::uint64_t mantissa = magnitude / unit;
        result += std::to_string(mantissa);
        if (mantissa < 10) {
            result += '.';
            result += (char)('0' + (magnitude / (unit / 10)) % 10);
        }
        result += kSuffix[k];
        return result;
    }

    define_test("number_caption") {
        // In full below ten thousand
        assert(number_caption(0) == "0");
        assert(number_caption(7) == "7");
        assert(number_caption(-1) == "-1");
        assert(number_caption(999) == "999");
        assert(number_caption(1000) == "1000");
        assert(number_caption(9999) == "9999");
        assert(number_caption(-9999) == "-9999");
        // Thousands: whole mantissas only, truncated
        assert(number_caption(10000) == "10k");
        assert(number_caption(-10000) == "-10k");
        assert(number_caption(12345) == "12k");
        assert(number_caption(-98765) == "-98k");
        assert(number_caption(999999) == "999k");
        // Millions and up: one decimal under ten, never rounded up
        assert(number_caption(1000000) == "1.0M");
        assert(number_caption(1234567) == "1.2M");
        assert(number_caption(-1234567) == "-1.2M");
        assert(number_caption(9999999) == "9.9M");
        assert(number_caption(10000000) == "10M");
        assert(number_caption(123456789) == "123M");
        assert(number_caption(999999999) == "999M");
        assert(number_caption(1000000000) == "1.0G");
        assert(number_caption(1200000000000) == "1.2T");
        assert(number_caption(999999999999999) == "999T");
        assert(number_caption(1000000000000000) == "1.0P");
        assert(number_caption(999999999999999999) == "999P");
        assert(number_caption(1000000000000000000) == "1.0E");
        // The int64 extremes
        assert(number_caption(INT64_MAX) == "9.2E");
        assert(number_caption(INT64_MIN) == "-9.2E");
        assert(number_caption(INT64_MIN + 1) == "-9.2E");
        // Never more than four characters after the sign, every magnitude
        for (std::uint64_t m = 1; m < (std::uint64_t)INT64_MAX / 7; m = m * 7 + 3) {
            for (std::int64_t v : {(std::int64_t)m, -(std::int64_t)m, (std::int64_t)m - 1}) {
                const std::string s = number_caption(v);
                assert(s.size() - (v < 0 ? 1 : 0) <= 4);
            }
        }
        co_return;
    };

} // namespace wry
