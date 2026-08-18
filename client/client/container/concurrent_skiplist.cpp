//
//  concurrent_skiplist.cpp
//  client
//
//  Created by Antony Searle on 23/11/2024.
//

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "concurrent_skiplist.hpp"

#include "test.hpp"

namespace wry {
    
    using Coroutine::Task;

    define_test("skiplist") {
                   
        {
            ConcurrentSkiplistSet<int, DefaultKeyService<int>, EpochDiscipline> a;
            std::set<int> b;
            
            int N = 1 << 7;
            for (int i = 0; i != N; ++i) {
                int j = rand() & (N - 1);
                a.try_emplace(j);
                b.emplace(j);
                for (int k = 0; k != N; ++k) {
                    auto c = a.find(k);
                    auto d = b.find(k);
                    assert((c == a.end()) == (d == b.end()));
                    if (c != a.end()) {
                        assert(*c == *d);
                    }
                    auto cl = a.lower_bound(k);
                    auto dl = b.lower_bound(k);
                    assert((cl == a.end()) == (dl == b.end()));
                    if (cl != a.end()) {
                        assert(*cl == *dl);
                    }
                }
            }

            // Frozen view of the same structure: every answer through
            // nonatomic loads must match the live view and the oracle,
            // iteration is in order, and the left-child/right-sibling
            // decomposition (for_each_child) reaches every node exactly once.
            {
                auto f = freeze(a);
                assert(f.is_empty() == b.empty());
                for (int k = 0; k != N; ++k) {
                    auto c = f.find(k);
                    auto d = b.find(k);
                    assert((c == f.end()) == (d == b.end()));
                    if (c != f.end())
                        assert(*c == *d);
                    auto cl = f.lower_bound(k);
                    auto dl = b.lower_bound(k);
                    assert((cl == f.end()) == (dl == b.end()));
                    if (cl != f.end())
                        assert(*cl == *dl);
                }
                std::vector<int> iterated;
                for (int x : f)
                    iterated.push_back(x);
                assert(std::equal(iterated.begin(), iterated.end(), b.begin(), b.end()));

                // find(query, visit): the entered nodes are a strictly
                // increasing chain of keys below the query, ending at the
                // match when there is one.
                for (int k = 0; k != N; ++k) {
                    std::vector<int> entered;
                    auto c = f.find(k, [&entered](int const& key) { entered.push_back(key); });
                    for (size_t i = 1; i < entered.size(); ++i)
                        assert(entered[i - 1] < entered[i]);
                    for (int key : entered)
                        assert(key <= k);
                    assert((c != f.end()) == (!entered.empty() && entered.back() == k));
                }

                using Node = decltype(f)::Node;
                std::vector<int> visited;
                auto collect = [&visited](auto const& self, Node const* node, Node const* bound) -> void {
                    node->for_each_child(bound, [&](Node const* child, Node const* child_bound) {
                        visited.push_back(child->_key);
                        self(self, child, child_bound);
                    });
                };
                collect(collect, f._head, nullptr);
                std::sort(visited.begin(), visited.end());
                assert(std::equal(visited.begin(), visited.end(), b.begin(), b.end()));
            }
        }

        {

            ConcurrentSkiplistMap<int, int, DefaultKeyService<int>, EpochDiscipline> a;
            std::map<int, int> b;

            int N = 1 << 7;
            for (int i = 0; i != N; ++i) {
                int j = rand() & (N - 1);
                int v = rand() & (N - 1);
                a.try_emplace(j, v);
                b.try_emplace(j, v);
                for (int k = 0; k != N; ++k) {
                    auto c = a.find(k);
                    auto d = b.find(k);
                    assert((c == a.end()) == (d == b.end()));
                    if (c != a.end()) {
                        assert(c->first == d->first);
                        assert(c->second == d->second);
                        // printf("[%d] = %d\n", c->first, c->second);
                    }
                    auto cl = a.lower_bound(k);
                    auto dl = b.lower_bound(k);
                    assert((cl == a.end()) == (dl == b.end()));
                    if (cl != a.end()) {
                        assert(cl->first == dl->first);
                    }
                }
            }

            // Frozen map: same checks; also the map-alias emplace
            // convention (key only => value-initialized mapped value).
            {
                auto f = freeze(a);
                assert(f.is_empty() == b.empty());
                for (int k = 0; k != N; ++k) {
                    auto c = f.find(k);
                    auto d = b.find(k);
                    assert((c == f.end()) == (d == b.end()));
                    if (c != f.end()) {
                        assert(c->first == d->first);
                        assert(c->second == d->second);
                    }
                    auto cl = f.lower_bound(k);
                    auto dl = b.lower_bound(k);
                    assert((cl == f.end()) == (dl == b.end()));
                    if (cl != f.end())
                        assert(cl->first == dl->first);
                }
                size_t count = 0;
                auto it = f.begin();
                for (auto const& [k, v] : b) {
                    assert(it != f.end());
                    assert(it->first == k && it->second == v);
                    ++it;
                    ++count;
                }
                assert(it == f.end());
                assert(count == b.size());
            }
            {
                ConcurrentSkiplistMap<int, int, DefaultKeyService<int>, EpochDiscipline> d;
                auto [it, inserted] = d.try_emplace(7);
                assert(inserted && it->first == 7 && it->second == 0);
                auto [it2, inserted2] = d.try_emplace(7, 99);
                assert(!inserted2 && it2 == it && it2->second == 0);
            }
        }

        co_return;

    };

