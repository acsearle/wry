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
//
//    HeapString const* HeapString::make(std::size_t hc, std::string_view view) {
//        void* a = malloc(sizeof(HeapString) + view.size());
//        HeapString* b = new(a) HeapString;
//        b->_hash = hc;
//        b->_size = view.size();
//        std::memcpy(b->_bytes, view.data(), view.size());
//        return b;
//    }
//
//
//    HeapString const* HeapString::make(std::string_view view) {
//        return make(hash_combine(view.data(), view.size()), view);
//    }

} // namespace wry
