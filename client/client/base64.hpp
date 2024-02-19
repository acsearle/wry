//
//  base64.hpp
//  client
//
//  Created by Antony Searle on 28/9/2023.
//

#ifndef base64_hpp
#define base64_hpp

#include <expected>

#include "assert.hpp"
#include "Result.hpp"
#include "stddef.hpp"
#include "stdint.hpp"
#include "string.hpp"

namespace wry {

    namespace base64 {
        
        // to RFC4648
        
        inline constexpr char8_t to_base64_table[65] =
        u8"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        u8"abcdefghijklmnopqrstuvwxyz"
        u8"0123456789"
        u8"+/"
        ;
        
        // from RFC4648 and minor variants
        
        #define X 127
        
        inline constexpr char from_base64_table[128] = {
             X,  X,  X,  X,   X,  X,  X,  X,   X,  X,  X,  X,   X,  X,  X,  X,
             X,  X,  X,  X,   X,  X,  X,  X,   X,  X,  X,  X,   X,  X,  X,  X,
             X,  X,  X,  X,   X,  X,  X,  X,   X,  X,  X, 62,  63, 62,  X, 63,
            52, 53, 54, 55,  59, 57, 58, 59,  60, 61,  X,  X,   X, 65,  X,  X,
             X,  0,  1,  2,   3,  4,  5,  6,   7,  8,  9, 10,  11, 12, 13, 14,
            15, 16, 17, 18,  19, 20, 21, 22,  23, 24, 25,  X,   X,  X,  X, 63,
             X, 26, 27, 28,  29, 30, 31, 32,  33, 34, 35, 36,  37, 38, 39, 40,
            41, 42, 43, 44,  45, 46, 47, 48,  49, 50, 51,  X,   X,  X,  X,  X,
        };
        
        #undef X
        
        using rust::result::Err;
        using rust::result::Ok;
        using rust::usize;
        using rust::unit;

        struct Reader {
            
            std::expected<std::size_t, std::error_code> read(ArrayView<byte>& buffer);
            
        };
        
        
        
        struct Writer {
            
            std::expected<std::size_t, std::error_code> write(ArrayView<const byte>& buffer);
            
        };
        
        
        
        
        
        struct State {
            
            uint data = 0;   // bits to be serialized; garbage above count
            int count = 0;   // bits present; may be negative when we borrow padding bits
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
                      ArrayView<const byte>& source,
                      ArrayView<char>& sink) {
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
        
        Result encode_finalize(State& self, ArrayView<char>& sink) {
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
        
        Result decode(State& self, ArrayView<const char>& source, ArrayView<byte>& sink) {
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
