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
            // auto q = p->clone_and_erase(k);
            //decltype(p) q = nullptr;
            //assert(!q);
            //auto r = Node<int>::make_with_key_value(k+100,v+1);
            /*
            auto s = Node<int>::merge(p, r);
            assert(s->contains(k));
            assert(!s->contains(k+1));
            assert(s->contains(k+100));
            q = Node<int>::make_with_key_value(k+2,v+2);
            s = Node<int>::merge(s, q);
            assert(s->contains(k));
            assert(!s->contains(k+1));
            assert(s->contains(k+2));
            assert(s->contains(k+100));
            int u = 0;
            assert(s->try_get(k, u));
            assert(u == v);
            assert(!s->try_get(k+1, u));
            assert(s->try_get(k+2, u));
            assert(u == v+2);
            assert(s->try_get(k+100, u));
            assert(u == v+1);
             */
        }
        
        mutator_repin();
        
        // stress test
        /*
         {
         const Node<int>* p = nullptr;
         for (int i = 0; i != 1 << 20; ++i) {
         int a = rand() & 0xFF;
         int b = rand() & 0xFF;
         uint64_t k = (uint64_t)a * (uint64_t)b;
         auto q = Node<int>::make_with_key_value(k, a);
         p = p ? Node<int>::merge(p, q) : q;
         //garbage_collected_shade(p);
         //garbage_collected_shade(q);
         mutator_handshake();
         // garbage_collected_shade p because we use it after the handshake
         p->_garbage_collected_shade();
         //garbage_collected_shade(p);
         //garbage_collected_shade(q);
         }
         }
         */
        {
            PersistentMap<uint64_t, int> p;
            std::map<uint64_t, int> m;
            const int N = 65536 / 8;
            for (int i = 0; i != N; ++i) {
                uint64_t k = std::rand() & (64 * 1024 - 1);
                int v = std::rand();
                
                uint64_t h = std::rand() & (64 * 1024 - 1);
                m.erase(h);
                int _ = {};
                garbage_collected_shade(p);
                (void) p.try_erase(h, _);
                                
                m.insert_or_assign(k, v);
                garbage_collected_shade(p);
                p.set(k, v);
                
                
                int u = {};
                if (!p.try_get(k, u)) {
                    printf("expected to find {%llx, %x}\n", k, v);
                    abort();
                }
                if (u != v) {
                    printf("expected to find {%llx, %x}, found {%llx, %x}\n", k, v, k, u);
                    abort();
                }
                
                if (!(i & 255))
                    mutator_repin();
                // printf("PMT %d\n", i);
            }
            for (uint64_t k = 0; k != N; ++k) {
                garbage_collected_shade(p);
                if (m.count(k)) {
                    int v = {};
                    bool result = p.try_get(k, v);
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


