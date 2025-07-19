//
//  ConcurrentSkiplist.cpp
//  client
//
//  Created by Antony Searle on 23/11/2024.
//

#include <map>
#include <set>

#include "gc.hpp"
#include "ConcurrentSkiplist.hpp"

#include "test.hpp"

namespace wry::concurrent_skiplist {
    
    define_test("skiplist") {
        
        mutator_become_with_name("skiplist");
        
        thread_local_random_number_generator = new std::ranlux24;

        {
            ConcurrentSkiplistSet<int> a;
            std::set<int> b;
            
            int N = 1 << 7;
            for (int i = 0; i != N; ++i) {
                int j = rand() & (N - 1);
                a.emplace(j);
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
        
        mutator_handshake();
        
        {
            
            
            ConcurrentSkiplistMap<int, int> a;
            std::map<int, int> b;
            
            int N = 1 << 7;
            for (int i = 0; i != N; ++i) {
                int j = rand() & (N - 1);
                int v = rand() & (N - 1);
                a.emplace(j, v);
                b.emplace(j, v);
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
        
        
        mutator_resign();
        
    };
    
} // namespace wry
