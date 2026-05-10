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

    template<typename> struct WeakHolder;
    void _heap_string_ctrie_collector_try_erase(WeakHolder<HeapString> const* victim);

    template<typename T>
    struct WeakHolder : GarbageCollected {

        void _garbage_collected_debug() const override {
            printf("WeakHolder\n");
        }

        enum State { READY, WAS_LOADED, GONE };

        mutable Atomic<State> _state;
        T const* _weak;

        explicit WeakHolder(T const* weak) : _state{READY}, _weak{weak} {}

        T const* mutator_try_upgrade() const {
            State expected = _state.load_relaxed();
            for (;;) {
                switch (expected) {
                    case READY:
                        if (_state.compare_exchange_weak_relaxed_relaxed(expected, WAS_LOADED)) {
                            printf("%p:HeapString was READY for mutator\n", _weak);
                            return _weak;
                        }
                        break;
                    case WAS_LOADED:
                        printf("%p:HeapString WAS_LOADED for mutator\n", _weak);
                        return _weak;
                    case GONE:
                        printf("%p:HeapString was GONE for mutator\n", _weak);
                        return nullptr;
                    default:
                        std::unreachable();
                }
            }
        }

        // Return value indicates if shading is requested
        virtual void _garbage_collected_decide_weak(uint16_t next_delete_mask,
                                                    uint16_t gray_for_marking,
                                                    uint16_t black_for_marking) const override {
            State expected = _state.load_relaxed();
            for (;;) {
                switch (expected) {
                    case READY:
                        printf("%p:HeapString was READY for collector\n", _weak);
                        if (_weak->_black & next_delete_mask) {
                            printf("%p:HeapString was READY+BLACK for collector\n", _weak);
                            // is k-strong-reachable; take no action
                            return;
                        }
                        if (_state.compare_exchange_weak_relaxed_relaxed(expected, GONE)) {
                            printf("%p:HeapString was READY+WHITE -> GONE for collector\n", _weak);
                            // is not k-strong reachable, and has not been loaded
                            _heap_string_ctrie_collector_try_erase(this);
                            return;
                        }
                        break;
                    case WAS_LOADED:
                        printf("%p:HeapString WAS_LOADED for collector\n", _weak);
                        if (_state.compare_exchange_weak_relaxed_relaxed(expected, READY)) {
                            printf("%p:HeapString WAS_LOADED->READY+BLACK for collector\n", _weak);
                            _weak->_gray.fetch_or_relaxed(gray_for_marking);
                            _weak->_black |= black_for_marking;
                            return;
                        }
                        break;
                    case GONE:
                        printf("%p:HeapString was GONE for collector\n", _weak);
                        return;
                    default:
                        std::unreachable();
                }
            }
        }

        virtual void _garbage_collected_scan() const override {

        }

    };


    struct std_string_Hasher {
        std::size_t operator()(std::string const& x) const {
            return wry::hash_combine(x.data(), x.size());
        }
    };

    inline void garbage_collected_scan(std_string_Hasher const&) {}


    using WeakDict = Ctrie<std::string, WeakHolder<HeapString> const*, std_string_Hasher>;

    inline WeakDict* heap_string_weak_dictionary() {
        static Root<WeakDict*> _ct{new WeakDict};
        return _ct._ptr;
    }

    inline HeapString const* _heap_string_ctrie_mutator_find_upgrade_or_emplace(std::string s) {
        WeakDict* t = heap_string_weak_dictionary();
        auto [before, after] = t->alter(s, [&s](std::optional<WeakHolder<HeapString> const*> before) {
            if (before.has_value()) {
                // It does exist, try to get permission:
                if (before.value()->mutator_try_upgrade()) {
                    printf("%p:HeapString: mutator upgraded\n", before.value()->_weak);
                    return WeakDict::AlterChoice::keep();
                }
                printf("%p:HeapString: mutator upgrade failed\n", before.value()->_weak);
                // On this path, _weak is GONE and may already be invalid
            }
            // Compete to install a new string
            HeapString const* nhs = HeapString::make(s);
            printf("%p:HeapString: mutator tries to install\n", nhs);
            WeakHolder<HeapString> const* nwh = new WeakHolder<HeapString>{nhs};
            return WeakDict::AlterChoice::replace(nwh);
        });
        return (*after)->_weak;
    }



    inline void _heap_string_ctrie_collector_try_erase(WeakHolder<HeapString> const* victim) {
        WeakDict* t = heap_string_weak_dictionary();
        // The collector is in charge of killing the string, so it knows it is
        // still alive, so we can get the key out of it
        std::string s{victim->_weak->as_string_view()};
        (void) t->alter(s, [victim, &s](std::optional<WeakHolder<HeapString> const*> before) {
            if (before.has_value() && before.value() == victim) {
                printf("HeapString: collector erases\n");
                return WeakDict::AlterChoice::erase();
            } else {
                printf("HeapString: collector found replacement\n");
                return WeakDict::AlterChoice::keep();
            }
        });
    }

} // namespace wry

#endif /* wry_gc_HeapString_hpp */
