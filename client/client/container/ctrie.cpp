//
//  ctrie.cpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#include <span>
#include <random>
#include <algorithm>
#include <unordered_set>
#include <cstdint>
#include <cstdlib>

#include "ctrie.hpp"
#include "test.hpp"

namespace wry {

    struct KeyType {

        inline static std::size_t _mask;

        std::size_t data;
        bool operator==(KeyType const&) const = default;
    };

    std::size_t hash(KeyType k) { return wry::hash(k.data) & KeyType::_mask; }


    void garbage_collected_scan(KeyType const&) {}

    struct ValueType {
        std::size_t data;
        bool operator==(ValueType const&) const = default;
    };

    void garbage_collected_scan(ValueType const&) {}

    // Sanity test.  The collector and worker threads are running
    // by the time tests are dispatched, and the worker threads are
    // mutator-pinned ([global_work_queue.cpp]), so allocation-and-shade
    // operations are well-defined here.
    define_test("ctrie") {
        // TODO: We need to report seed for reporducibility (and ability specify it)
        std::random_device rd;
        auto seed = rd();
        printf("SEED = %u\n", seed);
        std::mt19937_64 gen{seed};
        KeyType::_mask = gen() & gen();

        printf("MASK %0.16zx (%d)\n", KeyType::_mask, __builtin_popcountll(KeyType::_mask));

        using T = std::tuple<KeyType, int, ValueType>;

        std::vector<T> data;
        for (int i = 0; i != 1 << 17; ++i) {
            data.emplace_back(gen(), gen() & 1, gen());
        }

        {
            std::vector<std::size_t> hashes;
            for (auto [k, f, _] : data) {
                if (f) {
                    hashes.push_back(hash(k));
                }
            }
            std::sort(hashes.begin(), hashes.end());
            auto p = std::unique(hashes.begin(), hashes.end());
            auto n = p - hashes.begin();
            printf("UNIQUE HASHES = %zd / %zd\n", n, hashes.size());
        }

        Root<Ctrie<KeyType, ValueType>*> trie(new Ctrie<KeyType, ValueType>());
        using AlterChoice = Ctrie<KeyType, ValueType>::AlterChoice;

        Coroutine::Nursery nursery;

        printf("FORK\n");
        int m = 1 << 9;
        for (int i = 0; i != 1 << 17; i += m) {
            std::span s{data.data() + i, data.data() + i + m};
            co_await nursery.fork([](auto trie, std::span<T> s) -> Coroutine::Future<> {
                for (auto& [k, f, v] : s) {
                    if (f) {
                        // auto w = trie->find_or_emplace(k, v);
                        auto [w, x] = trie->alter(k, [&](std::optional<ValueType> const& in) -> AlterChoice {
                            if (in) {
                                return AlterChoice { AlterChoice::KEEP, {} };
                            } else {
                                return AlterChoice { AlterChoice::REPLACE, v };
                            }
                        });
                        if (w) {
                            f = false;
                        } else {
                            assert(*x == v);
                        }
                    }
                }
                co_return;
            }(trie, s));
        }

        co_await nursery.join();
        printf("JOIN\n");

        co_await Coroutine::WaitForCollectionCycles{1};

        printf("FORK2\n");
        for (int i = 0; i != 1 << 17; i += m) {
            std::span s{data.data() + i, data.data() + i + m};
            co_await nursery.fork([](auto trie, std::span<T> s) -> Coroutine::Future<> {
                for (auto [k, f, v] : s) {
                    auto w = trie->find(k);
                    assert(w.has_value() == f);
                    if (f) {
                        // printf("[%zu] -> %zu expect %zu (hash() -> %0.16zx\n", k.data, w->data, v.data, k.hash());
                        assert(*w == v);
                    }
                }
                co_return;
            }(trie, s));
        }

        co_await nursery.join();
        printf("JOIN2\n");

        printf("FORK3\n");
        for (int i = 0; i != 1 << 17; i += m) {
            std::span s{data.data() + i, data.data() + i + m};
            co_await nursery.fork([](auto trie, std::span<T> s) -> Coroutine::Future<> {
                for (auto [k, f, v] : s) {
                    // trie->erase(k);
                    auto [w, x] = trie->alter(k, [](std::optional<ValueType> const& in) {
                        return AlterChoice { AlterChoice::ERASE, {} };
                    });
                    assert((bool)w.has_value() == f);
                    if (f) {
                        assert(*w == v);
                    }
                }
                co_return;
            }(trie, s));
        }

        co_await nursery.join();
        printf("JOIN3\n");

        co_await Coroutine::WaitForCollectionCycles{1};

        printf("ERASED\n");
        trie = nullptr;

        co_await Coroutine::WaitForCollectionCycles{1};


        co_return;
    };


    // ── Commutative +1/-1 stress test ────────────────────────────────────────
    //
    // Per-key value is a signed integer encoded into V::data, with the
    // convention that count=0 means "absent".  Each key gets a target final
    // count C, plus extra (+1, -1) pairs that cancel.  All operations are
    // shuffled into a single global pool, fanned out across coroutines, and
    // applied via `alter`.
    //
    // The key property is commutativity: regardless of interleaving order,
    // the per-key sum of deltas is deterministic, so the post-condition is
    // checkable: for each key, find(k) must return Some(C) when C != 0, and
    // None when C == 0.  This catches lost updates, spurious-success CASes,
    // zombie tombs, and cross-key contamination.
    //
    // Uses its own local key/value types to avoid racing on the static mask
    // owned by the previous test (which can run concurrently).

    namespace {

