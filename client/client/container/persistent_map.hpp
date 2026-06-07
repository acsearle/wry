//
//  persistent_map.hpp
//  client
//
//  Created by Antony Searle on 24/11/2024.
//

#ifndef persistent_map_hpp
#define persistent_map_hpp

#include <iostream>
#include <map>
#include <mutex>
#include <set>

#include "array_mapped_trie.hpp"
#include "concurrent_map.hpp"
#include "utility.hpp"
#include "coroutine.hpp"
#include "hash.hpp"

namespace wry {

    // PersistentMap provides key-value mapping backed by an array_mapped_trie
    //
    // Requires the key be mapped to a 64-bit index.  Optimal for blocks of
    // contiguous indices.  Explicitly not a standard hash; must be perfect;
    // should concentrate entropy at lsbs.
    //
    // It is efficient to look up the subtrees bounding index ranges.
    // For coordinates represented in Z-order, the data structure operates
    // somewhat like a quadtree.

    // TODO: index range lookup

    // TODO: bottom-up rebuild

    // TODO: move disciplines out to associated headers
    // TODO: rationalize policy types for slots, allocators and key services


    template<typename Key, typename T, typename H = DefaultKeyService<Key>, typename D = ScanDiscipline>
    struct PersistentMap {
        
        using U = typename H::code_type;

        using AMT = ArrayMappedTrie<U, T, ScanDiscipline::InnerDiscipline>;
        using Slot = D::template Slot<AMT const*>;
        Slot _inner{};

        // const ArrayMappedTrie<U, T>* _inner = nullptr;

        bool contains(Key key) const {
            U j = H{}.encode(key);
            return _inner && _inner->contains(j);
        }
                
        bool try_get(Key key, T& victim) const {
            U j = H{}.encode(key);
            return _inner && _inner->try_get(j,
                                             victim);
        }
                        
        [[nodiscard]] PersistentMap clone_and_set(Key key, T value) const {
            U j = H{}.encode(key);
            T _ = {};
            return PersistentMap{Slot{
                _inner
                ? _inner->clone_and_insert_or_assign_key_value(j, value, _).first
                : AMT::make_singleton(j, value)
            }};
        }
        
        [[nodiscard]] std::pair<PersistentMap, bool> clone_and_try_erase(Key key, T& victim) const {
            U j = H{}.encode(key);
            std::pair<PersistentMap, bool> result = { *this, false };
            if (_inner) {
                std::tie(result.first._inner,
                         result.second) = _inner->clone_and_erase_key(j, victim);
            }
            return result;
        }

        
        // Mutable interface.  The backing structure remains immutable; this is
        // just sugar to tersely swing the pointer.
        PersistentMap& set(Key key, T value) {
            *this = clone_and_set(key, value);
            return *this;
        }

        bool try_erase(Key key, T& victim) {
            auto [node, flag] = clone_and_try_erase(key, victim);
            _inner = node._inner;
            return flag;
        }
        
        void parallel_for_each(auto&& action) const {
            if (_inner) {
                _inner->parallel_for_each([&action](uint64_t key, T value) {
                    action(H{}.decode(key), value);
                });
            }
        }
        
        void for_each(auto&& action) const {
            if (_inner) {
                _inner->for_each([&action](uint64_t key, T value) {
                    action(H{}.decode(key), value);
                });
            }
        }


    };
    
    template<typename Key, typename T, typename H, typename D>
    void garbage_collected_scan(const PersistentMap<Key, T, H, D>& x) {
        garbage_collected_scan(x._inner);
    }
        
    template<typename T>
    struct ParallelRebuildAction {
        enum {
            NONE = 0,
            WRITE_VALUE,
            CLEAR_VALUE,
            MERGE_VALUE,
        } tag;
        T value;
    };

    // Parallel rebuild (Stage 1): materialize the frozen modifier skiplist into
    // a code-ordered vector (dropping NONE so untouched subtrees stay shared),
    // then apply it to `source` via the AMT co-recursion.  The materialized keys
    // come out sorted by H's code because the modifier's comparator agrees with
    // H -- the rebuild precondition (see container/docs/parallel_rebuild.md).
    template<typename Key, typename T, typename H, typename D,
             typename U, typename F, typename S2, typename D2>
    [[nodiscard]] Coroutine::Future<PersistentMap<Key, T, H, D>>
    coroutine_parallel_rebuild(const PersistentMap<Key, T, H, D>& source,
                               const ConcurrentMap<Key, U, S2, D2>& modifier,
                               F&& action_for_key) {
        using PM = PersistentMap<Key, T, H, D>;
        using AMT = typename PM::AMT;
        using Action = ParallelRebuildAction<T>;

        std::vector<std::pair<typename H::code_type, Action>> mods;
        for (auto it = modifier.begin(); it != modifier.end(); ++it) {
            Action a = co_await action_for_key(*it);
            if (a.tag == Action::NONE)
                continue; // no-op: leave the subtree shared
            mods.emplace_back(H{}.encode(it->first), std::move(a));
        }

        auto combine = [](const T* old, const Action& a) -> std::optional<T> {
            switch (a.tag) {
                case Action::WRITE_VALUE: return a.value;
                case Action::CLEAR_VALUE: return std::nullopt;
                case Action::MERGE_VALUE: abort(); // not meaningful for a plain map
                case Action::NONE:        break;   // filtered above
            }
            return old ? std::optional<T>(*old) : std::nullopt;
        };

        const AMT* inner = source._inner ? &*source._inner : nullptr;
        const AMT* result = co_await AMT::coroutine_parallel_rebuild(
            inner, mods, 0, mods.size(), combine);
        co_return PM{ typename PM::Slot{ result } };
    }

} // namespace wry

#endif /* persistent_map_hpp */



