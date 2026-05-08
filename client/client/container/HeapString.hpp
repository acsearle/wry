//
//  wry/gc/HeapString.hpp
//  client
//
//  Created by Antony Searle on 22/6/2024.
//

#ifndef wry_gc_HeapString_hpp
#define wry_gc_HeapString_hpp

#include <string>
#include <string_view>

#include "value.hpp"
#include "ctrie.hpp"
#include "garbage_collected.hpp"

namespace wry {
    
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

    inline size_t hash(HeapString const* a) {
        return a->_hash;
    }

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


    struct StringKey {
        std::string data;
        bool operator==(StringKey const&) const = default;
    };

    inline size_t hash(StringKey const& x) {
        return hash_combine(x.data.data(), x.data.size());
    }

    template<typename T>
    struct WeakHolder : GarbageCollected {

        void _garbage_collected_debug() const override {
            printf("WeakHolder\n");
        }

        enum State { READY, WAS_LOADED, GONE };

        Atomic<State> _state;
        T const* _weak;

        T const* mutator_try_upgrade() const {
            State expected = _state.load_relaxed();
            for (;;) {
                switch (expected) {
                    case READY:
                        if (_state.compare_exchange_weak_relaxed_relaxed(expected, WAS_LOADED))
                            return _weak;
                        break;
                    case WAS_LOADED:
                        return _weak;
                    case GONE:
                        return nullptr;
                    default:
                        std::unreachable();
                }
            }
        }

        // Return value indicates if shading is requested
        virtual bool _garbage_collected_decide_weak(uint16_t next_delete_mask) const override {
            State expected = _state.load_relaxed();
            for (;;) {
                switch (expected) {
                    case READY:
                        if (_weak->_black & next_delete_mask)
                            return false;
                        if (_state.compare_exchange_weak_relaxed_relaxed(expected, GONE))
                            return false;
                        break;
                    case WAS_LOADED:
                        if (_state.compare_exchange_weak_relaxed_relaxed(expected, READY))
                            return true;
                        break;
                    case GONE:
                        return false;
                    default:
                        std::unreachable();
                }
            }
        }

    };

    // TODO: Can't static init GarbageCollected objects because unpinned
    // inline Root<Ctrie<StringKey, WeakHolder<HeapString>> const*> _heap_string_ctrie{new Ctrie<StringKey, WeakHolder<HeapString>>};

    HeapString const* _heap_string_ctrie_mutator_find_upgrade_or_emplace(std::string_view);
    void _heap_string_ctrie_collector_try_erase(WeakHolder<HeapString> const*);

} // namespace wry

#endif /* wry_gc_HeapString_hpp */
