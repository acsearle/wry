//
//  tagged_ptr.hpp
//  client
//
//  Created by Antony Searle on 30/5/2024.
//

#ifndef tagged_ptr_hpp
#define tagged_ptr_hpp

#include <cassert>
#include <cstdint>
#include <utility>

namespace wry {
            
    template<typename T, typename E = intptr_t>
    struct TaggedPtr {
        
        enum : intptr_t {
            TAG_MASK = 15,
            PTR_MASK = ~TAG_MASK,
        };
        
        struct TagImposter {
            intptr_t _value;
            operator E() const {
                return E{_value & TAG_MASK};
            }
            TagImposter& operator=(intptr_t t) {
                assert(!(t & PTR_MASK));
                _value = (_value & PTR_MASK) | t;
                return *this;
            }
        };
        
        struct PtrImposter {
            intptr_t _value;
            operator T*() const {
                return reinterpret_cast<T*>(_value & PTR_MASK);
            }
            PtrImposter& operator=(T* p) {
                intptr_t q = reinterpret_cast<intptr_t>(p);
                assert(!(q & TAG_MASK));
                _value = (_value & TAG_MASK) | q;
            }
        };
        
        union {
            intptr_t _value;
            TagImposter tag;
            PtrImposter ptr;
        };
        
        TaggedPtr() = default;
        
        explicit TaggedPtr(T* p) {
            intptr_t q = reinterpret_cast<intptr_t>(p);
            assert(!(q & TAG_MASK));
            _value = q;
        }
        
        explicit TaggedPtr(intptr_t pt)
        : _value(pt) {
        }
        
        TaggedPtr(const TaggedPtr&) = default;
        
        TaggedPtr(T* p, E t) {
            intptr_t q = (intptr_t)p;
            intptr_t s = (intptr_t)t;
            assert(!(q & TAG_MASK) && !(s & PTR_MASK));
            _value = q | s;
        }
        
        T* operator->() const {
            return ptr.operator T*();
        }
        
        T& operator*() const {
            return ptr.operator T*();
        }
        
        std::pair<T, E> destructure() const;
        
    }; // struct TaggedPtr<T, E>
    
} // namespace wry

#endif /* tagged_ptr_hpp */
