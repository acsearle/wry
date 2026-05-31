//
//  persistent_map.cpp
//  client
//
//  Created by Antony Searle on 24/11/2024.
//

#include <cstdlib>

#include "persistent_map.hpp"
#include "test.hpp"

namespace wry {
    
    define_test("PersistentMap") {
                               
        {
            uint64_t k = 6435475;
            int v = 4568;
            // auto p = Node<int>::make_with_key_value(k, v);
            PersistentMap<uint64_t, int> m;
            m.set(k, v);
            
            assert(m.contains(k));
            assert(!m.contains(k+1));            
        }
        
        mutator_repin();
        
        // stress test
        {
            PersistentMap<uint64_t, int> p;
            Root<decltype(p._inner)> hack;
            std::map<uint64_t, int> m;
            const int N = 65536 / 8;
            for (int i = 0; i != N; ++i) {
                uint64_t k = std::rand() & (64 * 1024 - 1);
                int v = std::rand();
                
                uint64_t h = std::rand() & (64 * 1024 - 1);
                m.erase(h);
                int _ = {};
                (void) p.try_erase(h, _);
                hack = p._inner;

                m.insert_or_assign(k, v);
                p.set(k, v);
                hack = p._inner;

                
                int u = {};
                if (!p.try_get(k, u)) {
                    printf("expected to find {%llx, %x}\n", k, v);
                    abort();
                }
                hack = p._inner;
                if (u != v) {
                    printf("expected to find {%llx, %x}, found {%llx, %x}\n", k, v, k, u);
                    abort();
                }
                
                if (!(i & 255))
                    mutator_repin();
                // printf("PMT %d\n", i);
            }
            for (uint64_t k = 0; k != N; ++k) {
                if (m.count(k)) {
                    int v = {};
                    bool result = p.try_get(k, v);
                    hack = p._inner;
                    assert(result);
                    assert(v == m[k]);
                } else {
                    int v = {};
                    assert(!p.try_get(k, v));
                }
                if (!(k & 255))
                    mutator_repin();
                // printf("PMT %llu\n", k);
            }
            
        }
        co_return;
                
    };
}


