//
//  deserialize.hpp
//  client
//
//  Created by Antony Searle on 30/9/2023.
//

#ifndef deserialize_hpp
#define deserialize_hpp

#include "stdint.hpp"
#include "stdfloat.hpp"
#include "utility.hpp"
#include "array.hpp"
#include "string.hpp"

#include "Option.hpp"

namespace wry {
    
    using namespace rust::option;
    
    // serde-rs deserialization
    
    template<typename T, typename D>
    T deserialize(D&& deserializer) {
        // tag dispatch to actual implementations
        return deserialize(std::in_place_type<T>, std::forward<D>(deserializer));
    }
    
    
    
    // define visitors
            
    template<typename T>
    struct basic_visitor {
        
        using Value = T;
        
        // default implementations
        
#define X(T)\
        T visit_##T(T x) { throw ENOTSUP; }

        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES

        X(String)
        
#undef X

    };
    
    
    // define deserialization

#define X(T)\
    struct visitor_##T : basic_visitor<T> {\
        T visit_##T(T x) { return x; }\
    };\
    \
    template<typename D>\
    T deserialize(std::in_place_type_t<T>, D&& deserializer) {\
        return std::forward<D>(deserializer).deserialize_##T(visitor_##T{});\
    }
    
    WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
    WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES
    
#undef X
    
    struct visitor_bool  {
        
#define X(T)\
        bool visit_##T (T value) const {\
            return static_cast<bool>(value);\
        }
        
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_INTEGER_TYPES
        WRY_X_OF_T_FOR_T_IN_FIXED_WIDTH_FLOAT_TYPES
        
#undef X
        
    };

    template<typename D>
    bool deserialize(std::in_place_type_t<bool>, D&& deserializer) {
        return std::forward<D>(deserializer).deserialize_string(visitor_bool{});
    }
    
    struct visitor_string : basic_visitor<String> {
        using Value = String;
        Value visit_string(String s) const {
            return s;
        }
        
    };
    
    template<typename D>
    String deserialize(std::in_place_type_t<String>, D&& deserializer) {
        return std::forward<D>(deserializer).deserialize_string(visitor_string{});
    }
        
    
    

    
    template<typename T>
    struct _deserialize_array_visitor {
        
        using Value = ContiguousDeque<T>;
        
        template<typename A>
        ContiguousDeque<T> visit_seq(A&& access) {
            ContiguousDeque<T> x;
            for (;;) {
                Option<T> y = access.template next_element<T>();
                if (y.is_some())
                    x.emplace_back(std::move(y).unwrap());
                else
                    return std::move(x);
            }
        }
    };
    
    template<typename T, typename D>
    ContiguousDeque<T> deserialize(std::in_place_type_t<ContiguousDeque<T>>, D&& deserializer) {
        return std::forward<D>(deserializer).deserialize_seq(_deserialize_array_visitor<T>{});
    }
    
    
    
    
#define WRY_X_FOR_ALL_RUST_INTEGERS\
X(f32) X(f64)\
X(i8) X(i16) X(i32) X(i64) X(i128)\
X(u8) X(u16) X(u32) X(u64) X(u128)
    

    
}


#endif /* deserialize_hpp */
