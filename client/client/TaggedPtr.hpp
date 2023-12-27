//
//  TaggedPtr.hpp
//  client
//
//  Created by Antony Searle on 17/12/2023.
//

#ifndef TaggedPtr_hpp
#define TaggedPtr_hpp

#include "assert.hpp"
#include "stddef.hpp"
#include "stdint.hpp"

namespace wry {
    
    
    
    template<typename T>
    struct TaggedPtr {
        
        
        
        struct Ptr {
            
            enum : std::uintptr_t {
                MASK = 1 - alignof(T),
            };
            
            std::uintptr_t data;
            
            T* operator++(int) {
                T* result = (T*) (data & MASK);
                data += sizeof(T);
                return result;
            }
            
            T* operator--(int) {
                T* result = (T*) (data & MASK);
                data -= sizeof(T);
                return result;
            }
            
            template<typename U>
            operator U*() const {
                return (T*) (data & MASK);
            }
            
            operator bool() const {
                return (bool) (data & MASK);
            }
            
            T& operator[](std::ptrdiff_t n) const {
                T* result = (T*) (data & MASK);
                assert(result);
                return result[n];
            }
            
            T& operator->() const {
                T* result = (T*) (data & MASK);
                assert(result);
                return result;
            }
            
            Ptr& operator++() {
                ++((T*&) data);
                return *this;
            }
            
            Ptr& operator--() {
                --((T*&) data);
                return *this;
            }
            
            bool operator!() const {
                return !(data & MASK);
            }
            
            T& operator*() const {
                return *(data & MASK);
            }
            
            Ptr& operator=(T* p) {
                std::uintptr_t value = (std::uintptr_t) p;
                assert(!(value & ~MASK));
                data = (data & ~MASK) | (value & MASK);
                return *this;
            }
            
        }; // struct Ptr
        
        
        
        struct Tag {
            
            std::uintptr_t data;
            
            enum : std::uintptr_t {
                MASK = alignof(T) - 1,
            };
            
            template<typename U>
            operator U() const {
                return (data & MASK);
            }
            
            bool operator!() const {
                return !(data & MASK);
            }
            
            bool operator==(std::uintptr_t value) {
                return (data & MASK) == value;
            }
            
            Tag& operator=(std::uintptr_t value) {
                assert(!(value & ~MASK));
                data = (data & ~MASK) | (value & MASK);
                return *this;
            }
            
        }; // struct Tag
               
        
        
        union {
            
            std::uintptr_t data;
            Ptr ptr;
            Tag tag;
            
        }; // union
        
        
        
    }; // struct TaggedPtr
    
    
} // namespace wry


#endif /* TaggedPtr_hpp */
