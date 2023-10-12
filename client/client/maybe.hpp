//
//  maybe.hpp
//  client
//
//  Created by Antony Searle on 10/10/2023.
//

#ifndef maybe_hpp
#define maybe_hpp

#include <memory>
#include <utility>

namespace wry {
    
    // externally discriminated optional
    
    template<typename T>
    union Maybe {
        
        Maybe() {
        }
        
        template<typename... Args>
        Maybe(std::in_place_t, Args&&... args)
        : T(std::forward<Args>(args)...) {
        }
        
        Maybe(const Maybe&) = delete;
        Maybe(Maybe&&) = delete;
        
        Maybe& operator=(const Maybe&);
        Maybe& operator=(Maybe&&);

        template<typename... Args>
        T& emplace(Args&&... args) {
            std::construct_at(&value, std::forward<Args>(args)...);
        }
        
        void destroy() {
            std::destroy_at(&value);
        }
        
        T value;
        
    };
    
}; // namespace wry

#endif /* maybe_hpp */
