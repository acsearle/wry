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
        
    template<typename Key, typename H = DefaultKeyService<Key>>
    struct PersistentSet {
        
        using U = typename H::hash_type;
        using T = int;
        
        using N = array_mapped_trie::Node<T, U>;
        N const* _inner = nullptr;
        
        bool contains(Key key) const {
            U j = H{}.hash(key);
            uint64_t _ = {};
            return _inner && _inner->try_get(j, _);
        }
        
        [[nodiscard]] PersistentSet clone_and_set(Key key) const {
            U j = H{}.hash(key);
            int value = {};
            int _ = {};
            return PersistentSet{
                _inner
                ? _inner->clone_and_insert_or_assign_key_value(j, value, _).first
                : array_mapped_trie::Node<int, U>::make_singleton(j, value)
            };
        }
        
        // Mutable interface.  The backing structure remains immutable; this is
        // just sugar to tersely swing the pointer.
        PersistentSet& set(Key key) {
            mutator_overwrote(_inner);
            return *this = clone_and_set(key);
        }
                
        void for_each(auto&& action) const {
            if (_inner) {
                _inner->parallel_for_each([&action](U key, int) {
                    // TODO: we need a better way of mapping the Key type to
                    // and from the integer type
                    action(H{}.unhash(key));
                });
            }
        }

        void parallel_for_each(auto&& action) const {
            // TODO: parallel implementation
            for_each(std::forward<decltype(action)>(action));
        }
        
        Task coroutine_parallel_for_each(auto&& action) const {
            if (_inner) {
                co_await _inner->coroutine_parallel_for_each([&action](U key, int) {
                    action(H{}.unhash(key));
                });
            }
        }

        Task coroutine_parallel_for_each_coroutine(auto&& action) const {
            if (_inner) {
                co_await _inner->coroutine_parallel_for_each_coroutine([&action](U key, int) -> Task {
                    co_await action(H{}.unhash(key));
                });
            }
        }
        
        void merge(PersistentSet const& other) {
            mutator_overwrote(_inner);
            _inner = N::merge(_inner, other._inner);
        }

        
    }; // PersistentSet
    
    template<typename Key, typename H> auto
    merge(PersistentSet<Key, H> const& left, PersistentSet<Key, H> const& right) -> PersistentSet<Key, H> {
        // TODO: this implementation fails to reuse right subtrees
        PersistentSet<Key, H> result = left;
        for (auto key : right) {
            result.set(key);
        }
        return result;
    }
    
    
    
    template<typename Key, typename H>
    void garbage_collected_scan(const PersistentSet<Key, H>& x) {
        garbage_collected_scan(x._inner);
    }
    
    
    template<typename A, typename B, typename H>
    auto
    partition_first(PersistentSet<std::pair<A, B>, H> const& x, A a) {
        using Key = std::pair<A, B>;
        using S = PersistentSet<Key, H>;
        auto z = H{}.hash(Key{a, B{}});
        auto mask = H{}.mask_first();
        std::pair<S, S> result;
        auto c = x._inner->partition_mask(x._inner, z, mask);
        result.first._inner = c.first;
        result.second._inner = c.second;
        return result;
    };
    
    template<typename Key, typename H, typename Key2, typename U, typename F>
    Coroutine::Future<PersistentSet<Key, H>>
    coroutine_parallel_rebuild(const PersistentSet<Key, H>& source,
                               const ConcurrentMap<Key2, U>& modifier,
                               F&& action_for_key) {
        PersistentSet<Key, H> result{source};
        auto first = modifier.begin();
        auto last = modifier.end();
        for (; first != last; ++first) {
            using P = ParallelRebuildAction<PersistentSet<Key, H>>;
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
            }
        }
        co_return result;
    }
    
    
            
} // namespace wry

#endif /* persistent_set_hpp */
