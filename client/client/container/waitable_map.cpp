//
//  waitable_map.cpp
//  client
//
//  Created by Antony Searle on 8/8/2025.
//

#include <cstdlib>
#include <map>
#include <set>
#include <vector>

#include "waitable_map.hpp"
#include "test.hpp"

namespace wry {

    // Stage-0 vs Stage-1 differential test for the WaitableMap rebuild that
    // World::step() drives.  Both the dense kv value map and the nested sparse
    // ki waiter index are now rebuilt in parallel; each must match the serial
    // rebuild and a std::map/std::set oracle.
    define_test("waitablemap_parallel_rebuild") {

        using WM = WaitableMap<uint64_t, int>;
        using ValAction = ParallelRebuildAction<int>;
        using KiAction = ParallelRebuildAction<std::vector<EntityID>>;
        const uint64_t key_domain = 200;

        auto ki_set = [](const WM& wm, uint64_t k) -> std::set<uint64_t> {
            std::set<uint64_t> s;
            WaitSet ws;
            if (wm.ki.try_get(k, ws))
                ws.for_each([&s](EntityID e) { s.insert(e.data); });
            return s;
        };

        for (int iter = 0; iter != 150; ++iter) {

            std::map<uint64_t, int> kv_oracle;
            std::map<uint64_t, std::set<uint64_t>> ki_oracle;
            WM source;

            int ns = std::rand() % 80;
            for (int s = 0; s != ns; ++s) {
                uint64_t k = std::rand() % key_domain;
                int v = std::rand();
                source.kv.set(k, v);
                kv_oracle[k] = v;
            }
            // Non-empty source ki: random waiters on some keys (so CLEAR/MERGE/
            // WRITE exercise replace/erase/union of existing waitsets).
            int nki = std::rand() % 40;
            for (int s = 0; s != nki; ++s) {
                uint64_t k = std::rand() % key_domain;
                uint64_t e = (uint64_t)std::rand();
                WaitSet ws;
                (void) source.ki.try_get(k, ws);
                ws.set(EntityID{e});
                source.ki.set(k, ws);
                ki_oracle[k].insert(e);
            }

            // Deterministic per-key bundled (kv, ki) action.
            ConcurrentMap<uint64_t, int, DefaultKeyService<uint64_t>, EpochDiscipline> modifier;
            std::map<uint64_t, std::pair<ValAction, KiAction>> acts;
            int nm = std::rand() % 40;
            for (int m = 0; m != nm; ++m) {
                uint64_t k = std::rand() % key_domain;
                int r = std::rand() % 3;
                ValAction va = (r == 0) ? ValAction{ValAction::WRITE_VALUE, std::rand()}
                             : (r == 1) ? ValAction{ValAction::CLEAR_VALUE, 0}
                                        : ValAction{ValAction::NONE, 0};
                // ki: WRITE (replace) / CLEAR (erase) / MERGE (union) / NONE.
                // WRITE and MERGE always carry >=1 entity, matching world.cpp
                // (a writer-waiter or a non-empty waiter batch); CLEAR is empty.
                int q = std::rand() % 4;
                std::vector<EntityID> ws;
                if (q == 0 || q == 2) {
                    int n = 1 + std::rand() % 3;
                    for (int i = 0; i != n; ++i)
                        ws.push_back(EntityID{(uint64_t)std::rand()});
                }
                KiAction ka = (q == 0) ? KiAction{KiAction::WRITE_VALUE, ws}
                            : (q == 1) ? KiAction{KiAction::CLEAR_VALUE, {}}
                            : (q == 2) ? KiAction{KiAction::MERGE_VALUE, ws}
                                       : KiAction{KiAction::NONE, {}};
                acts[k] = {va, ka};
                modifier.try_emplace(k, 0);
            }

            // Oracles.
            std::map<uint64_t, int> kv_expect = kv_oracle;
            std::map<uint64_t, std::set<uint64_t>> ki_expect = ki_oracle;
            for (auto& [k, p] : acts) {
                if (p.first.tag == ValAction::WRITE_VALUE)
                    kv_expect[k] = p.first.value;
                else if (p.first.tag == ValAction::CLEAR_VALUE)
                    kv_expect.erase(k);
                switch (p.second.tag) {
                    case KiAction::WRITE_VALUE: {
                        std::set<uint64_t> s;
                        for (EntityID e : p.second.value)
                            s.insert(e.data);
                        ki_expect[k] = s;
                        break;
                    }
                    case KiAction::CLEAR_VALUE:
                        ki_expect.erase(k);
                        break;
                    case KiAction::MERGE_VALUE:
                        for (EntityID e : p.second.value)
                            ki_expect[k].insert(e.data);
                        break;
                    case KiAction::NONE:
                        break;
                }
            }

            auto action_for_key = [&acts](auto&& kv)
                -> Coroutine::Future<std::pair<ValAction, KiAction>> {
                co_return acts[kv.first];
            };

            WM parallel = co_await coroutine_parallel_rebuild2(source, modifier, action_for_key);
            WM serial   = co_await coroutine_parallel_rebuild2_serial(source, modifier, action_for_key);

            for (uint64_t k = 0; k != key_domain; ++k) {
                // kv: parallel == serial == oracle
                int pv = 0; bool ph = parallel.kv.try_get(k, pv);
                int sv = 0; bool sh = serial.kv.try_get(k, sv);
                auto it = kv_expect.find(k);
                assert(ph == (it != kv_expect.end()));
                assert(ph == sh && (!ph || pv == sv));
                if (ph)
                    assert(pv == it->second);

                // ki: parallel == serial == oracle
                std::set<uint64_t> ps = ki_set(parallel, k);
                std::set<uint64_t> ss = ki_set(serial, k);
                auto kit = ki_expect.find(k);
                std::set<uint64_t> es = (kit != ki_expect.end()) ? kit->second
                                                                 : std::set<uint64_t>{};
                assert(ps == ss);
                assert(ps == es);
            }

            if (!(iter & 7))
                mutator_repin();
        }

        co_return;

    };

} // namespace wry
