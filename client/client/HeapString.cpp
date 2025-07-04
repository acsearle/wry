//
//  HeapString.cpp
//  client
//
//  Created by Antony Searle on 22/6/2024.
//

#include "HeapString.hpp"

namespace wry::gc {
    
    HeapString::~HeapString() {
        printf("~\"%.*s\"\n", (int)_size, (const char*)_bytes);
    }

    void HeapString::_garbage_collected_shade() const {
        _garbage_collected_trace();
    }
    
    void HeapString::_garbage_collected_trace() const {
        Color expected = Color::WHITE;
        (void) color.compare_exchange(expected, Color::BLACK);
        switch (expected) {
            case Color::WHITE:
            case Color::BLACK:
                break;
            case Color::GRAY:
            case Color::RED:
            default:
                adl::debug(this);
                abort();
        }
    }
    
    void HeapString::_garbage_collected_scan() const {
        fprintf(stderr, "Scanned a weak object ");
        _garbage_collected_debug();
        abort();
    }

    void HeapString::_garbage_collected_trace_weak() const {
        // no-op
    }
    
    void HeapString::_garbage_collected_debug() const {
        printf("\"%.*s\"", (int)_size, (const char*)_bytes);
    }
    
    
    
} // namespace wry::gc