        struct CommKey {
            inline static std::size_t _mask;
            std::size_t data;
            bool operator==(CommKey const&) const = default;
        };
        std::size_t hash(CommKey k) { return wry::hash(k.data) & CommKey::_mask; }

        struct CommVal {
            std::size_t data;
            bool operator==(CommVal const&) const = default;
        };

        // ADL hooks for GC scan of the placeholder K/V types (both POD).
        void garbage_collected_scan(CommKey const&) {}
        void garbage_collected_scan(CommVal const&) {}

    } // anonymous namespace

    define_test("ctrie_commutative") {

        std::random_device rd;
        auto seed = rd();
        printf("[ctrie_commutative] SEED=%u\n", seed);
        std::mt19937_64 gen{seed};

        // Aggressive mask: AND of three random words, expected popcount 8,
        // so ~256 hash buckets — guarantees substantial LNode coverage at
        // 8K keys.
        CommKey::_mask = gen() & gen() & gen();
        printf("[ctrie_commutative] MASK=%016zx (%d bits)\n",
               CommKey::_mask, __builtin_popcountll(CommKey::_mask));

        constexpr size_t N         = 1 << 13; // 8192 distinct keys
        constexpr int   target_max = 4;       // per-key target in [-4, +4]
        constexpr int   padding    = 4;       // extra (+1,-1) cancelling pairs

        // Generate N unique keys with random target counts.
        struct Entry { CommKey key; int target; };
        std::vector<Entry> entries;
        entries.reserve(N);
        {
            std::unordered_set<std::size_t> seen;
            std::uniform_int_distribution<int> tdist(-target_max, target_max);
            while (entries.size() < N) {
                std::size_t d = gen();
                if (!seen.insert(d).second) continue;
                entries.push_back({CommKey{d}, tdist(gen)});
            }
        }

        // Build the global op list: |target| ops of sign(target) plus
        // `padding` cancelling pairs per key.
        std::vector<std::pair<CommKey, int>> ops;
        {
            size_t total = 0;
            for (auto const& e : entries)
                total += std::abs(e.target) + 2 * padding;
            ops.reserve(total);
        }
        for (auto const& e : entries) {
            int num_pos = padding + (e.target > 0 ?  e.target : 0);
            int num_neg = padding + (e.target < 0 ? -e.target : 0);
            for (int j = 0; j < num_pos; ++j) ops.emplace_back(e.key, +1);
            for (int j = 0; j < num_neg; ++j) ops.emplace_back(e.key, -1);
        }
        std::shuffle(ops.begin(), ops.end(), gen);
        printf("[ctrie_commutative] %zu keys, %zu ops\n",
               entries.size(), ops.size());

        // Run.
        Root<Ctrie<CommKey, CommVal>*> trie(new Ctrie<CommKey, CommVal>());
        using AlterChoice = Ctrie<CommKey, CommVal>::AlterChoice;
        Coroutine::Nursery nursery;

        constexpr size_t batch = 1 << 9; // 512 ops per fork
        printf("[ctrie_commutative] FORK\n");
        for (size_t i = 0; i < ops.size(); i += batch) {
            size_t hi = std::min(i + batch, ops.size());
            std::span s{ops.data() + i, ops.data() + hi};
            co_await nursery.fork(
                [](auto trie, std::span<std::pair<CommKey, int>> s)
                -> Coroutine::Future<> {
                    for (auto [k, delta] : s) {
                        // alter ignores the returned old value -- we only
                        // care about the final state, which we'll check
                        // after the join.
                        trie->alter(k, [delta](std::optional<CommVal> const& in) -> AlterChoice {
                                std::int64_t n =
                                    (in ? (std::int64_t)in->data : 0) + delta;
                            if (n == 0) return AlterChoice { AlterChoice::ERASE, {} };
                            return AlterChoice { AlterChoice::REPLACE, CommVal{(std::size_t)n} };
                            });
                    }
                    co_return;
                }(trie, s));
        }
        co_await nursery.join();
        printf("[ctrie_commutative] JOIN\n");

        co_await Coroutine::WaitForCollectionCycles{1};

        // Verify: for each key, the final state must match its target count
        // exactly, with the convention that count==0 means absent.
        size_t mismatches = 0;
        size_t expected_present = 0, expected_absent = 0;
        size_t got_present = 0,      got_absent = 0;
        for (auto const& e : entries) {
            auto found = trie->find(e.key);
            if (e.target == 0) {
                ++expected_absent;
                if (found.has_value()) {
                    if (mismatches < 16)
                        printf("  MISMATCH key=0x%zx target=0 got=%zu\n",
                               e.key.data, found->data);
                    ++mismatches;
                    ++got_present;
                } else {
                    ++got_absent;
                }
            } else {
                ++expected_present;
                if (!found.has_value()) {
                    if (mismatches < 16)
                        printf("  MISMATCH key=0x%zx target=%d got=absent\n",
                               e.key.data, e.target);
                    ++mismatches;
                    ++got_absent;
                } else {
                    ++got_present;
                    auto got = (std::int64_t)found->data;
                    if (got != e.target) {
                        if (mismatches < 16)
                            printf("  MISMATCH key=0x%zx target=%d got=%lld\n",
                                   e.key.data, e.target, (long long)got);
                        ++mismatches;
                    }
                }
            }
        }
        printf("[ctrie_commutative] mismatches=%zu "
               "(present: %zu/%zu, absent: %zu/%zu)\n",
               mismatches,
               got_present, expected_present,
               got_absent,  expected_absent);
        assert(mismatches == 0);

        trie = nullptr;
        co_await Coroutine::WaitForCollectionCycles{1};

        co_return;
    };

} // namespace wry
