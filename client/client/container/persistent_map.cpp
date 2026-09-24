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
            // The interleaved erases must have left canonical shape: the
            // trie that sorted insertion of the surviving content builds.
            {
                using A = decltype(p)::AMT;
                const A* ref = nullptr;
                for (auto [k, v] : m)
                    ref = A::insert(ref, k, v);
                const A* actual = p._inner ? &*p._inner : nullptr;
                A::assert_canonical(actual);
                assert(A::same_shape(actual, ref));
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

        // Root pin for the whole work tree: parked rebuild continuations
        // hold FrozenCursor state in bump slabs, which rotate after 3
        // epoch advances (see the drain-loop contract in
        // global_work_queue.cpp).
        auto guard = pin_global_epoch();

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

            // Canonical shape: whichever mix of serial and parallel paths the
            // rebuild took, the result must be the sorted-insertion build of
            // the expected content.
            A::assert_canonical(result);
            {
                const A* ref = nullptr;
                for (auto [k, v] : expect)
                    ref = A::insert(ref, k, v);
                assert(A::same_shape(result, ref));
            }

            if (!(iter & 7))
                mutator_repin();
        }

        unpin_global_epoch(guard);
        co_return;

    };

    // Stage 1 end-to-end: materialize a real ConcurrentSkiplistMap modifier and
    // rebuild a PersistentMap, vs a std::map oracle (+ source immutability).
    define_test("persistentmap_parallel_rebuild") {

        // Root pin for the whole work tree; see amt_parallel_rebuild.
        auto guard = pin_global_epoch();

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

            PM result = co_await coroutine_parallel_rebuild(src, freeze(modifier), action_for_key);

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

            // Canonical shape, as in amt_parallel_rebuild.
            {
                using A = PM::AMT;
                const A* ref = nullptr;
                for (auto [k, v] : expect)
                    ref = A::insert(ref, k, v);
                const A* actual = result._inner ? &*result._inner : nullptr;
                A::assert_canonical(actual);
                assert(A::same_shape(actual, ref));
            }

            if (!(iter & 7))
                mutator_repin();
        }

        unpin_global_epoch(guard);
        co_return;

    };

    // Erase must leave the trie in canonical form: no emptied leaf lingers,
    // and a parent left with one child collapses to it.  Shapes are checked
    // against tries built by insertion alone, which never produce either
    // artefact, and insertion in reversed order pins down that shape is a
    // function of the key set rather than of history.  Keys 0, 32, 64 are
    // sibling leaves under a shift-5 parent; a key near 2^40 forces a far
    // ancestor.
    define_test("amt_erase_canonical") {

        using A = PersistentMap<uint64_t, int>::AMT;

        auto build = [](std::initializer_list<uint64_t> keys) -> const A* {
            const A* acc = nullptr;
            for (uint64_t k : keys)
                acc = A::insert(acc, k, (int)k);
            return acc;
        };
        auto erase = [](const A* root, uint64_t k) -> const A* {
            int victim = -1;
            auto [next, did_erase] = root->clone_and_erase_key(k, victim);
            assert(did_erase);
            assert(victim == (int)k);
            return next;
        };

        // Sole member of the only leaf: the trie becomes empty.
        {
            const A* t = build({7});
            assert(erase(t, 7) == nullptr);
        }

        // Two sibling leaves.  Emptying one collapses the parent to the
        // other, which is what inserting only the survivor builds.  Before
        // the fix this left a parent over an empty leaf.  The source is
        // untouched.
        {
            const A* t = build({0, 32});
            const A* u = erase(t, 32);
            A::assert_canonical(u);
            assert(A::same_shape(u, build({0})));
            assert(!A::same_shape(u, t));
            int v = 0;
            assert(t->try_get(32, v) && v == 32);
            assert(!u->try_get(32, v));
        }

        // Three sibling leaves: emptying one leaves a two-child parent.
        {
            const A* t = build({0, 32, 64});
            const A* u = erase(t, 32);
            A::assert_canonical(u);
            assert(A::same_shape(u, build({0, 64})));
        }

        // Collapse across levels.  Emptying the far leaf collapses the root
        // to the near subtree; emptying both near leaves collapses the root
        // to the far leaf.
        {
            uint64_t far = (uint64_t)1 << 40;
            const A* t = build({0, 32, far});
            A::assert_canonical(t);
            const A* u = erase(t, far);
            A::assert_canonical(u);
            assert(A::same_shape(u, build({0, 32})));
            const A* w = erase(erase(t, 0), 32);
            A::assert_canonical(w);
            assert(A::same_shape(w, build({far})));
        }

        // Partial leaf erase keeps the leaf.
        {
            const A* t = build({3, 5});
            const A* u = erase(t, 3);
            A::assert_canonical(u);
            assert(A::same_shape(u, build({5})));
        }

        // Insertion order does not matter.
        {
            const A* a = build({1, 40, 3000, 9, (uint64_t)1 << 50, 41});
            const A* b = build({41, (uint64_t)1 << 50, 9, 3000, 40, 1});
            A::assert_canonical(a);
            assert(A::same_shape(a, b));
        }

        // Randomized: interleaved sets and erases through a rooted
        // PersistentMap (so the trie survives repins) against an oracle.
        // After every step the trie must have the shape that sorted
        // insertion of the oracle's contents builds.
        {
            PersistentMap<uint64_t, int, DefaultKeyService<uint64_t>, RootDiscipline> p;
            std::map<uint64_t, int> oracle;
            for (int i = 0; i != 2000; ++i) {
                uint64_t k = std::rand() % 300;
                if (std::rand() & 1) {
                    int v = std::rand();
                    p.set(k, v);
                    oracle[k] = v;
                } else {
                    int victim = -1;
                    bool did_erase = p.try_erase(k, victim);
                    auto it = oracle.find(k);
                    assert(did_erase == (it != oracle.end()));
                    if (did_erase) {
                        assert(victim == it->second);
                        oracle.erase(it);
                    }
                }
                const A* ref = nullptr;
                for (auto [ok, ov] : oracle)
                    ref = A::insert(ref, ok, ov);
                const A* actual = p._inner ? &*p._inner : nullptr;
                A::assert_canonical(actual);
                assert(A::same_shape(actual, ref));
                if (!(i & 255))
                    mutator_repin();
            }
        }

        co_return;

    };
}
