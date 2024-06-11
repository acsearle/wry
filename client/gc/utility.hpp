//
//  utility.hpp
//  client
//
//  Created by Antony Searle on 30/5/2024.
//

#ifndef utility_hpp
#define utility_hpp

#include <cassert>
#include <cstdint>

#include "atomic.hpp"

namespace gc {
            
    template<typename T>
    struct TaggedPtr {
        
        enum : std::intptr_t {
            TAG_MASK = 7,
            PTR_MASK = -8,
        };
        
        struct TagImposter {
            std::intptr_t _value;
            operator std::intptr_t() const {
                return _value & TAG_MASK;
            }
            TagImposter& operator=(std::intptr_t t) {
                assert(!(t & PTR_MASK));
                _value = (_value & PTR_MASK) | t;
                return *this;
            }
        };
        
        struct PtrImposter {
            std::intptr_t _value;
            operator T*() const {
                return reinterpret_cast<T*>(_value & PTR_MASK);
            }
            PtrImposter& operator=(T* p) {
                std::intptr_t q = reinterpret_cast<std::intptr_t>(p);
                assert(!(q & TAG_MASK));
                _value = (_value & TAG_MASK) | q;
            }
        };
        
        union {
            std::intptr_t _value;
            TagImposter tag;
            PtrImposter ptr;
        };
        
        TaggedPtr() = default;
        
        explicit TaggedPtr(T* p) {
            std::intptr_t q = reinterpret_cast<std::intptr_t>(p);
            assert(!(q & TAG_MASK));
            _value = q;
        }
        
        explicit TaggedPtr(std::intptr_t pt)
        : _value(pt) {
        }
        
        TaggedPtr(const TaggedPtr&) = default;
        
        TaggedPtr(T* p, std::intptr_t t) {
            std::intptr_t q = reinterpret_cast<std::intptr_t>(p);
            assert(!(q & TAG_MASK) && !(t & PTR_MASK));
            _value = q | t;
        }
        
        T* operator->() const {
            return ptr.operator T*();
        }
        
        T& operator*() const {
            return ptr.operator T*();
        }
        
    }; // TaggedPtr<T>
    
} // namespace gc

#endif /* utility_hpp */
