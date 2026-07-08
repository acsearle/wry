//
//  concurrent_skiplist.cpp
//  client
//
//  Created by Antony Searle on 23/11/2024.
//

#include <map>
#include <set>

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
        }
        
        co_return;

    };

    // Frozen-cursor frame partition: recurse to leaves via skiplist_partition_frame
    // and assert the collected keys equal the brute-force set -- i.e. the per-child
    // cursors partition the frame correctly (nothing lost, duplicated, or misplaced).
    namespace {
        using PartSet = ConcurrentSkiplistSet<uint64_t, DefaultKeyService<uint64_t>, EpochDiscipline>;
        using PartCur = PartSet::FrozenCursor;

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
                    c = c.right();
                uint64_t hi = lo + 32;
                while (part_codeof(c) < hi) {
                    out.insert(part_codeof(c));
                    c = c.right();
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
            collect_via_partition(a.make_cursor(), 0, 15, 32, got);
            assert(got == b);
            if (!(iter & 31))
                mutator_repin();
        }
        unpin_global_epoch(guard);
        co_return;
    };

} // namespace wry
