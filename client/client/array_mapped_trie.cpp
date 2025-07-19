//
//  array_mapped_trie.cpp
//  client
//
//  Created by Antony Searle on 12/7/2025.
//

#include <map>

#include "array_mapped_trie.hpp"

#include "gc.hpp"
#include "test.hpp"

namespace wry::_amt0 {
    
    define_test("AMT") {
        mutator_become_with_name("AMT test");
        
        {
            for (int i = 0; i != 63; ++i) {
                assert(decode(i) == ((uint64_t)1 << i));
                assert(shift_for_keylike_difference(decode(i)) == (i / 6) * 6);
                assert(encode(decode(i)) == i);
            }
        }
        
        mutator_handshake();
        
        {
            uint64_t k = 6435475;
            int v = 4568;
            auto p = Node<int>::make_with_key_value(k, v);
            assert(p);
            assert(p->contains(k));
            assert(!p->contains(k+1));
            // auto q = p->clone_and_erase(k);
            decltype(p) q = nullptr;
            assert(!q);
            auto r = Node<int>::make_with_key_value(k+100,v+1);
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
        }
                
        mutator_handshake();
                
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
                //shade(p);
                //shade(q);
                mutator_handshake();
                // shade p because we use it after the handshake
                p->_garbage_collected_shade();
                //shade(p);
                //shade(q);
            }
        }
         */
        {
            const Node<int>* p = nullptr;
            std::map<uint64_t, int> m;
            for (int i = 0; i != 1024; ++i) {
                uint64_t k = rand() & (64 * 1024 - 1); // We will have some missing and some multiply occipied leaves
                int v = rand();
                m.insert_or_assign(k, v);
                // auto q = Node<int>::make_with_key_value(k, v);
                // printf("insert_or_assign {%llx, %x}\n", k, v);
                // p = p ? Node<int>::merge(p, q) : q;
                p = (p
                     ? p->clone_and_insert_or_assign_key_value(k, v)
                     : Node<int>::make_with_key_value(k, v));
                
                int u = {};
                if (!p->try_get(k, u)) {
                    printf("expected to find {%llx, %x}\n", k, v);
                    abort();
                }
                if (u != v) {
                    printf("expected to find {%llx, %x}, found {%llx, %x}\n", k, v, k, u);
                    abort();
                }
                mutator_handshake();
                shade(p);
            }
            for (uint64_t k = 0; k != 65536; ++k) {
                if (m.count(k)) {
                    int v = {};
                    bool result = p->try_get(k, v);
                    assert(result);
                    assert(v == m[k]);
                } else {
                    int v = {};
                    assert(!p->try_get(k, v));
                }
                mutator_handshake();
                shade(p);
            }
            
        }
        
         
        mutator_resign();
    };
        
}
