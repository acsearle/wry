//
//  IndirectFixedCapacityValueArray.cpp
//  client
//
//  Created by Antony Searle on 22/6/2024.
//

#include "IndirectFixedCapacityValueArray.hpp"

namespace wry::gc {
    
    void IndirectFixedCapacityValueArray::_object_scan() const {
        auto first = _storage;
        auto last = first + _capacity;
        for (; first != last; ++first)
            value_trace(*first);
    }
    
} // namespace wry::gc
