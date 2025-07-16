//
//  HeapString.cpp
//  client
//
//  Created by Antony Searle on 22/6/2024.
//

#include "HeapString.hpp"

namespace wry {
    
    HeapString::~HeapString() {
        printf("~\"%.*s\"\n", (int)_size, (const char*)_bytes);
    }

    void HeapString::_garbage_collected_shade() const {
        abort();
#if 0
        _garbage_collected_trace(p);
#endif
    }
    
    void HeapString::_garbage_collected_trace(void*p) const {
        abort();
#if 0
        Color expected = Color::WHITE;
        (void) color.compare_exchange(expected, Color::BLACK);
        switch (expected) {
            case Color::WHITE:
            case Color::BLACK:
                break;
            case Color::GRAY:
            case Color::RED:
            default:
                debug(this);
                abort();
        }
#endif
    }
    
    void HeapString::_garbage_collected_scan(void*) const {
        fprintf(stderr, "Scanned a weak object ");
        _garbage_collected_debug();
        abort();
    }

    void HeapString::_garbage_collected_trace_weak(void*) const {
        // no-op
    }
    
    void HeapString::_garbage_collected_debug() const {
        printf("\"%.*s\"", (int)_size, (const char*)_bytes);
    }
    
    
    
} // namespace wry
