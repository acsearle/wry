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
            PersistentMap<uint64_t, int, DefaultKeyService<uint64_t>, RootDiscipline> p;
            std::map<uint64_t, int> m;
            const int N = 65536 / 8;
            for (int i = 0; i != N; ++i) {
                uint64_t k = std::rand() & (64 * 1024 - 1);
                int v = std::rand();
                
                uint64_t h = std::rand() & (64 * 1024 - 1);
                m.erase(h);
                int _ = {};
                (void) p.try_erase(h, _);

                m.insert_or_assign(k, v);
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

    // Differential test: AMT parallel rebuild vs a std::map oracle, also
    // checking the source trie is left unchanged (structural-sharing safety).
    define_test("amt_parallel_rebuild") {

        using A = PersistentMap<uint64_t, int>::AMT;

        struct Mod { enum Tag { Write, Clear } tag; int value; };
        auto combine = [](const int* old, const Mod& m) -> std::optional<int> {
            (void) old;
            if (m.tag == Mod::Write)
                return m.value;
            return std::nullopt; // CLEAR
        };

        const uint64_t key_domain = 200; // small domain forces collisions and depth

        for (int iter = 0; iter != 300; ++iter) {

            // Random source trie + oracle.
            std::map<uint64_t, int> oracle;
            const A* src = nullptr;
            int ns = std::rand() % 80;
            for (int s = 0; s != ns; ++s) {
                uint64_t k = std::rand() % key_domain;
                int v = std::rand();
                src = A::insert(src, k, v);
                oracle[k] = v;
            }

            // Random modifiers: unique keys (std::map dedups), sorted ascending.
            std::map<uint64_t, Mod> mm;
            int nm = std::rand() % 40;
            for (int m = 0; m != nm; ++m) {
                uint64_t k = std::rand() % key_domain;
                if (std::rand() & 1)
                    mm[k] = Mod{Mod::Write, std::rand()};
                else
                    mm[k] = Mod{Mod::Clear, 0};
            }
            std::vector<std::pair<uint64_t, Mod>> mods(mm.begin(), mm.end());

            // Expected result.
            std::map<uint64_t, int> expect = oracle;
            for (auto& [k, m] : mm) {
                if (m.tag == Mod::Write)
                    expect[k] = m.value;
                else
                    expect.erase(k);
            }

            const A* result = co_await A::coroutine_parallel_rebuild(
                src, mods, 0, mods.size(), combine);

            for (uint64_t k = 0; k != key_domain; ++k) {
                int v = 0;
                bool has = result && result->try_get(k, v);
                auto it = expect.find(k);
                assert(has == (it != expect.end()));
                if (has)
                    assert(v == it->second);
                // source must be untouched by the rebuild
                int sv = 0;
                bool shas = src && src->try_get(k, sv);
                auto sit = oracle.find(k);
                assert(shas == (sit != oracle.end()));
                if (shas)
                    assert(sv == sit->second);
            }

            if (!(iter & 7))
                mutator_repin();
        }

        co_return;

    };

    // Stage 1 end-to-end: materialize a real ConcurrentSkiplistMap modifier and
    // rebuild a PersistentMap, vs a std::map oracle (+ source immutability).
    define_test("persistentmap_parallel_rebuild") {

        using PM = PersistentMap<uint64_t, int>;
        using Action = ParallelRebuildAction<int>;
        const uint64_t key_domain = 200;

        for (int iter = 0; iter != 200; ++iter) {

            std::map<uint64_t, int> oracle;
            PM src;
            int ns = std::rand() % 80;
            for (int s = 0; s != ns; ++s) {
                uint64_t k = std::rand() % key_domain;
                int v = std::rand();
                src.set(k, v);
                oracle[k] = v;
            }

            // Build a real skiplist modifier with WRITE / CLEAR / NONE actions.
            ConcurrentMap<uint64_t, Action, DefaultKeyService<uint64_t>, EpochDiscipline> modifier;
            std::map<uint64_t, Action> mm;
            int nm = std::rand() % 40;
            for (int m = 0; m != nm; ++m) {
                uint64_t k = std::rand() % key_domain;
                int r = std::rand() % 3;
                Action a = (r == 0) ? Action{Action::WRITE_VALUE, std::rand()}
                         : (r == 1) ? Action{Action::CLEAR_VALUE, 0}
                                    : Action{Action::NONE, 0};
                mm[k] = a;
            }
            for (auto& [k, a] : mm)
                modifier.try_emplace(k, a);

            std::map<uint64_t, int> expect = oracle;
            for (auto& [k, a] : mm) {
                if (a.tag == Action::WRITE_VALUE)
                    expect[k] = a.value;
                else if (a.tag == Action::CLEAR_VALUE)
                    expect.erase(k);
                // NONE: no change
            }

            auto action_for_key = [](auto&& kv) -> Coroutine::Future<Action> {
                co_return kv.second;
            };

            PM result = co_await coroutine_parallel_rebuild(src, modifier, action_for_key);

            for (uint64_t k = 0; k != key_domain; ++k) {
                int v = 0;
                bool has = result.try_get(k, v);
                auto it = expect.find(k);
                assert(has == (it != expect.end()));
                if (has)
                    assert(v == it->second);
                int sv = 0;
                bool shas = src.try_get(k, sv);
                auto sit = oracle.find(k);
                assert(shas == (sit != oracle.end()));
                if (shas)
                    assert(sv == sit->second);
            }

            if (!(iter & 7))
                mutator_repin();
        }

        co_return;

    };
}


