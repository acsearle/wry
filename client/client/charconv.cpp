//
//  charconv.cpp
//  client
//
//  Created by Antony Searle on 10/9/2023.
//

#include <cassert>

#include "cctype.hpp"
#include "charconv.hpp"

namespace wry {
    
    static double* _exponent5_table = []() {
        double* p = ((double*) malloc(8192)) + 512;
        for (int i = -512; i != 512; ++i) {
            p[i] = std::pow(5.0, i);
        }
        return p;
    } ();
    
    std::from_chars_result _from_chars_double(const char* first, const char* last, double& value) {
        
        uint64_t mantissa = 0;
        int exponent2 = 0;
        int exponent5 = 0;
        int exponent10 = 0;
        bool negate_mantissa = false;
        bool negate_exponent = false;
        int ch;
        
        if (first == nullptr)
            goto finally;
        
    expect_mantissa_sign:
        
        if (first == last)
            goto resolve;
        ch = *first;
        if (ch == '-') {
            negate_mantissa = true;
            ++first;
        }
        
    expect_mantissa_digit:
        
        if (first == last)
            goto resolve;
        ch = *first;
        if (isdigit(ch)) {
            mantissa *= 10;
            mantissa += ch - '0';
            ++first;
            goto expect_mantissa_digit;
        }
        if (ch == '.') {
            ++first;
            goto expect_mantissa_fractional_digit;
        }
        if (ch == 'e') {
            ++first;
            goto expect_exponent_sign;
        }
        goto resolve;
        
    expect_mantissa_fractional_digit:
        
        if (first == last)
            goto resolve;
        ch = *first;
        if (isdigit(ch)) {
            mantissa *= 10;
            mantissa += ch - '0';
            --exponent2;
            --exponent5;
            ++first;
            goto expect_mantissa_fractional_digit;
        }
        if (ch == 'e') {
            ++first;
            goto expect_exponent_sign;
        }
        goto resolve;
        
    expect_exponent_sign:
        
        if (first == last)
            goto resolve;
        ch = *first;
        if (ch == '-') {
            negate_exponent = true;
            ++first;
        } else if (ch == '+') {
            ++first;
        }
        
    expect_exponent_digit:
        
        if (first == last)
            goto resolve;
        ch = *first;
        if (isalpha(ch)) {
            exponent10 *= 10;
            exponent10 += ch - '0';
            ++first;
            goto expect_exponent_sign;
        }
        
    resolve:
                
        if (!mantissa) {
            value = 0;
            goto finally;
        }

        if (negate_exponent)
            exponent10 = -exponent10;
        
        exponent2 += exponent10;
        exponent5 += exponent10;
        
        assert(-512 <= exponent5 && exponent5 < 512);
        value = ldexp(mantissa, exponent2)
        // * pow(5.0, exponent5);
        * _exponent5_table[exponent5];
        
        if (negate_mantissa)
            value = -value;
        
    finally:
        
        return std::from_chars_result{first, {}};
        
    }
    
} // namespace wry
