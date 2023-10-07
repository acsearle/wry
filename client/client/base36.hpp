//
//  base36.hpp
//  client
//
//  Created by Antony Searle on 4/10/2023.
//

#ifndef base36_hpp
#define base36_hpp

namespace wry {
    
    // alnum digits up to 36, notably including binary, octal, decimal, hex
    // as subsets; invalid characters are marked as 64; for a given base,
    // invalid characters are marked as
    
    namespace base36 {
        
        inline constexpr char to_base36lower_table[37] =
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        ;
        
        inline constexpr char to_base36upper_table[37] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        ;
        
        #define X 127
        inline constexpr char from_base36_table[128] = {
             X,  X,  X,  X,   X,  X,  X,  X,   X,  X,  X,  X,   X,  X,  X,  X,
             X,  X,  X,  X,   X,  X,  X,  X,   X,  X,  X,  X,   X,  X,  X,  X,
             X,  X,  X,  X,   X,  X,  X,  X,   X,  X,  X,  X,   X,  X,  X,  X,
             0,  1,  2,  3,   4,  5,  6,  7,   8,  9,  X,  X,   X,  X,  X,  X, // '0'-'9'
             X, 10, 11, 12,  13, 14, 15, 16,  17, 18, 19, 20,  21, 22, 23, 24, // 'A'-'O'
            25, 26, 27, 28,  29, 30, 31, 32,  33, 34, 35,  X,   X,  X,  X,  X, // 'P'-'Z'
             X, 10, 11, 12,  13, 14, 15, 16,  17, 18, 19, 20,  21, 22, 23, 24, // 'a'-'o'
            25, 26, 27, 28,  29, 30, 31, 32,  33, 34, 35,  X,   X,  X,  X,  X, // 'p'-'z'
        };
        #undef X
        
    } // namespace base36
    
} // namespace wry
#endif /* base36_hpp */
