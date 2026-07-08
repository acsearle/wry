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

        // Root pin for the whole work tree: this was the test that hit the
        // slab-rotation use-after-poison under the drain-loop repin cadence
        // (see global_work_queue.cpp).
        auto guard = pin_global_epoch();

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

            // WM parallel = co_await coroutine_parallel_rebuild2(source, modifier, action_for_key);
            // WM serial   = co_await coroutine_parallel_rebuild2_serial(source, modifier, action_for_key);
            WM unified  = co_await coroutine_parallel_rebuild2_unified(source, modifier, action_for_key);

            for (uint64_t k = 0; k != key_domain; ++k) {
                // kv: parallel == serial == unified == oracle
                // int pv = 0; bool ph = parallel.kv.try_get(k, pv);
                // int sv = 0; bool sh = serial.kv.try_get(k, sv);
                int uv = 0; bool uh = unified.kv.try_get(k, uv);
                auto it = kv_expect.find(k);
                assert(uh == (it != kv_expect.end()));
                // assert(ph == sh && (!ph || pv == sv));
                // assert(ph == uh && (!ph || pv == uv));
                if (uh)
                    assert(uv == it->second);

                // ki: parallel == serial == unified == oracle
                // std::set<uint64_t> ps = ki_set(parallel, k);
                // std::set<uint64_t> ss = ki_set(serial, k);
                std::set<uint64_t> us = ki_set(unified, k);
                auto kit = ki_expect.find(k);
                std::set<uint64_t> es = (kit != ki_expect.end()) ? kit->second
                                                                 : std::set<uint64_t>{};
                // assert(ps == ss);
                // assert(ps == es);
                assert(us == es);
            }

            if (!(iter & 7))
                mutator_repin();
        }

        unpin_global_epoch(guard);
        co_return;

    };

} // namespace wry






#if 0
// Apply one ki (waiter-index) action to the nested map, in place.  WRITE
// replaces a key's waitset, CLEAR erases it, MERGE is the read-modify-write
// upsert (union the new waiters into the existing set).  Serial for now (the
// ki map is sparse); the same WRITE/CLEAR/MERGE semantics will move into a
// combine when ki is parallelized.
template<typename Key>
void apply_ki_action(PersistentMap<Key, WaitSet, DefaultKeyService<Key>, ScanDiscipline>& ki,
                     Key key,
                     const ParallelRebuildAction<std::vector<EntityID>>& action) {
    using A = ParallelRebuildAction<std::vector<EntityID>>;
    switch (action.tag) {
        case A::NONE:
            break;
        case A::WRITE_VALUE: {
            WaitSet ws;
            for (EntityID e : action.value)
                ws.set(e);
            ki.set(key, ws);
            break;
        }
        case A::CLEAR_VALUE: {
            assert(action.value.empty());
            WaitSet victim;
            (void) ki.try_erase(key, victim);
            break;
        }
        case A::MERGE_VALUE: {
            WaitSet ws;
            (void) ki.try_get(key, ws); // empty if absent
            for (EntityID e : action.value)
                ws.set(e);
            ki.set(key, ws);
            break;
        }
    }
}


// Stage 0 (serial) -- kept as the oracle for the differential test.
template<typename Key, typename T, typename U, typename F, typename S2, typename D2>
Coroutine::Future<WaitableMap<Key, T>>
coroutine_parallel_rebuild2_serial(const WaitableMap<Key, T>& source,
                                   const ConcurrentMap<Key, U, S2, D2>& modifier,
                                   F&& action_for_key) {
    WaitableMap<Key, T> result{source};
    // SAFETY: We can iterate the concurrent map here because it is
    // immutable in this phase
    auto first = modifier.begin();
    auto last = modifier.end();
    for (; first != last; ++first) {
        std::pair<ParallelRebuildAction<T>, ParallelRebuildAction<std::vector<EntityID>>> p;
        p = co_await action_for_key(*first);
        switch (p.first.tag) {
            case ParallelRebuildAction<T>::NONE:
                break;
            case ParallelRebuildAction<T>::WRITE_VALUE:
                result.kv.set(first->first, p.first.value);
                break;
            case ParallelRebuildAction<T>::CLEAR_VALUE:
                (void) result.kv.try_erase(first->first, p.first.value);
                break;
            case ParallelRebuildAction<T>::MERGE_VALUE:
                abort();
        }
        apply_ki_action(result.ki, first->first, p.second);
    }
    co_return result;
}

// The dense kv value map and the (now nested, sparse) ki waiter index are
// both rebuilt in parallel via the AMT co-recursion, forked concurrently.
// One serial pass splits each key's bundled action into a kv value action and
// a ki waiter action (NONE dropped to keep subtree sharing); both vectors are
// code-ordered because the modifier is.  World::step() calls this; the name
// is unchanged, so its call sites are not.
template<typename Key, typename T, typename U, typename F, typename S2, typename D2>
Coroutine::Future<WaitableMap<Key, T>>
coroutine_parallel_rebuild2(const WaitableMap<Key, T>& source,
                            const ConcurrentMap<Key, U, S2, D2>& modifier,
                            F&& action_for_key) {
    WaitableMap<Key, T> result{source};
    using Code = typename DefaultKeyService<Key>::code_type;
    std::vector<std::pair<Code, ParallelRebuildAction<T>>> kv_mods;
    std::vector<std::pair<Code, ParallelRebuildAction<std::vector<EntityID>>>> ki_mods;
    for (auto first = modifier.begin(); first != modifier.end(); ++first) {
        std::pair<ParallelRebuildAction<T>, ParallelRebuildAction<std::vector<EntityID>>> p
        = co_await action_for_key(*first);
        Code code = DefaultKeyService<Key>{}.encode(first->first);
        if (p.first.tag != ParallelRebuildAction<T>::NONE)
            kv_mods.emplace_back(code, std::move(p.first));
        if (p.second.tag != ParallelRebuildAction<std::vector<EntityID>>::NONE)
            ki_mods.emplace_back(code, std::move(p.second));
    }
    Coroutine::Nursery nursery;
    co_await nursery.fork(result.kv,
                          coroutine_parallel_rebuild_from_mods(source.kv, kv_mods,
                                                               ParallelRebuildValueCombine<T>{}));
    co_await nursery.fork(result.ki,
                          coroutine_parallel_rebuild_from_mods(source.ki, ki_mods,
                                                               WaitSetMergeCombine{}));
    co_await nursery.join();
    co_return result;
}


#endif //0
