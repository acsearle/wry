//
//  IndirectFixedCapacityArray.hpp
//  client
//
//  Created by Antony Searle on 22/6/2024.
//

#ifndef wry_gc_IndirectFixedCapacityValueArray_hpp
#define wry_gc_IndirectFixedCapacityValueArray_hpp

#include "value.hpp"

namespace wry::gc {
    
    struct IndirectFixedCapacityValueArray : Object {
        
        std::size_t _capacity;
        Traced<Value>* _storage;
        explicit IndirectFixedCapacityValueArray(std::size_t count);
        ~IndirectFixedCapacityValueArray();
    }; // struct IndirectFixedCapacityValueArray
    
} // namespace wry::gc

#endif /* wry_gc_IndirectFixedCapacityValueArray_hpp */
