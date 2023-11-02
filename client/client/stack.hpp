//
//  stack.hpp
//  client
//
//  Created by Antony Searle on 29/10/2023.
//

#ifndef stack_hpp
#define stack_hpp

#include "stddef.hpp"
#include "utility.hpp"

namespace wry {
    
    template<typename> struct Stack;
    
    template<typename T>
    struct rank<Stack<T>>
    : std::integral_constant<std::size_t, rank<T>::value + 1> {
    };
    
    // Stack hews closer to C++ vector and Rust Vec
    //
    // vs Array,
    // + 25% smaller handle
    // + 50%(?) smaller overallocation
    // - O(1) front operations
    
    template<typename T>
    struct Stack {
        
        using size_type = size_type;
        using difference_type = difference_type;
        using value_type = T;
        using iterator = T*;
        using const_iterator = const T*;
        using reference = T&;
        using const_reference = const T&;
        using byte_type = byte;
        
        T* _begin;
        T* _end;
        T* _allocation_end;

        bool invariant() const {
            return ((_begin <= _end)
                    && (_end <= _allocation_end));
        }
        
        size_type size() const {
            return _end - _begin;
        }
        
        size_type capacity() const {
            return _allocation_end - _begin;
        }

        size_type size_in_bytes() const {
            return size() * sizeof(T);
        }
        
        size_type capacity_in_bytes() const {
            return capacity() * sizeof(T);
        }

        Stack()
        : _begin(nullptr)
        , _end(nullptr)
        , _allocation_end(nullptr) {
        }
        
        Stack(const Stack& other) 
        : _begin(static_cast<T*>(operator new(other.size_in_bytes())))
        , _end(_begin)
        , _allocation_end(_begin + other.size()) {
            _end = std::uninitialized_copy(other._begin, other._end, _begin);
        }
        
        Stack(Stack&& other)
        : _begin(exchange(other._begin, nullptr))
        , _end(exchange(other._end, nullptr))
        , _allocation_end(exchange(other._allocation_end, nullptr)) {
        }
        
        ~Stack() {
            std::destroy(_begin, _end);
            operator delete(_begin);
        }
        
        Stack& operator=(Stack&& other) {
            std::destroy(_begin, _end);
            operator delete(_begin);
            _begin = exchange(other._begin, nullptr);
            _end = exchange(other._end, nullptr);
            _allocation_end = exchange(other._allocation_end, nullptr);
            return *this;
        }
        
        Stack& operator=(const Stack& other) {
            if (other.size() <= size()) {
                T* middle = std::copy(other._begin, other._end, _begin);
                std::destroy(middle, _end);
                _end = middle;
            } else if (other.size() <= capacity()) {
                const T* middle = other._begin + size();
                std::copy(other._begin, middle, _begin);
                _end = std::uninitialized_copy(middle, other._end, _end);
            } else {
                std::destroy(_begin, _end);
                operator delete(_begin);
                _begin = static_cast<T*>(operator new(other.size_in_bytes()));
                _end = _begin;
                _allocation_end = _begin + other.size();
                _end = std::uninitialized_copy(other._begin, other._end, _end);
            }
        }

        
    };
    
    

    
}

#endif /* stack_hpp */
