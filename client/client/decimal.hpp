//
//  decimal.hpp
//  client
//
//  Created by Antony Searle on 16/3/2024.
//

#ifndef decimal_hpp
#define decimal_hpp

#include <cinttypes>

#include "charconv.hpp"
#include "stdint.hpp"
#include "string.hpp"

namespace wry::decimal {
    
    char8_t* digits_add(const char8_t* first,
                        const char8_t* last,
                        char8_t* d_last,
                        int carry = 0) 
    {
        for (; first != last;) {
            int a = *--d_last;
            int b = *--last;
            int i = a + (b - u8'0') + carry;
            if ((carry = (i > u8'9')))
                i -= 10;
            *d_last = i;
        }
        char8_t* p = d_last;
        while (carry) {
            int a = *--p;
            int i = a + carry;
            if ((carry = (i > u8'9')))
                i -= 10;
            *p = i;
        }
        return d_last;
    };
    
    char8_t* digits_subtract(const char8_t* first,
                             const char8_t* last,
                             char8_t* d_last,
                             int borrow = 0)
    {
        for (; first != last;) {
            int a = *--d_last;
            int b = *--last;
            int i = a - (b - u8'0') - borrow;
            if ((borrow = (i < u8'0')))
                i += 10;
            *d_last = i;
        }
        char8_t* p = d_last;
        while (borrow) {
            int a = *--p;
            int i = a - borrow;
            if ((borrow = (i < u8'0')))
                i += 10;
            *p = i;
        }
        return d_last;
    };
    
    char8_t* digits_multiply(const char8_t* first,
                             const char8_t* last,
                             char8_t* d_last,
                             int multiplier,
                             int carry = 0) {
        for (; first != last;) {
            int a = *--d_last;
            int b = *--last;
            int i = (a - u8'0') + (b - u8'0') * multiplier + carry;
            *d_last = (i % 10) + u8'0';
            carry = i / 10;
        }
        char8_t* p = d_last;
        while (carry) {
            int a = *--p;
            int b = *--last;
            int i = (a - u8'0') + (b - u8'0') * multiplier + carry;
            *p = (i % 10) + u8'0';
            carry = i / 10;
        }
        return d_last;
    }

    
    struct Decimal {
        
        // Represent a number as a UTF-8 string in JSON number format
        
        // Pro:
        // - Fast display
        // - Provides exact representation of decimals
        // - Provides arbitrary precision arithmetic
        // - Defers JSON parsing and type choice
        // Con:
        // - Slow arithmetic
        // - Heap allocated
        // - Space inefficient
        
        ArrayView<char8_t> data;
        
        StringView as_StringView() const;
        
        bool is_integer() const;

        bool try_as(auto&) const;

        int as_int() const;
        int64_t as_int64_t() const;
        uint64_t as_uint64_t() const;
        double as_double() const;
        
    };
    
    struct Descriptor {
        
        ArrayView<const char8_t> integer;
        ArrayView<const char8_t> fraction;
        int exponent;
        bool is_negative;
        
        explicit Descriptor(ArrayView<const char8_t> a) {
            
            exponent = 0;
            is_negative = false;
                                    
            integer._begin = a.begin();
            const char8_t* last = a.end();
            if (integer._begin == last)
                // empty string is interpreted as 0
                return;
                        
            if ((is_negative = (*integer._begin == u8'-')))
                ++integer._begin;
            
            integer._end = integer._begin;
            char8_t c;
            const char8_t* first;
            
        continue_integer:
            
            if (integer._end == last) {
                assert(!integer.empty());
                return;
            }
            c = *integer._end;
            switch (c) {
                case u8'.':
                    assert(!integer.empty());
                    fraction._begin = integer._end + 1;
                    fraction._end = fraction._begin;
                    goto continue_fraction;
                case u8'E':
                case u8'e':
                    first = integer._end + 1;
                    goto parse_exponent;
                default:
                    assert(isdigit(c));
                    ++integer._end;
                    goto continue_integer;
            }
            
        continue_fraction:
            
            if (fraction._end == last)
                return;
            c = *fraction._end;
            switch (c) {
                case u8'E':
                case u8'e':
                    first = fraction._end + 1;
                    goto parse_exponent;
                default:
                    assert(isdigit(c));
                    ++fraction._end;
                    goto continue_fraction;
            }
            
        parse_exponent:
            
            if (first == last)
                return;
            if (*first == u8'+') // skip +
                ++first;
            std::from_chars_result result;
            result = std::from_chars((const char*) first,
                                     (const char*) last,
                                     exponent);
            assert(result.ptr == (const char*) last);

        }

