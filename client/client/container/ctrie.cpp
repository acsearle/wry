//
//  ctrie.cpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#include <span>
#include <random>

#include "ctrie.hpp"
#include "test.hpp"

namespace wry {

    struct KeyType {

        inline static std::size_t _mask;

        std::size_t data;
        std::size_t hash() const { return wry::hash(data) & _mask; }
        bool operator==(KeyType const&) const = default;
    };

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
                    hashes.push_back(k.hash());
                }
            }
            std::sort(hashes.begin(), hashes.end());
            auto p = std::unique(hashes.begin(), hashes.end());
            auto n = p - hashes.begin();
            printf("UNIQUE HASHES = %zd / %zd\n", n, hashes.size());
        }

        Root<Ctrie<KeyType, ValueType>*> trie(new Ctrie<KeyType, ValueType>());

        Coroutine::Nursery nursery;

        printf("FORK\n");
        int m = 1 << 9;
        for (int i = 0; i != 1 << 17; i += m) {
            std::span s{data.data() + i, data.data() + i + m};
            co_await nursery.fork([](auto trie, std::span<T> s) -> Coroutine::Future<> {
                for (auto [k, f, v] : s) {
                    if (f) {
                        auto w = trie->find_or_emplace(k, v);
                        // printf("[%zu] -> %zu expect %zu (hash() -> %0.16zx\n", k.data, w.data, v.data, k.hash());
                        // TODO: occasional collisions to be expected
                        assert(w == v);
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
                    trie->erase(k);
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

} // namespace wry
