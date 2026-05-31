//
//  persistent_stack.hpp
//  client
//
//  Created by Antony Searle on 18/10/2025.
//

#ifndef persistent_stack_hpp
#define persistent_stack_hpp

#include "assert.hpp"
#include "garbage_collected.hpp"
#include "utility.hpp"

namespace wry {

    // Persistent stack: a classic functional cons list.
    //
    // There is no wrapper object -- a stack IS a (possibly null) pointer to
    // its top node, owned by the caller.  The caller stores that pointer
    // however its lifetime demands:
    //
    //   - a plain `PersistentStack<T> const*` local, while building a
    //     logically-immutable object that has not yet been published to the
    //     collector (no write barrier needed; the collector cannot have
    //     observed the displaced edge);
    //   - a `GarbageCollectedSlot<PersistentStack<T> const*>` field inside a
    //     live garbage-collected object (the slot barriers each overwrite);
    //   - a `Root<PersistentStack<T> const*>` on a stack or coroutine frame.
    //
    // The operations are static methods that never mutate a node; they only
    // build new nodes and return a new top pointer.  Because the node is
    // reached through a pointer-to-const, immutability is enforced by the
    // type rather than by convention.  Scanning is the ordinary
    // GarbageCollected base behaviour; shading, if any, belongs to whichever
    // slot the caller stores the top pointer in.

    template<typename T>
    struct PersistentStack : GarbageCollected {

        PersistentStack const* _Nullable _next;
        T _payload;

        explicit
        PersistentStack(PersistentStack const* _Nullable next, auto&&... args)
        : _next{next}
        , _payload{FORWARD(args)...} {
        }

        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        virtual void _garbage_collected_scan() const override {
            using wry::garbage_collected_scan;
            garbage_collected_scan(_next);
            garbage_collected_scan(_payload);
        }

        // Push a value, returning the new top.
        [[nodiscard]] static auto
        push(PersistentStack const* _Nullable top, auto&&... args) -> PersistentStack const* _Nonnull {
            return new PersistentStack(top, FORWARD(args)...);
        }

        // The stack below the top.  Precondition: non-empty.
        [[nodiscard]] static auto
        tail(PersistentStack const* _Nonnull top) -> PersistentStack const* _Nullable {
            assert(top);
            return top->_next;
        }

        // The top value.  Precondition: non-empty.
        [[nodiscard]] static auto
        peek(PersistentStack const* _Nonnull top) -> T {
            assert(top);
            return top->_payload;
        }

        [[nodiscard]] static auto
        is_empty(PersistentStack const* _Nullable top) -> bool {
            return !top;
        }

        // Drop up to n leading elements.
        [[nodiscard]] static auto
        drop(PersistentStack const* _Nullable top, size_t n) -> PersistentStack const* _Nullable {
            while (top && n) {
                top = top->_next;
                --n;
            }
            return top;
        }

        static auto
        size(PersistentStack const* _Nullable top) -> size_t {
            size_t n = 0;
            for (; top; top = top->_next)
                ++n;
            return n;
        }

        // Indexed access from the top outward.  Debugging convenience.
        [[nodiscard]] static auto
        at(PersistentStack const* _Nonnull top, ptrdiff_t i) -> T {
            assert(i >= 0);
            while (i) {
                assert(top);
                top = top->_next;
                --i;
            }
            assert(top);
            return top->_payload;
        }

    };

} // namespace wry

#endif /* persistent_stack_hpp */