        // the first digit is of the form d * 10^(high_place - 1)
        int high_place() const {
            return (int) integer.size() + exponent;
        }
        
        // the last digit is of the form d * 10^low_place
        int low_place() const {
            return exponent - (int) fraction.size();
        }
                
    };
    
    
    String string_add(ArrayView<const char8_t> a, ArrayView<const char8_t> b) {
        
        // Our goal here is to perform subtraction on strings directly without
        // parsing into an intermediate binary representation

        String result;

        Descriptor c(a);
        Descriptor d(b);
        
        int left = std::max(c.high_place(), d.high_place());
        int right = std::min(c.low_place(), d.low_place());
        
        bool is_negative = c.is_negative;
        int exponent = right;
        
        result.chars.resize(left - right + 1, u8'0');
        
        {
            char8_t* d_first = 
            std::copy(c.integer.begin(),
                      c.integer.end(),
                      result.chars.to(left - c.high_place() + 1));
            std::copy(c.fraction.begin(),
                      c.fraction.end(),
                      d_first);
        }
        
        if (c.is_negative == d.is_negative) {
            char8_t* d_last =
            digits_add(d.fraction.begin(),
                       d.fraction.end(),
                       result.chars.to(left - d.low_place() + 1));
            digits_add(d.integer.begin(),
                       d.integer.end(),
                       d_last);
        } else {
            // trap overflow
            assert(result.chars.front() == u8'0');
            result.chars.front() = u8'1';
            char8_t* d_last =
            digits_subtract(d.fraction.begin(),
                            d.fraction.end(),
                            result.chars.to(left - d.low_place() + 1));
            digits_subtract(d.integer.begin(),
                            d.integer.end(),
                            d_last);
            if (result.chars.front() == u8'1') {
                result.chars.front() = u8'0';
            } else {
                // we have to negate everything
                char8_t* first = result.chars.begin() + 1;
                char8_t* last = result.chars.end();
                int borrow = 0;
                while (last-- != first) {
                    int i = *last;
                    *last = (10 - (i - u8'0') - borrow) + u8'0';
                    borrow = 1;
                }
                is_negative = !is_negative;
            }
        }
        
        // we now have a technically correct result that needs to be
        // canonicalized
        
        // discard leading zeros
        while (!result.chars.empty() && result.chars.front() == u8'0') {
            result.chars.pop_front();
        }
        
        // discard trailling zeros
        while (!result.chars.empty() && result.chars.back() == u8'0') {
            result.chars.pop_back();
            exponent += 1;
        }
        
        // clean up zero
        if (result.chars.empty()) {
            result.chars.push_back(u8'0');
            is_negative = 0;
            exponent = 0;
        }
        
        // we now have 
        //     (is_negative, "12456789", exponent)
        // representing
        //     -123456789e123
        
        {
            int P = 6;
            int X = exponent + (int) result.chars.size() - 1;
            if ((P > X) && (X >= -4)) {
                // style %f
                int N = (int) result.chars.size() + exponent;
                while (N > result.chars.size()) {
                    result.chars.push_back(u8'0');
                    --exponent;
                }
                while (N <= 0) {
                    result.chars.push_front(u8'0');
                    ++N;
                }
                if (N < result.chars.size()) {
                    result.chars.insert(result.chars.to(N), u8'.');
                    exponent = 0;
                } else {
                    assert(exponent == 0);
                }
            } else {
                if (1 < result.chars.size()) {
                    result.chars.insert(result.chars.to(1), u8'.');
                    exponent += (int) result.chars.size() - 2;
                }
            }
        }
        
        if (is_negative)
            result.chars.push_front(u8'-');
        if (exponent) {
            result.chars.reserve(11);
            result.chars.push_back(u8'e');
            std::to_chars_result s =
            std::to_chars((char*) result.chars._end,
                          (char*) result.chars._allocation_end,
                          exponent);
            result.chars._end = (char8_t*) s.ptr;
        }

        return result;
    }
    
    
    bool Decimal::try_as(auto& value) const {
        const char* first = (const char*) data.begin();
        const char* last = (const char*) data.end();
        std::from_chars_result result =
        wry::from_chars(first,
                        last,
                        value);
        return result.ptr == last;
    }
    
    
} // namespace wry::decimal

#endif /* decimal_hpp */