    // Frozen-cursor frame partition: recurse to leaves via skiplist_partition_frame
    // and assert the collected keys equal the brute-force set -- i.e. the per-child
    // cursors partition the frame correctly (nothing lost, duplicated, or misplaced).
    namespace {
        using PartSet = ConcurrentSkiplistSet<uint64_t, DefaultKeyService<uint64_t>, EpochDiscipline>;
        using PartCur = FrozenSkiplistSet<uint64_t, DefaultKeyService<uint64_t>, EpochDiscipline>::FrozenCursor;

        uint64_t part_codeof(const PartCur& c) {
            auto* k = c.key();
            return k ? *k : ~(uint64_t)0;
        }

        void collect_via_partition(PartCur cursor, uint64_t lo, int shift, int n_slots,
                                   std::set<uint64_t>& out) {
            if (shift == 0) {
                // leaf covers 32 keys [lo, lo+32): descend to level 0 and walk
                PartCur c = cursor;
                while (!c.bottom())
                    c = c.down();
                while (part_codeof(c) < lo)
                    c = c.succ();
                uint64_t hi = lo + 32;
                while (part_codeof(c) < hi) {
                    out.insert(part_codeof(c));
                    c = c.succ();
                }
                return;
            }
            std::optional<PartCur> result[32] = {};
            skiplist_partition_frame(cursor, lo, shift, n_slots, result,
                                     [](uint64_t key) { return key; });
            for (int ci = 0; ci < n_slots; ++ci)
                if (result[ci])
                    collect_via_partition(*result[ci], lo + ((uint64_t)ci << shift),
                                          shift - 5, 32, out);
        }
    } // anonymous namespace

    define_test("skiplist_partition") {
        auto guard = pin_global_epoch();
        for (int iter = 0; iter != 200; ++iter) {
            PartSet a;
            std::set<uint64_t> b;
            int N = std::rand() % 200;
            for (int i = 0; i != N; ++i) {
                uint64_t k = std::rand() & ((1u << 20) - 1); // keys in [0, 2^20)
                a.try_emplace(k);
                b.insert(k);
            }
            std::set<uint64_t> got;
            // top frame [0, 2^20): shift 15, 32 slots (32 * 2^15 = 2^20)
            collect_via_partition(freeze(a).make_cursor(), 0, 15, 32, got);
            assert(got == b);
            if (!(iter & 31))
                mutator_repin();
        }
        unpin_global_epoch(guard);
        co_return;
    };

} // namespace wry
