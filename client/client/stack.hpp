//
//  stack.hpp
//  client
//
//  Created by Antony Searle on 29/10/2023.
//

#ifndef stack_hpp
#define stack_hpp

#include <stack>
#include <vector>

#include "stddef.hpp"
#include "utility.hpp"


namespace wry {
    
    template<typename T>
    struct StandardVectorStack {
        
        std::vector<T> c;
        
        void push(T value) {
            c.push_back(value);
        }
                
        bool try_pop(T& victim) {
            bool result = !c.empty();
            if (result) {
                victim = std::move(c.back());
                c.pop_back();
            }
            return result;
        }
        
        bool debug_is_empty() const {
            return c.empty();
        }
        
    };
    
    template<typename T>
    using Stack = StandardVectorStack<T>;
    
    template<typename T>
    struct Rank<Stack<T>>
    : std::integral_constant<std::size_t, Rank<T>::value + 1> {
    };

    
} // namespace wry

#endif /* stack_hpp */
