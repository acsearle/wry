//
//  concurrent_skiplist.cpp
//  client
//
//  Created by Antony Searle on 23/11/2024.
//

#include <map>
#include <set>

#include "concurrent_skiplist.hpp"

#include "test.hpp"

namespace wry {
    
    define_test("skiplist") {
                
        thread_local_random_number_generator = new std::ranlux24;

        {
            ConcurrentSkiplistSet<int> a;
            std::set<int> b;
            
            int N = 1 << 7;
            for (int i = 0; i != N; ++i) {
                int j = rand() & (N - 1);
                a.try_emplace(j);
                b.emplace(j);
                for (int k = 0; k != N; ++k) {
                    auto c = a.find(k);
                    auto d = b.find(k);
                    assert((c == a.end()) == (d == b.end()));
                    if (c != a.end()) {
                        assert(*c == *d);
                    }
                }
            }
        }
                
        {
            
            
            ConcurrentSkiplistMap<int, int> a;
            std::map<int, int> b;
            
            int N = 1 << 7;
            for (int i = 0; i != N; ++i) {
                int j = rand() & (N - 1);
                int v = rand() & (N - 1);
                a.try_emplace(j, v);
                b.try_emplace(j, v);
                for (int k = 0; k != N; ++k) {
                    auto c = a.find(k);
                    auto d = b.find(k);
                    assert((c == a.end()) == (d == b.end()));
                    if (c != a.end()) {
                        assert(c->first == d->first);
                        assert(c->second == d->second);
                        // printf("[%d] = %d\n", c->first, c->second);
                    }
                }
            }
        }
                
        ArenaAllocator::clear();

    };
    
} // namespace wry
