//
//  HeapString.cpp
//  client
//
//  Created by Antony Searle on 22/6/2024.
//

#include "HeapString.hpp"

#include "test.hpp"

namespace wry {
    
    HeapString::~HeapString() {
        printf("%p:%s\n", this, __PRETTY_FUNCTION__);
    }

    const HeapString* HeapString::make(std::string_view view) {
        return make(std::hash<std::string_view>()(view), view);
    }

    std::string_view HeapString::as_string_view() const {
        return std::string_view(_bytes, _size);
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



    const HeapString* HeapString::make(size_t hash, std::string_view view) {
        // TODO(ctrie.md Phase 3): intern via the global string ctrie.  For
        // now (Phase 0) we allocate a fresh HeapString every call; the trie
        // machinery exists but is not yet on the production allocation path.
        size_t n = view.size();
        size_t bytes = sizeof(HeapString) + n;
        void* raw = GarbageCollected::operator new(bytes);
        std::memset(raw, 0, bytes);
        HeapString* a = new(raw) HeapString;
        a->_hash = hash;
        a->_size = n;
        std::memcpy(a->_bytes, view.data(), n);
        printf("%p:%s\n", a, __PRETTY_FUNCTION__);
        return a;
    }




    define_test("[string interning]") {

        HeapString const* a{_heap_string_ctrie_mutator_find_upgrade_or_emplace("one")};
        HeapString const* b{_heap_string_ctrie_mutator_find_upgrade_or_emplace("one")};

        assert(a == b);

        co_await Coroutine::WaitForCollectionCycles{3};
        // SAFETY: a and b are now dangling

        HeapString const* c{_heap_string_ctrie_mutator_find_upgrade_or_emplace("one")};
        HeapString const* d{_heap_string_ctrie_mutator_find_upgrade_or_emplace("one")};

        assert(c == d);

        // TODO: This can spuriously fire if the allocation is reused
        assert(a != c);


        co_return;
    };


} // namespace wry
