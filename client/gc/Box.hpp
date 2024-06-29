//
//  Box.hpp
//  client
//
//  Created by Antony Searle on 29/6/2024.
//

#ifndef Box_hpp
#define Box_hpp

#include "object.hpp"

namespace wry::gc {

    // Structs such as Arrays can usefully be embedded directly into a larger
    // object, or have an independent lifetime as first class objects on the
    // heap
        
    // Box wraps the former into the latter
    

    // object_shade(T) needs to call object_shade on all subobjects
    //      recusively until pointer or empty
    // object_shade(Box<T>*) just shades the Box's own color

    // object_trace(T) needs to call object_trace on all sub-objects
    // object_scan(Box<T>*) needs to call object_trace on all subobjects
    
    
    // Thus:
    //  - object_scan(Object*)
    //  - object_scan(T) = delete
    //  - object_trace(Object*) = stack for later
    //  - object_trace(T) = recursively call object_trace on subobjects
    //       until pointer, or nothing
    
    
    template<typename T>
    struct Box : Object {
        
        T payload;
        
        template<typename... Args>
        explicit Box(Args&&... args)
        : payload(std::forward<Args>(args)...) {
        }
        
        virtual void _object_scan() const;
        
    };
    
    
    template<typename T>
    void Box<T>::_object_scan() const {
        object_trace(payload);
    }
    
    
    
        
    
    
} // namespace wry::gc

#endif /* Box_hpp */
