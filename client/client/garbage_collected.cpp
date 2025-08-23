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




#if 0
// Tricolor abstraction color
enum class Color {
    WHITE = 0,
    BLACK = 1,
    GRAY  = 2,
    RED   = 3,
};

struct AtomicEncodedColor {
    
    Atomic<std::underlying_type_t<Color>> _encoded;
    
    AtomicEncodedColor();
    Color load() const;
    bool compare_exchange(Color& expected, Color desired);
    
};

/*
 // implementation of
 
 template<PointerConvertibleTo<GarbageCollected> T>
 hash_t hash(T*const self ) {
 return self->_garbage_collected_hash();
 }
 
 template<PointerConvertibleTo<GarbageCollected> T>
 void debug(const T* self) {
 if (self) {
 self->_garbage_collected_debug();
 } else {
 printf("(const GarbageCollected*)nullptr\n");
 }
 }
 
 template<PointerConvertibleTo<GarbageCollected> T>
 void passivate(T* self) {
 self = nullptr;
 }
 
 template<PointerConvertibleTo<GarbageCollected> T>
 void shade(T*const self) {
 if (self)
 self->_garbage_collected_shade();
 }
 
 template<PointerConvertibleTo<GarbageCollected> T>
 void trace(T*const self) {
 if (self)
 self->_garbage_collected_trace();
 }
 
 template<PointerConvertibleTo<GarbageCollected> T>
 void trace_weak(T*const self) {
 if (self)
 self->_garbage_collected_trace_weak();
 }
 */


#if 0
// inline Color GarbageCollected::_garbage_collected_sweep() const {
// return color.load();
// }
#endif

#endif
