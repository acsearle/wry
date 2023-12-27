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
        return;
        
        std::random_device rd;
        std::default_random_engine rne(rd());
        std::uniform_int_distribution<> uid; // 0 .. INT_MAX
            
        StablePriorityQueue<std::pair<int, int>, LessFirst> q;
        std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, std::greater<>> r;
                
        int N = 10000;
        std::size_t mm = 0;
        
        for (int i = 0;; ++i) {
            int j = rand() % N;
            if (j > i) {
                int k = rand() % (q._size + 1);
                q.insert({k, i});
                r.push({k, i});
                printf("pushed (%d,%d)\n", k, i);
            } else {
                if (!q._size) {
                    printf("%g comparisons per operation, average size %g\n", (double) LessFirst::_ops / i, (double) mm / i);
                    assert(r.empty());
                    break;
                }
                auto a = q.stable_extract_min();
                auto b = r.top();
                r.pop();
                printf("popped (%d,%d) ? (%d,%d)\n", a.first, a.second, b.first, b.second);
                assert(a == b);
            }
            mm += q._size;
        }
        
        /*
        for (int i = 0; i != 1000; ++i) {
            auto j = q.stable_extract_min();
            auto k = r.top();
            r.pop();
            assert(j == k);
            // printf("%d ? [%d]->{%d, %d}\n", j, i, r[i].first, r[i].second);
        }
         */
        
    };
    
} // namespace wry
