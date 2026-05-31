//
//  persistent_stack.cpp
//  client
//
//  Created by Antony Searle on 18/10/2025.
//

#include "persistent_stack.hpp"
#include "test.hpp"

namespace wry {

    // Smoke test for PersistentStack: both the mutable pointer-swinging family
    // (push/pop/peek) and the functional family that returns new handles over
    // shared nodes (singleton/emplace/tail/drop).

    define_test("PersistentStack") {

        // empty stack
        {
            PersistentStack<int> s;
            assert(s.is_empty());
            assert(s.size() == 0);
            assert(s.pop_else(-1) == -1);
            assert(s.peek_else(-1) == -1);
            assert(s.is_empty());
        }

        mutator_repin();

        // push / peek / index / LIFO pop
        {
            PersistentStack<int> s;
            s.push(1);
            s.push(2);
            s.push(3);

            assert(!s.is_empty());
            assert(s.size() == 3);
            assert(s.peek() == 3);
            assert(s.peek_else(-1) == 3);

            // operator[] indexes from the head outward
            assert(s[0] == 3);
            assert(s[1] == 2);
            assert(s[2] == 1);

            // pop returns in last-in-first-out order
            assert(s.pop() == 3);
            assert(s.pop() == 2);
            assert(s.size() == 1);
            assert(s.peek() == 1);
            assert(s.pop_else(-1) == 1);

            // drained
            assert(s.is_empty());
            assert(s.size() == 0);
            assert(s.pop_else(42) == 42);
        }

        mutator_repin();

        // value semantics: copying a handle shares the backing nodes, and the
        // copies evolve independently as their heads are swung
        {
            PersistentStack<int> a;
            a.push(10);
            a.push(20);

            PersistentStack<int> b = a;     // shares nodes with a
            assert(b.size() == 2);
            assert(b.peek() == 20);

            assert(b.pop() == 20);          // only b's head moves
            assert(b.size() == 1);
            assert(a.size() == 2);          // a is unaffected
            assert(a.peek() == 20);
        }

        mutator_repin();

        // functional (const) interface: singleton/emplace/tail/drop return new
        // handles over shared nodes and leave the receiver unchanged
        {
            // singleton builds a one-element stack
            PersistentStack<int> one = PersistentStack<int>::singleton(7);
            assert(one.size() == 1);
            assert(one.peek() == 7);

            PersistentStack<int> base;
            base.push(1);
            base.push(2);                   // base = [2, 1]

            // emplace returns a grown stack; the receiver is untouched
            PersistentStack<int> grown = base.emplace(3);   // [3, 2, 1]
            assert(grown.size() == 3);
            assert(grown.peek() == 3);
            assert(base.size() == 2);       // base unchanged
            assert(base.peek() == 2);

            // tail returns the chain minus its head; the receiver is untouched
            PersistentStack<int> rest = grown.tail();        // [2, 1]
            assert(rest.size() == 2);
            assert(rest.peek() == 2);
            assert(grown.size() == 3);      // grown unchanged

            // drop(n) skips n leading elements: drop(0) shares the whole chain,
            // drop(>=size) yields empty
            assert(grown.drop(0).size() == 3);
            assert(grown.drop(2).size() == 1);
            assert(grown.drop(2).peek() == 1);
            assert(grown.drop(3).is_empty());
            assert(grown.drop(100).is_empty());
        }

        co_return;
    };

} // namespace wry
