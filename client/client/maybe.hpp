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
    
    template<typename T>
    union Maybe {
        
        Maybe() = delete;
        Maybe(const Maybe&) = delete;
        Maybe(Maybe&&) = delete;
        ~Maybe() = delete;
        Maybe& operator=(const Maybe&) = delete;
        Maybe& operator=(Maybe&&) = delete;

        template<typename... Args>
        T& emplace(Args&&... args) {
            std::construct_at(&value, std::forward<Args>(args)...);
        }
        
        void destroy() {
            std::destroy_at(&value);
        }
        
        T value;
        
    }; // union Maybe
        
}; // namespace wry

#endif /* maybe_hpp */
