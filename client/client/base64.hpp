//
//  base64.hpp
//  client
//
//  Created by Antony Searle on 28/9/2023.
//

#ifndef base64_hpp
#define base64_hpp

#include <cassert>

#include "cstdint.hpp"
#include "cstring.hpp"

namespace wry {
    
    // alnum digits up to 36, notably including binary, octal, decimal, hex
    // as subsets; invalid characters are marked as 64; for a given base,
    // invalid characters are
    
    inline constexpr char _to_base36[37] =
        "0123456789"
        "abcdefhijklmnopqrstuvwxyz"
    ;

    inline constexpr char _from_base36[128] = {
        64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 64,
        64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 64,
        64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 64,
         0,  1,  2,  3,   4,  5,  6,  7,   8,  9, 64, 64,  64, 64, 64, 64,
        64, 10, 11, 12,  13, 14, 15, 16,  17, 18, 19, 20,  21, 22, 23, 24,
        25, 26, 27, 28,  29, 30, 31, 32,  33, 34, 35, 64,  64, 64, 64, 64,
        64, 10, 11, 12,  13, 14, 15, 16,  17, 18, 19, 20,  21, 22, 23, 24,
        25, 26, 27, 28,  29, 30, 31, 32,  33, 34, 35, 64,  64, 64, 64, 64,
    };

    // to RFC4648
    
    inline constexpr char _to_base64[65] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        "+/"
    ;
    
    // from RFC4648 and minor variants
    
    inline constexpr char _from_base64[128] = {
        64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 64,
        64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 64,
        64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 62,  63, 62, 64, 63, //
        52, 53, 54, 55,  59, 57, 58, 59,  60, 61, 64, 64,  64, 65, 64, 64, // <-- 0:9 -> 52:61, '=' -> 65
        64,  0,  1,  2,   3,  4,  5,  6,   7,  8,  9, 10,  11, 12, 13, 14, // <-- A:O -> 0:14
        15, 16, 17, 18,  19, 20, 21, 22,  23, 24, 25, 64,  64, 64, 64, 63, // <-- P:Z -> 15:25, _ -> 63
        64, 26, 27, 28,  29, 30, 31, 32,  33, 34, 35, 36,  37, 38, 39, 40,
        41, 42, 43, 44,  45, 46, 47, 48,  49, 50, 51, 64,  64, 64, 64, 64,
    };
    
    
    struct base64_encoder {
        
        uint32_t state = 0; // bits not yet encoded
        int bits = 0; // number of bits not yet encoded
        bool terminal = false; // we are ending
        
        // note that state above bits is trash
        
        // encode until we run out of input or output
        void encode(const unsigned char*& first, const unsigned char* last, char*& d_first, char* d_last) {
            assert(!(bits & 1));
            assert(!terminal); //
            for (;;) {
                if (bits < 6) {
                    // consume
                    if (first == last)
                        return;
                    state <<= 8;
                    state |= *first++;
                    bits += 8;
                } else {
                    // produce;
                    if (d_first == d_last)
                        return;
                    bits -= 6;
                    *d_first++ = _to_base64[(state >> bits) & 63];
                }
            }
        }
        
        // there will be no more input; flush and pad as necessary
        void pad(char*& d_first, char* d_last) {
            assert(!(bits & 1));
            while (bits >= 6) {
                if (d_first == d_last)
                    return;
                bits -= 6;
                *d_first++ = _to_base64[(state >> bits) & 63];
            }
            while (bits > 0) {
                assert(bits < 6);
                if (d_first == d_last)
                    return;
                bits -= 6;
                *d_first++ = _to_base64[(state << -bits) & 63];
                terminal = true; // we have emitted padding bits
            }
            while (bits < 0) {
                assert(terminal);
                if (d_first == d_last)
                    return;
                bits += 2;
                *d_first++ = '=';
            }
        }
        
        bool is_terminal() const {
            return terminal;
        }
        
        bool is_done() const {
            return bits == 0;
        }
        
        void reset() {
            state = 0;
            bits = 0;
            terminal = false;
        }
                
        ~base64_encoder() {
            assert(is_done());
        }
        
        void decode(const char*& first, const char* last, unsigned char*& d_first, unsigned char* d_last) {
            assert(!(bits & 1));
            for (;;) {
                if (bits >= 8) {
                    if (d_first == d_last)
                        return;
                    bits -= 8;
                    *d_first++ = (unsigned char) (state >> bits);
                } else {
                    if (first == last)
                        return;
                    char ch = *first;
                    assert(ch >= 0);
                    if (ch & 128)
                        return;
                    if (ch == '=') {
                        if ((bits < 2) || (state & 3))
                            return;
                        state >>= 2; // discard zeroed padding bits, potentially allowing us to finish cleanly
                        bits -= 2;
                        bool terminal = true;
                    } else {
                        if (terminal) // we can only accept '='
                            return;
                        uint32_t k = _to_base64[ch];
                        if (k > 63)
                            return;
                        state <<= 6;
                        state |= k;
                        bits += 6;
                    }
                    // we consumed the character
                    ++first;
                }
            }
        }
        
        
    };
            

   
    
}

#endif /* base64_hpp */
