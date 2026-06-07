//
//  waitable_map.cpp
//  client
//
//  Created by Antony Searle on 8/8/2025.
//

#include <cstdlib>
#include <map>
#include <vector>

#include "waitable_map.hpp"
#include "test.hpp"

namespace wry {

    // Stage-0 vs Stage-1 differential test for the WaitableMap rebuild that
    // World::step() drives: the parallel kv rebuild must match both the serial
    // rebuild and a std::map oracle.  The ki (waiter-index) path is identical
    // code in both versions, so it is exercised but not separately diffed.
    define_test("waitablemap_parallel_rebuild") {

        using WM = WaitableMap<uint64_t, int>;
        using ValAction = ParallelRebuildAction<int>;
        using KiAction = ParallelRebuildAction<std::vector<EntityID>>;
        const uint64_t key_domain = 200;

        for (int iter = 0; iter != 150; ++iter) {

            std::map<uint64_t, int> oracle;
            WM source;
            int ns = std::rand() % 80;
            for (int s = 0; s != ns; ++s) {
                uint64_t k = std::rand() % key_domain;
                int v = std::rand();
                source.kv.set(k, v);
                oracle[k] = v;
            }

            // A real skiplist modifier, plus a deterministic per-key action map
            // so both rebuilds observe identical actions.
            ConcurrentMap<uint64_t, int, DefaultKeyService<uint64_t>, EpochDiscipline> modifier;
            std::map<uint64_t, std::pair<ValAction, KiAction>> acts;
            int nm = std::rand() % 40;
            for (int m = 0; m != nm; ++m) {
                uint64_t k = std::rand() % key_domain;
                int r = std::rand() % 3;
                ValAction va = (r == 0) ? ValAction{ValAction::WRITE_VALUE, std::rand()}
                             : (r == 1) ? ValAction{ValAction::CLEAR_VALUE, 0}
                                        : ValAction{ValAction::NONE, 0};
                // exercise the ki path: a single-entity MERGE, or NONE
                KiAction ka = (std::rand() & 1)
                    ? KiAction{KiAction::MERGE_VALUE,
                               std::vector<EntityID>{ EntityID{(uint64_t)std::rand()} }}
                    : KiAction{KiAction::NONE, {}};
                acts[k] = {va, ka};
                modifier.try_emplace(k, 0);
            }

            std::map<uint64_t, int> expect = oracle;
            for (auto& [k, p] : acts) {
                if (p.first.tag == ValAction::WRITE_VALUE)
                    expect[k] = p.first.value;
                else if (p.first.tag == ValAction::CLEAR_VALUE)
                    expect.erase(k);
            }

            auto action_for_key = [&acts](auto&& kv)
                -> Coroutine::Future<std::pair<ValAction, KiAction>> {
                co_return acts[kv.first];
            };

            WM parallel = co_await coroutine_parallel_rebuild2(source, modifier, action_for_key);
            WM serial   = co_await coroutine_parallel_rebuild2_serial(source, modifier, action_for_key);

            for (uint64_t k = 0; k != key_domain; ++k) {
                int pv = 0; bool ph = parallel.kv.try_get(k, pv);
                int sv = 0; bool sh = serial.kv.try_get(k, sv);
                auto it = expect.find(k);
                assert(ph == (it != expect.end()));      // parallel == oracle (presence)
                assert(ph == sh && (!ph || pv == sv));    // parallel == serial
                if (ph)
                    assert(pv == it->second);             // parallel == oracle (value)
            }

            if (!(iter & 7))
                mutator_repin();
        }

        co_return;

    };

} // namespace wry
