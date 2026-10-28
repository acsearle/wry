//
//  persistent_map.cpp
//  client
//
//  Created by Antony Searle on 24/11/2024.
//

#include <cstdlib>
#include <utility>

#include "persistent_map.hpp"
#include "persistent_set.hpp"
#include "test.hpp"

namespace wry {

    // The hash oracle: the sum over a std::map's entries of the trie's
    // own per-entry hash, which is the definition (array_mapped_trie.hpp)
    // with every cache bypassed.
    template<typename A, typename Key, typename V>
    static uint64_t oracle_hash(const std::map<Key, V>& m) {
        uint64_t sum = 0;
        for (auto const& [k, v] : m)
            sum += A::entry_hash_of(k, v);
        return sum;
    }

    // The content hash of a trie: the sum form, maintained by every
    // construction path.  Pinned constants first (any change to
    // splitmix64, hash_combine or the entry mix moves them, and the hash
    // is compared across peers and across save/load); then the empty trie;
    // an oracle check at every step of a random set/erase history; shape
    // independence across insertion order, the sorted builder and the
    // parallel rebuild; the nested case (a map of sets); and the
    // non-linearity the sum form depends on: moving a unit of value from
    // one cell to another changes the hash.
    define_test("amt_hash") {

        using A = PersistentMap<uint64_t, int>::AMT;
        using S = ArrayMappedTrie<__uint128_t, std::monostate, ScanDiscipline>;

        // Pinned (computed independently in Python from the definitions
        // in core/hash.hpp; regenerate the same way if they are ever
        // deliberately changed).
        {
            assert(A::entry_hash_of(1, 2) == 0xbcd9dbb49673066bull);
            std::map<uint64_t, int> m{{1, 2}, {3, 4}, {((uint64_t)1 << 40) | 5, 6}};
            assert(oracle_hash<A>(m) == 0xc0f6fb6c039ed651ull);
            const A* t = nullptr;
            for (auto [k, v] : m)
                t = A::insert(t, k, v);
            assert(content_hash(t) == 0xc0f6fb6c039ed651ull);
            __uint128_t codes[] = { (__uint128_t)7,
                                    ((__uint128_t)5 << 64) | 9,
                                    ((__uint128_t)1 << 70) | 3 };
            const S* s = S::build_from_sorted(std::begin(codes), std::end(codes));
            assert(content_hash(s) == 0x41e0cf37b76bd5fbull);
        }

        // Empty is zero; a singleton is its entry.
        assert(content_hash((const A*)nullptr) == 0);
        assert((PersistentMap<uint64_t, int>{}.hash() == 0));
        assert(content_hash(A::make_singleton(5, 50)) == A::entry_hash_of(5, 50));

        // Non-linearity: moving one unit from cell a to cell b changes the
        // sum, in both directions and for the degenerate equal case; and a
        // value change is seen in every cell, not only the low ones.
        {
            auto build = [](std::map<uint64_t, int> const& m) -> const A* {
                const A* t = nullptr;
                for (auto [k, v] : m)
                    t = A::insert(t, k, v);
                return t;
            };
            for (int iter = 0; iter != 200; ++iter) {
                uint64_t a = std::rand() % 64, b = std::rand() % 64;
                if (a == b) continue;
                int v = std::rand() % 1000, w = std::rand() % 1000;
                std::map<uint64_t, int> x{{a, v + 1}, {b, w}};
                std::map<uint64_t, int> y{{a, v}, {b, w + 1}};
                assert(content_hash(build(x)) != content_hash(build(y)));
                // and against the key-swapped form
                std::map<uint64_t, int> z{{a, w}, {b, v + 1}};
                if (v + 1 != w)
                    assert(content_hash(build(x)) != content_hash(build(z)));
            }
            // Two sets differing by one member differ.
            const S* s1 = S::insert(nullptr, 1, std::monostate{});
            const S* s2 = S::insert(s1, 2, std::monostate{});
            const S* s3 = S::insert(s1, 3, std::monostate{});
            assert(content_hash(s1) != content_hash(s2));
            assert(content_hash(s2) != content_hash(s3));
        }

        // Random history against the oracle at every step: the map and
        // every node of it (assert_canonical runs the shallow invariant,
        // which recomputes each node's hash from its parts).
        {
            PersistentMap<uint64_t, int, DefaultKeyService<uint64_t>, RootDiscipline> p;
            std::map<uint64_t, int> oracle;
            for (int i = 0; i != 3000; ++i) {
                uint64_t k = (i & 1) ? (std::rand() % 200) : ((uint64_t)std::rand() << 20);
                if (std::rand() % 3) {
                    int v = std::rand();
                    p.set(k, v);
                    oracle[k] = v;
                } else {
                    int victim = -1;
                    bool did_erase = p.try_erase(k, victim);
                    auto it = oracle.find(k);
                    assert(did_erase == (it != oracle.end()));
                    if (did_erase)
                        oracle.erase(it);
                }
                assert(p.hash() == oracle_hash<A>(oracle));
                if (!(i & 63))
                    A::assert_canonical(p._inner ? &*p._inner : nullptr);
                if (!(i & 255))
                    mutator_repin();
            }

            // Shape independence: the same content built by insertion in
            // another order, by the sorted builder, and by a parallel
            // rebuild from empty, all hash alike.
            std::vector<std::pair<uint64_t, int>> entries(oracle.begin(), oracle.end());
            for (size_t i = entries.size(); i > 1; --i)
                std::swap(entries[i - 1], entries[std::rand() % i]);
            const A* shuffled = nullptr;
            for (auto [k, v] : entries)
                shuffled = A::insert(shuffled, k, v);
            assert(content_hash(shuffled) == p.hash());
            const A* sorted = A::build_from_sorted(oracle.begin(), oracle.end());
            assert(content_hash(sorted) == p.hash());
            {
                auto guard = pin_global_epoch();
                std::vector<std::pair<uint64_t, int>> mods(oracle.begin(), oracle.end());
                auto combine = [](const int* old, const int& v) -> std::optional<int> {
                    (void) old;
                    return v;
                };
                const A* rebuilt = co_await A::coroutine_parallel_rebuild(
                    nullptr, std::span(std::as_const(mods)), 0, mods.size(), combine);
                assert(content_hash(rebuilt) == p.hash());
                // and a rebuild that erases half of it, against the oracle
                std::map<uint64_t, int> half = oracle;
                std::vector<std::pair<uint64_t, bool>> erasures;
                for (auto const& [k, v] : oracle)
                    if (k & 1) {
                        erasures.emplace_back(k, true);
                        half.erase(k);
                    }
                auto erase = [](const int* old, const bool&) -> std::optional<int> {
                    assert(old);
                    return std::nullopt;
                };
                const A* halved = co_await A::coroutine_parallel_rebuild(
                    sorted, std::span(std::as_const(erasures)), 0, erasures.size(), erase);
                assert(content_hash(halved) == oracle_hash<A>(half));
                A::assert_canonical(halved);
                unpin_global_epoch(guard);
            }
        }

        // Nested: a map whose values are sets hashes each value by the
        // set's own content hash, so replacing a set by an equal set built
        // another way is invisible, and changing a member is not.
        {
            using Inner = PersistentSet<uint64_t, DefaultKeyService<uint64_t>, ScanDiscipline>;
            using Outer = PersistentMap<uint64_t, Inner, DefaultKeyService<uint64_t>, ScanDiscipline>;
            Inner i1; i1.set(3); i1.set(1); i1.set(2);
            Inner i2; i2.set(1); i2.set(2); i2.set(3);
            Inner i3; i3.set(1); i3.set(2); i3.set(4);
            assert(i1.hash() == i2.hash());
            assert(i1.hash() != i3.hash());
            assert(i1.hash() == Inner::hash_of(1) + Inner::hash_of(2) + Inner::hash_of(3));
            Outer o1; o1.set(10, i1); o1.set(20, i3);
            Outer o2; o2.set(20, i3); o2.set(10, i2);
            Outer o3; o3.set(10, i3); o3.set(20, i1);
            assert(o1.hash() == o2.hash());
            assert(o1.hash() != o3.hash());
            assert(o1.hash() == Outer::AMT::entry_hash_of(10, i1) + Outer::AMT::entry_hash_of(20, i3));
        }

        co_return;
    };
    
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
                assert(p.size() == m.size());
                assert(p.hash() == oracle_hash<A>(m));
                assert(p.hash() == content_hash(ref));
            }
            for (uint64_t k = 0; k != N; ++k) {
                if (m.count(k)) {
                    int v = {};
                    bool result = p.try_get(k, v);
                    assert(result);
                    assert(v == m[k]);
                } else {
                    int v = {};
                    bool present = p.try_get(k, v);
                    assert(!present);
                    (void) present;
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
                src, std::span(std::as_const(mods)), 0, mods.size(), combine);

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
                assert(content_hash(result) == content_hash(ref));
            }
            assert((result ? result->size() : 0) == expect.size());
            assert(content_hash(result) == oracle_hash<A>(expect));

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

            auto action_for_key = [](auto&& kv) -> coroutine::Future<Action> {
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
                assert(result.hash() == content_hash(ref));
            }
            assert(result.size() == expect.size());
            assert(result.hash() == oracle_hash<PM::AMT>(expect));

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
            const A* u = erase(t, 7);
            assert(u == nullptr);
            (void) u;
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
            assert(t->size() == 2 && u->size() == 1);
            int v = 0;
            bool in_t = t->try_get(32, v);
            assert(in_t && v == 32);
            (void) in_t;
            bool in_u = u->try_get(32, v);
            assert(!in_u);
            (void) in_u;
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
            assert(t->size() == 3);
            const A* u = erase(t, far);
            A::assert_canonical(u);
            assert(A::same_shape(u, build({0, 32})));
            assert(u->size() == 2);
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
                assert(p.size() == oracle.size());
                assert(p.hash() == oracle_hash<A>(oracle));
                if (!(i & 255))
                    mutator_repin();
            }
        }

        co_return;

    };

    // The sorted builder must produce, in one pass, exactly the trie that
    // insertion builds, for maps and for sets, across key distributions that
    // stress each part of the algorithm: dense blocks (full leaves), sparse
    // keys (deep divergence, singleton leaves), clustered blocks with gaps,
    // and codes at the top of the word where the last symbol is narrower.
    // Also the loader's own use: walking a canonical trie in code order and
    // rebuilding from the walk must reproduce it.
    define_test("amt_sorted_builder") {

        using A = PersistentMap<uint64_t, int>::AMT;
        using S = ArrayMappedTrie<__uint128_t, std::monostate, ScanDiscipline>;

        auto rand64 = []() -> uint64_t {
            return ((uint64_t)std::rand() << 42) ^ ((uint64_t)std::rand() << 21) ^ (uint64_t)std::rand();
        };

        // Fisher-Yates over std::rand, so a run is reproducible from the
        // suite's seed.
        auto shuffle = [](auto& v) {
            for (size_t i = v.size(); i > 1; --i)
                std::swap(v[i - 1], v[std::rand() % i]);
        };

        // Build from the oracle's sorted contents; check shape against an
        // insertion build in random order, then content, code-ordered
        // iteration, the range convenience, and the walk-and-rebuild round
        // trip a loader performs.
        auto check_map = [&shuffle](const std::map<uint64_t, int>& oracle) {
            A::SortedBuilder b;
            for (auto [k, v] : oracle)
                b.push(k, v);
            const A* built = b.finish();
            A::assert_canonical(built);
            assert((built ? built->size() : 0) == oracle.size());

            std::vector<std::pair<uint64_t, int>> entries(oracle.begin(), oracle.end());
            shuffle(entries);
            const A* ref = nullptr;
            for (auto [k, v] : entries)
                ref = A::insert(ref, k, v);
            assert(A::same_shape(built, ref));
            assert(content_hash(built) == oracle_hash<A>(oracle));
            assert(content_hash(built) == content_hash(ref));

            for (auto [k, v] : oracle) {
                int u = 0;
                bool present = built->try_get(k, u);
                assert(present && u == v);
                (void) present;
            }

            std::vector<uint64_t> walked;
            if (built)
                built->for_each([&walked](uint64_t k, int) { walked.push_back(k); });
            assert(walked.size() == oracle.size());
            size_t i = 0;
            for (auto [k, v] : oracle) {
                assert(walked[i] == k);
                ++i;
            }

            std::vector<std::pair<uint64_t, int>> sorted(oracle.begin(), oracle.end());
            assert(A::same_shape(A::build_from_sorted(sorted.begin(), sorted.end()), built));

            A::SortedBuilder b2;
            if (built)
                built->for_each([&b2](uint64_t k, int v) { b2.push(k, v); });
            assert(A::same_shape(b2.finish(), built));
        };

        // Edges: empty, singleton, one block, sibling leaves, divergence at
        // the top symbol (4 bits wide at shift 60), the top bit alone.
        {
            A::SortedBuilder b;
            assert(b.finish() == nullptr);
            check_map({});
            check_map({{5, 50}});
            check_map({{0, 1}, {31, 2}});
            check_map({{0, 1}, {32, 2}});
            check_map({{0, 1}, {~(uint64_t)0, 2}});
            check_map({{(uint64_t)1 << 63, 1}, {((uint64_t)1 << 63) | 1, 2}});
            check_map({{0, 1}, {32, 2}, {(uint64_t)1 << 40, 3}, {((uint64_t)1 << 40) | 32, 4}});
        }

        for (int iter = 0; iter != 100; ++iter) {
            std::map<uint64_t, int> oracle;
            int n = 1 + std::rand() % 400;
            switch (iter % 4) {
                case 0: // dense small domain: full and near-full leaves
                    for (int i = 0; i != n; ++i)
                        oracle[std::rand() % 300] = std::rand();
                    break;
                case 1: // sparse: singleton leaves under deep divergence
                    for (int i = 0; i != n; ++i)
                        oracle[rand64()] = std::rand();
                    break;
                case 2: { // clustered: a few blocks, some completely full
                    int blocks = 1 + std::rand() % 8;
                    for (int j = 0; j != blocks; ++j) {
                        uint64_t base = rand64() & ~(uint64_t)31;
                        if (std::rand() & 1) {
                            for (int m = 0; m != 32; ++m)
                                oracle[base + m] = std::rand();
                        } else {
                            int members = 1 + std::rand() % 31;
                            for (int m = 0; m != members; ++m)
                                oracle[base + std::rand() % 32] = std::rand();
                        }
                    }
                    break;
                }
                case 3: // top of the word: the shift-60 symbol is 4 bits wide
                    for (int i = 0; i != n; ++i)
                        oracle[((uint64_t)(std::rand() & 15) << 60) | (uint64_t)(std::rand() % 64)] = std::rand();
                    break;
            }
            check_map(oracle);
            if (!(iter & 7))
                mutator_repin();
        }

        // A set over 128-bit codes, as the time wheel uses: (time, entity)
        // packed high and low.  Bare codes go through the set overload.
        for (int iter = 0; iter != 20; ++iter) {
            std::set<__uint128_t> oracle;
            int n = std::rand() % 300;
            for (int i = 0; i != n; ++i) {
                __uint128_t t = (__uint128_t)(std::rand() % 8);
                __uint128_t e = (__uint128_t)(std::rand() % 500);
                oracle.insert((t << 64) | e);
            }
            std::vector<__uint128_t> codes(oracle.begin(), oracle.end());
            const S* built = S::build_from_sorted(codes.begin(), codes.end());
            S::assert_canonical(built);
            assert((built ? built->size() : 0) == oracle.size());
            shuffle(codes);
            const S* ref = nullptr;
            for (__uint128_t k : codes)
                ref = S::insert(ref, k, std::monostate{});
            assert(S::same_shape(built, ref));
            {
                uint64_t sum = 0;
                for (__uint128_t k : oracle)
                    sum += S::entry_hash_of(k, std::monostate{});
                assert(content_hash(built) == sum);
                assert(content_hash(ref) == sum);
            }
            for (__uint128_t k : oracle)
                assert(built->contains(k));
            assert(!built || !built->contains(((__uint128_t)9 << 64) | 1));
            if (!(iter & 7))
                mutator_repin();
        }

        co_return;

    };
}
