//
//  serialize.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef serialize_hpp
#define serialize_hpp

#include <variant>

#include "contiguous_deque.hpp"
#include "stdint.hpp"
#include "type_traits.hpp"
#include "stdfloat.hpp"
#include "string.hpp"

namespace wry {
        
    // Serialize primitives
    
#define X(T) template<typename S>\
    void serialize(const T& x, S&& serializer) {\
        return std::forward<S>(serializer).serialize_##T(x);\
    }
    
    WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
    WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES
    X(String)
    X(StringView)
#undef X

    // Serialize sequences
    
    template<typename T, typename S> 
    void serialize(const ContiguousView<T>& x, S&& serializer) {
        auto seq = std::forward<S>(serializer).serialize_seq(Some(x.size()));
        for (const auto& e : x)
            seq.serialize_element(e);
        seq.end();
    }
     
} // namespace wry

#endif /* serialize_hpp */
