//
//  persistent_array.cpp
//  client
//
//  Created by Antony Searle on 31/5/2026.
//

#include "persistent_array.hpp"
#include "test.hpp"

namespace wry {

    // Smoke test for the PersistentArray placeholder sequence.  A "sequence" is
    // a (possibly null) node pointer the caller rebinds; the static ops build
    // new nodes and return new tops.
    define_test("PersistentArray") {

        using A = PersistentArray<int>;

        // empty
        {
            A const* a = nullptr;
            require(A::is_empty(a));
            require(A::size(a) == 0);
            auto [l, r] = A::split(a, 0);
            require(!l && !r);
            require(A::cat(a, a) == nullptr);
        }

        mutator_repin();

        // push at both ends / at / front / back, then pop at both ends
        {
            A const* a = nullptr;
            a = A::push_back(a, 2);     // [2]
            a = A::push_back(a, 3);     // [2, 3]
            a = A::push_front(a, 1);    // [1, 2, 3]

            require(A::size(a) == 3);
            require(A::at(a, 0) == 1);
            require(A::at(a, 1) == 2);
            require(A::at(a, 2) == 3);
            require(A::front(a) == 1);
            require(A::back(a) == 3);

            a = A::pop_front(a);        // [2, 3]
            require(A::front(a) == 2);
            a = A::pop_back(a);         // [2]
            require(A::size(a) == 1);
            require(A::front(a) == 2 && A::back(a) == 2);
            a = A::pop_back(a);         // []
            require(A::is_empty(a));
        }

        mutator_repin();

        // from() bulk build + cat, with empty-identity and source immutability
        {
            int xs[] = {0, 1, 2};
            int ys[] = {3, 4, 5};
            A const* a = A::from(xs, 3);
            A const* b = A::from(ys, 3);
            A const* c = A::cat(a, b);                  // [0..5]
            require(A::size(c) == 6);
            for (int i = 0; i != 6; ++i)
                require(A::at(c, i) == i);

            require(A::cat(nullptr, c) == c);            // empty is the identity
            require(A::cat(c, nullptr) == c);
            require(A::size(a) == 3 && A::size(b) == 3); // sources untouched
        }

        mutator_repin();

        // split, boundary sharing, and split-then-cat round trip
        {
            int xs[] = {0, 1, 2, 3, 4, 5};
            A const* a = A::from(xs, 6);

            auto [l, r] = A::split(a, 2);               // [0,1] | [2,3,4,5]
            require(A::size(l) == 2 && A::size(r) == 4);
            require(A::back(l) == 1 && A::front(r) == 2);

            auto [l0, r0] = A::split(a, 0);             // boundaries share input
            require(l0 == nullptr && r0 == a);
            auto [ln, rn] = A::split(a, 6);
            require(ln == a && rn == nullptr);

            A const* j = A::cat(l, r);                  // reconstruct
            require(A::size(j) == 6);
            for (int i = 0; i != 6; ++i)
                require(A::at(j, i) == i);
            require(A::size(a) == 6);                    // original intact
        }

        co_return;
    };

} // namespace wry
