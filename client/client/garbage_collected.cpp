//
//  object.cpp
//  client
//
//  Created by Antony Searle on 16/6/2024.
//

#include <cstdlib>
#include <cstdio>

#include "garbage_collected.hpp"

namespace wry {
    
    // TODO: are these methods better off being abstract?
        
    void GarbageCollected::_garbage_collected_debug() const {
        abort();
    }
    
    size_t GarbageCollected::_garbage_collected_hash() const { 
        abort();
    }
    
    std::strong_ordering GarbageCollected::operator<=>(const GarbageCollected&) const {
        abort();
    }
    
    bool GarbageCollected::operator==(const GarbageCollected&) const {
        abort();
    }
    
    GarbageCollected::GarbageCollected(const GarbageCollected&)
    : GarbageCollected() {
    }
    
    GarbageCollected::GarbageCollected(GarbageCollected&&)
    : GarbageCollected() {
    }

        
} // namespace wry
