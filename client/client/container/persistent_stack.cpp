//
//  persistent_stack.cpp
//  client
//
//  Created by Antony Searle on 18/10/2025.
//

#include "persistent_stack.hpp"
#include "test.hpp"

namespace wry {

    // Smoke test for the PersistentStack cons-list node and its static
    // operations.  A "stack" is just a (possibly null) node pointer that the
    // caller owns and rebinds; the operations build new nodes and return a new
    // top.
    define_test("PersistentStack") {

        using S = PersistentStack<int>;

        // empty stack
        {
            S const* s = nullptr;
            assert(S::is_empty(s));
            assert(S::size(s) == 0);
            assert(S::drop(s, 3) == nullptr);
        }

        mutator_repin();

        // push / peek / index / LIFO
        {
            S const* s = nullptr;
            s = S::push(s, 1);
            s = S::push(s, 2);
            s = S::push(s, 3);          // [3, 2, 1]

            assert(!S::is_empty(s));
            assert(S::size(s) == 3);
            assert(S::peek(s) == 3);

            // at() indexes from the top outward
            assert(S::at(s, 0) == 3);
            assert(S::at(s, 1) == 2);
            assert(S::at(s, 2) == 1);

            // tail walks toward the bottom, last-in-first-out
            s = S::tail(s);
            assert(S::peek(s) == 2);
            s = S::tail(s);
            assert(S::peek(s) == 1);
            s = S::tail(s);
            assert(S::is_empty(s));
        }

        mutator_repin();

        // structural sharing: two tops over one chain evolve independently
        {
            S const* a = nullptr;
            a = S::push(a, 10);
            a = S::push(a, 20);         // a = [20, 10]

            S const* b = S::tail(a);    // b shares the [10] tail of a
            assert(S::size(b) == 1);
            assert(S::peek(b) == 10);
            assert(S::size(a) == 2);    // a is unaffected
            assert(S::peek(a) == 20);

            // drop is the n-step tail
            assert(S::drop(a, 0) == a);
            assert(S::size(S::drop(a, 1)) == 1);
            assert(S::is_empty(S::drop(a, 2)));
            assert(S::is_empty(S::drop(a, 100)));
        }

        co_return;
    };

} // namespace wry
