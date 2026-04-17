//
//  indirect.hpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#ifndef indirect_hpp
#define indirect_hpp

#include <memory>

namespace wry {
    
    // A type that holds a T but provides access to it via a subset of the
    // pointer interface.
    
    template<typename T>
    class indirect {
        
        T _value;
        
    public:
        
        constexpr explicit indirect(T&& x)
        : _value(std::move(x)) {
        }
        
        constexpr indirect() = delete;
        
        // (other special member functions defaulted)
        
        constexpr const T* _Nonnull operator->() const {
            return std::addressof(_value);
        }
        
        constexpr const T& operator*() const {
            return _value;
        }
        
        constexpr explicit operator bool() const {
            return true;
        }
        
        constexpr bool operator!() const {
            return false;
        }
        
    };
    
} // namespace wry

#endif /* indirect_h */
