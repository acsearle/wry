//
//  ctrie.cpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#include "ctrie.hpp"
#include "test.hpp"

namespace wry {

    struct KeyType {
        std::size_t data;
        std::size_t hash() const { return wry::hash(data); }
        bool operator==(KeyType const&) const = default;
    };

    void garbage_collected_scan(KeyType const&) {}

    struct ValueType {
        std::size_t data;
        bool operator==(ValueType const&) const = default;
    };

    void garbage_collected_scan(ValueType const&) {}

    // Phase 0 sanity test.  The collector and worker threads are running
    // by the time tests are dispatched, and the worker threads are
    // mutator-pinned ([global_work_queue.cpp]), so allocation-and-shade
    // operations are well-defined here.
    define_test("ctrie") {

        Root<Ctrie<KeyType, ValueType>*> trie(new Ctrie<KeyType, ValueType>());

        KeyType k0{1234567890};
        ValueType v0{2345678901};
        KeyType k1{3456789012};
        ValueType v1{4567890123};

        ValueType a = trie->find_or_emplace(k0, v0);
        ValueType b = trie->find_or_emplace(k1, v1);
        assert(a.data == v0.data);
        assert(b.data == v1.data);
        printf("[%zu] -> %zu expect %zu (hash() -> %0.16zx\n", k0.data, a.data, v0.data, k0.hash());
        printf("[%zu] -> %zu expect %zu (hash() -> %0.16zx\n", k1.data, b.data, v1.data, k1.hash());


        // Exercise more keys to grow the trie.
        std::vector<ValueType> kept;
        for (size_t i = 0; i != 64; ++i) {
            KeyType key{i};
            ValueType value{i};
            ValueType result = trie->find_or_emplace(key, value);
            printf("[%zu] -> %zu expect %zu (hash() -> %0.16zx\n", key.data, result.data, value.data, key.hash());
            assert(result == value);
            kept.emplace_back(value);
        }

        // Wait for several full collection cycles so the collector has
        // had a chance to scan the trie and run any cleanup phases that
        // depend on a full sweep.  Everything unrooted (transient
        // INodes / CNodes / SNodes left in the wake of trie growth)
        // becomes eligible for collection during this gap.
        co_await Coroutine::WaitForCollectionCycles{3};

        for (size_t i = 0; i != 64; ++i) {
            KeyType key{i};
            ValueType value{i};
            ValueType result = trie->find_or_emplace(key, value);
            printf("[%zu] -> %zu expect %zu (hash() -> %0.16zx\n", key.data, result.data, kept[i].data, key.hash());
            assert(result == kept[i]);
        }

        co_return;
    };

} // namespace wry
