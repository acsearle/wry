//
//  object.cpp
//  client
//
//  Created by Antony Searle on 16/6/2024.
//

#include <cstdlib>
#include <cstdio>

#include "object.hpp"

namespace wry::gc {
    
    // TODO: are these methods better off being abstract?
        
    void Object::_object_debug() const {
        abort();
    }
    
    size_t Object::_object_hash() const { 
        abort();
    }
    
    std::strong_ordering Object::operator<=>(const Object&) const {
        abort();
    }
    
    bool Object::operator==(const Object&) const {
        abort();
    }
    
    Object::Object(const Object&)
    : Object() {
    }
    
    Object::Object(Object&&)
    : Object() {
    }

        
} // namespace wry::gc
