//
//  word.hpp
//  client
//
//  Created by Antony Searle on 9/3/2024.
//

#ifndef word_hpp
#define word_hpp

#include <bit>
#include <cstdint>

namespace wry {
    
    // Use bit cast as an alternative to a type punning in a union

    struct Word {
        
        intptr_t representation;
        
        template<typename T>
        constexpr explicit operator T() const {
            return std::bit_cast<T>(representation);
        }
        
        template<typename T>
        constexpr Word& operator=(const T& value) {
            representation = std::bit_cast<intptr_t>(value);
            return *this;
        }
        
        template<typename T>
        constexpr bool operator==(const T& other) const {
            return representation == std::bit_cast<intptr_t>(other);
        }
        
    }; // struct Word
    
    static_assert(alignof(Word) == 8);
    
};

#endif /* word_hpp */
