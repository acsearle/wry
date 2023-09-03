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
    
    // A type that holds a T and provides access to it via the pointer
    // interface.  Useful for implementing operator->() on iterators that
    // return a value.
    
    template<typename T>
    class indirect {
        T _value;
    public:
        indirect() = delete;
        indirect(const indirect&) = delete;
        indirect(indirect&&) = default;
        explicit indirect(T&& x) : _value(std::move(x)) {}
        ~indirect() = default;
        indirect& operator=(const indirect&) = delete;
        indirect& operator=(indirect&&) = delete;
        const T* operator->() const { return std::addressof(_value); }
        const T& operator*() const { return _value; }
        explicit operator bool() const { return true; }
        bool operator!() const { return false; }
    };
    
} // namespace wry

#endif /* indirect_h */
