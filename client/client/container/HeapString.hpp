//
//  wry/gc/HeapString.hpp
//  client
//
//  Created by Antony Searle on 22/6/2024.
//

#ifndef wry_gc_HeapString_hpp
#define wry_gc_HeapString_hpp

#include <string_view>

#include "garbage_collected.hpp"
#include "ctrie.hpp"

namespace wry {
    
    // HeapString is an interned-string value.  It is stored in the trie via
    // an _ctrie::SNode wrapper -- HeapString itself is not a Branch.  See
    // [core/docs/ctrie.md].
    struct HeapString final : HeapValue {

        // Local placement-new: GarbageCollected's `operator new(size_t)`
        // would otherwise hide the global placement form via class-scope
        // name lookup, breaking `new(raw) HeapString` inside `make()`.
        static void* operator new(std::size_t, void* ptr) noexcept { return ptr; }

        static const HeapString* make(std::size_t hash, std::string_view view);
        static const HeapString* make(std::string_view view);

        size_t _hash;
        size_t _size;
        char _bytes[] __counted_by(_size);

        std::string_view as_string_view() const;

        HeapString();
        virtual ~HeapString() override final;

        // _garbage_collected_shade inherits the GarbageCollected default
        // (fetch_or on _gray with the thread-local gray bits); HeapString
        // has no GC-managed children, so scan is a no-op.
        virtual void _garbage_collected_scan() const override final;
        virtual void _garbage_collected_debug() const override final;

    }; // struct HeapString
    
    
    template<size_t N> requires (N > 0)
    constexpr Value::Value(const char (&ntbs)[N]) {
        const size_t M = N - 1;
        assert(ntbs[M] == '\0');
        if (M < 8) {
            _short_string_t s;
            s._tag_and_len = (M << VALUE_SHIFT) | VALUE_TAG_SHORT_STRING;
            // builtin for constexpr
            __builtin_memcpy(s._chars, ntbs, M);
            __builtin_memcpy(&_data, &s, 8);
        } else {
            _data = (uint64_t)HeapString::make(ntbs);
        }
    }
    
} // namespace wry

#endif /* wry_gc_HeapString_hpp */
