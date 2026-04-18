//
//  stable_priority_queue.cpp
//  client
//
//  Created by Antony Searle on 5/11/2023.
//

#include <vector>
#include <random>
#include <queue>

#include "stable_priority_queue.hpp"

#include "algorithm.hpp"
#include "debug.hpp"
#include "test.hpp"

namespace wry {

    
    struct LessFirst {
        static int _ops;
        bool operator()(const auto& a, const auto& b) const {
            ++_ops;
            return a.first < b.first;
        }
    };
    
    int LessFirst::_ops = 0;
    
    define_test("StablePriorityQueue") {
        // Cross-check StablePriorityQueue against std::priority_queue on a
        // mixed insert/extract workload.  Each element is a (key, tag) pair
        // where the tag is a unique monotonically-increasing integer, and
        // LessFirst only inspects the key — so every matching key is a tie
        // that must be broken by seniority.
        //
        // SPQ returns the oldest (smallest tag) on ties.  std::priority_queue
        // with std::greater<pair> agrees because lexicographic ordering also
        // selects the smaller second component when firsts are equal.
        //
        // We run three passes that differ in key-space density so that
        // ties are exercised heavily (not just incidentally).

        std::default_random_engine rne(0xC0FFEEu);

        auto run_pass = [&rne](int insertions, int key_mod, const char* label) {
            LessFirst::_ops = 0;
            StablePriorityQueue<std::pair<int, int>, LessFirst> q;
            std::priority_queue<std::pair<int, int>,
                                std::vector<std::pair<int, int>>,
                                std::greater<>> r;

            std::uniform_int_distribution<int> key_d(0, key_mod - 1);
            std::uniform_int_distribution<int> bias_d(0, 2);

            std::size_t size_sum = 0;
            int ops = 0;

            // Mix inserts and extracts, biased toward inserts while there
            // are still budgeted insertions, then drain.
            for (int i = 0; i < insertions || q._size > 0; ++i, ++ops) {
                bool do_insert = (i < insertions) &&
                                 (q._size == 0 || bias_d(rne) != 0);
                if (do_insert) {
                    int k = key_d(rne);
                    q.insert({k, i});
                    r.push({k, i});
                } else {
                    auto a = q.stable_extract_min();
                    auto b = r.top();
                    r.pop();
                    assert(a == b);
                }
                size_sum += q._size;
            }
            assert(r.empty());
            printf("[SPQ %-10s] ops=%d cmp/op=%.2f avg-size=%.2f\n",
                   label, ops,
                   (double) LessFirst::_ops / ops,
                   (double) size_sum / ops);
        };

        run_pass(10000, 10000, "sparse");      // keys rarely collide
        run_pass( 5000,     8, "heavy-ties");  // many ties per key
        run_pass( 1000,     1, "all-tied");    // every key identical

        co_return;
    };
    
} // namespace wry
