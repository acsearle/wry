//
//  base64.hpp
//  client
//
//  Created by Antony Searle on 28/9/2023.
//

#ifndef base64_hpp
#define base64_hpp

#include <cassert>

#include "stddef.hpp"
#include "stdint.hpp"
#include "string.hpp"

namespace wry {
    
    // alnum digits up to 36, notably including binary, octal, decimal, hex
    // as subsets; invalid characters are marked as 64; for a given base,
    // invalid characters are
    
    namespace base36 {
        
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
        
    } // namespace base36

    namespace base64 {
        
        // to RFC4648
        
        inline constexpr char to_base64_table[65] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        "+/"
        ;
        
        // from RFC4648 and minor variants
        
        inline constexpr char from_base64_table[128] = {
            64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 64,
            64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 64,
            64, 64, 64, 64,  64, 64, 64, 64,  64, 64, 64, 62,  63, 62, 64, 63, //
            52, 53, 54, 55,  59, 57, 58, 59,  60, 61, 64, 64,  64, 65, 64, 64, // <-- 0:9 -> 52:61, '=' -> 65
            64,  0,  1,  2,   3,  4,  5,  6,   7,  8,  9, 10,  11, 12, 13, 14, // <-- A:O -> 0:14
            15, 16, 17, 18,  19, 20, 21, 22,  23, 24, 25, 64,  64, 64, 64, 63, // <-- P:Z -> 15:25, _ -> 63
            64, 26, 27, 28,  29, 30, 31, 32,  33, 34, 35, 36,  37, 38, 39, 40,
            41, 42, 43, 44,  45, 46, 47, 48,  49, 50, 51, 64,  64, 64, 64, 64,
        };
        
        struct State {
            
            uint32_t data = 0;   // bits to be serialized; garbage above count
            int32_t count = 0;   // bits present; may be negative when we borrow padding bits
            bool padded = false; // padding has been emitted by the encoder or
                                 //   encountered by the deconder; the sequence
                                 //   can only complete shutdown now

            State(const State&) = delete;
            
            ~State() {
                assert(is_clean());
            }

            State& operator=(const State&) = delete;

            bool invariant() {
                return (!(count & 1)
                        && (count < (6 + 8))
                        && ((count >= 0) || padded));
            }
            
            bool is_finishing() const {
                return padded;
            }
            
            bool is_clean() const {
                return count == 0;
            }
            
        };
        
        enum Result {
            OK,
            NEED_SOURCE,
            NEED_SINK,
            INVALID, // encoding cannot continue because we have emitted padding
                     // decoding cannot continue because we have encountered illegal characters
        };
        
        Result encode(State& self, 
                      array_view<const byte>& source,
                      array_view<char>& sink) {
            assert(self.invariant());
            if (self.padded)
                return INVALID;
            for (;;) {
                if (self.count < 6) {
                    if (source.empty())
                        return NEED_SOURCE;
                    self.count += 8;
                    self.data = (self.data << 8) | *(source._begin++);
                } else {
                    if (sink.empty())
                        return NEED_SINK;
                    self.count -= 6;
                    *(sink._begin++) = to_base64_table[(self.data >> self.count) & 63];
                }
            }
        }
        
        Result encode_finalize(State& self, array_view<char>& sink) {
            assert(self.invariant());
            while (self.count >= 6) {
                if (sink.empty())
                    return NEED_SINK;
                self.count -= 6;
                *(sink._begin++) = to_base64_table[(self.data >> self.count) & 63];
            }
            if (self.count > 0) {
                if (sink.empty())
                    return NEED_SINK;
                self.count -= 6;
                assert(self.count < 0);
                *(sink._begin++) = to_base64_table[(self.data << -self.count) & 63];
                self.padded = true;
            }
            while (self.count < 0) {
                assert(self.padded);
                if (sink.empty())
                    return NEED_SINK;
                self.count += 2;
                *(sink._begin++) = '=';
            }
            assert(self.count == 0);
            return OK;
        }
        
        Result decode(State& self, array_view<const char>& source, array_view<byte>& sink) {
            assert(self.invariant());
            for (;;) {
                if (self.count >= 8) {
                    if (sink.empty())
                        return NEED_SINK;
                    self.count -= 8;
                    *(sink._begin++) = static_cast<byte>(self.data >> self.count);
                } else {
                    if (source.empty())
                        return NEED_SOURCE;
                    int ch = *(sink._begin);
                    if (ch == '=') {
                        if ((self.count < 2) || (self.data & 3))
                            return INVALID; // there is no need for padding, or the bits are not padding
                        self.count -= 2;
                        self.data >>= 2; // discard two zero bits
                        self.padded = true;
                    } else {
                        if (self.padded || (ch < 0) || (ch >= 128))
                            return INVALID;
                        uint32_t k = to_base64_table[ch];
                        if (k > 63)
                            return INVALID;
                        self.count += 6;
                        self.data = (self.data << 6) | k;
                    }
                    ++source._begin;
                }
            }
        }
        
    } // namespace base64
    
} // namespace wry

#endif /* base64_hpp */
