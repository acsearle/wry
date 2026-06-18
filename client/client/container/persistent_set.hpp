//
//  persistent_set.hpp
//  client
//
//  Created by Antony Searle on 23/11/2024.
//

#ifndef persistent_set_hpp
#define persistent_set_hpp

#include <iostream>
#include <mutex>
#include <set>
#include <tuple>

#include "array_mapped_trie.hpp"
#include "coroutine.hpp"
#include "key_service.hpp"
#include "utility.hpp"
#include "concurrent_map.hpp"
#include "persistent_map.hpp"

namespace wry {
    
    using Coroutine::Task;
        
    template<typename Key, typename H, typename Discipline>
    struct PersistentSet {
        
        using U = typename H::code_type;
        // A set carries no value; std::monostate is the empty-value convention
        // (its no-op garbage_collected_scan lives in ext/variant.hpp).
        using T = std::monostate;
        
        using N = ArrayMappedTrie<U, T, typename Discipline::InnerDiscipline>;
        Discipline::template Slot<N const*> _inner = nullptr;

        bool contains(Key key) const {
            U j = H{}.encode(key);
            return _inner && _inner->contains(j);
        }
        
        [[nodiscard]] PersistentSet clone_and_set(Key key) const {
            U j = H{}.encode(key);
            T value = {};
            T _ = {};
            return PersistentSet{
                _inner
                ? _inner->clone_and_insert_or_assign_key_value(j, value, _).first
                : N::make_singleton(j, value)
            };
        }
        
        // Mutable interface.  The backing structure remains immutable; this is
        // just sugar to tersely swing the pointer.
        PersistentSet& set(Key key) {
            return *this = clone_and_set(key);
        }
                
        void for_each(auto&& action) const {
            if (_inner) {
                _inner->for_each([&action](U key, T) {
                    // TODO: we need a better way of mapping the Key type to
                    // and from the integer type
                    action(H{}.decode(key));
                });
            }
        }
        
        Task coroutine_parallel_for_each(auto&& action) const {
            if (_inner) {
                co_await _inner->coroutine_parallel_for_each([&action](U key, T) {
                    action(H{}.decode(key));
                });
            }
        }

        Task coroutine_parallel_for_each_coroutine(auto&& action) const {
            if (_inner) {
                co_await _inner->coroutine_parallel_for_each_coroutine([&action](U key, T) -> Task {
                    co_await action(H{}.decode(key));
                });
            }
        }
        
        void merge(PersistentSet const& other) {
            _inner = N::merge(_inner, other._inner);
        }
        
    }; // PersistentSet
    
    /*
    template<typename Key, typename H> auto
    merge(PersistentSet<Key, H> const& left, PersistentSet<Key, H> const& right) -> PersistentSet<Key, H> {
        // TODO: this implementation fails to reuse right subtrees
        PersistentSet<Key, H> result = left;
        for (auto key : right) {
            result.set(key);
        }
        return result;
    }
     */
    
    
    
    template<typename Key, typename H, typename D>
    void garbage_collected_scan(const PersistentSet<Key, H, D>& x) {
        garbage_collected_scan(x._inner);
    }
    
    
    template<typename A, typename B, typename H, typename D>
    auto
    partition_first(PersistentSet<std::pair<A, B>, H, D> const& x, A a) {
        using Key = std::pair<A, B>;
        using S = PersistentSet<Key, H, D>;
        auto z = H{}.encode(Key{a, B{}});
        auto mask = H{}.mask_first();
        std::pair<S, S> result;
        auto c = x._inner->partition_mask(x._inner, z, mask);
        result.first._inner = c.first;
        result.second._inner = c.second;
        return result;
    };
    
    // NB: the flat-pair multimap helpers (for_each_if_first, as_multimap_*) were
    // removed when the waiter index became a nested PersistentMap<Key, WaitSet>;
    // see container/docs/parallel_rebuild.md.  partition_first above is still
    // used by the time wheel.


    
    template<typename Key, typename H, typename D, typename Key2, typename U, typename F, typename S2, typename D2>
    Coroutine::Future<PersistentSet<Key, H, D>>
    coroutine_parallel_rebuild(const PersistentSet<Key, H, D>& source,
                               const ConcurrentMap<Key2, U, S2, D2>& modifier,
                               F&& action_for_key) {
        PersistentSet<Key, H, D> result{source};
        auto first = modifier.begin();
        auto last = modifier.end();
        for (; first != last; ++first) {
            using P = ParallelRebuildAction<PersistentSet<Key, H, D>>;
            P action = co_await action_for_key(*first);
            switch (action.tag) {
                case P::NONE:
                    break;
                //case P::WRITE_VALUE:
                //    result.set(first->first, action.value);
                //    break;
                //case P::CLEAR_VALUE:
                //    (void) result.try_erase(first->first, action.value);
                //    break;
                case P::MERGE_VALUE:
                    result.merge(action.value);
                    break;
                default:
                    abort();
            }
        }
        co_return result;
    }
    
    
            
} // namespace wry

#endif /* persistent_set_hpp */
