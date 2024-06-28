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

    void HeapString::_object_shade() const {
        Color expected = Color::WHITE;
        (void) color.compare_exchange(expected, Color::BLACK);
    }
    
    void HeapString::_object_trace() const {
        Color expected = Color::WHITE;
        (void) color.compare_exchange(expected, Color::BLACK);
        switch (expected) {
            case Color::WHITE:
            case Color::BLACK:
                break;
            case Color::GRAY:
            case Color::RED:
            default:
                object_debug(this);
                abort();
        }
    }

    void HeapString::_object_trace_weak() const {
        // no-op
    }
    
} // namespace wry::gc
