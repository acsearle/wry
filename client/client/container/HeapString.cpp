//
//  HeapString.cpp
//  client
//
//  Created by Antony Searle on 22/6/2024.
//

#include "HeapString.hpp"

namespace wry {
    
    HeapString::~HeapString() {
        // intentionally silent
    }

    void HeapString::_garbage_collected_scan() const {
        // No GC-managed children.  The interesting reachability question
        // for HeapString is handled in WEAK_DECISION (Phase 2+); see
        // core/docs/ctrie.md.
    }

    void HeapString::_garbage_collected_debug() const {
        printf("\"%.*s\"", (int)_size, (const char*)_bytes);
    }
    
    
    
} // namespace wry
