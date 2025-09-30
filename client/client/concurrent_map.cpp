//
//  concurrent_map.cpp
//  client
//
//  Created by Antony Searle on 19/7/2025.
//

#include <map>
#include <set>

#include "concurrent_map.hpp"

#include "test.hpp"

namespace wry::concurrent_map {
    
    define_test("ConcurrentMap") {
        
        thread_local_random_number_generator = new std::ranlux24;
                
        {
            ConcurrentMap<int, int> a;
            std::map<int, int> b;
        
            int N = 1000;
            for (int i = 0; i != N; ++i) {
                int k = rand() % N;
                int v = rand() % N;
                auto [p, f] = a.try_emplace(k, v);
                auto [q, g] = b.try_emplace(k, v);
                assert(f == g);
                if (!p) {
                    assert(p->second == q->second);
                }
            }
            
            for (auto [k, v] : a) {
                auto q = b.find(k);
                assert(q != b.end());
                assert(q->first == k);
                assert(q->second == v);
            }

            for (auto [k, v] : b) {
                auto p = a.find(k);
                assert(p != a.end());
                assert(p->first == k);
                assert(p->second == v);
            }

        }

        // It is safe to clear, but perhaps we should reset and let the test
        // harness clear once before the thread goes down?
        ArenaAllocator::clear();

    }; // define_test("ConcurrentMap")
    
} // namespace wry::concurrent_map
