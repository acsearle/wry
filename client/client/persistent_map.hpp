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
    
    
    template<typename Key, typename T, typename H = DefaultHasher<Key>>
    struct PersistentMap {
        
        using U = typename H::hash_type;
        
        const array_mapped_trie::Node<T, U>* _inner = nullptr;
        
        bool contains(Key key) const {
            U j = H{}.hash(key);
            return _inner && _inner->contains(j);
        }
                
        bool try_get(Key key, T& victim) const {
            U j = H{}.hash(key);
            return _inner && _inner->try_get(j,
                                             victim);
        }
                        
        [[nodiscard]] PersistentMap clone_and_set(Key key, T value) const {
            U j = H{}.hash(key);
            T _ = {};
            return PersistentMap{
                _inner
                ? _inner->clone_and_insert_or_assign_key_value(j, value, _).first
                : array_mapped_trie::Node<T>::make_singleton(j, value)
            };
        }
        
        [[nodiscard]] std::pair<PersistentMap, bool> clone_and_try_erase(Key key, T& victim) const {
            U j = H{}.hash(key);
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
            mutator_overwrote(_inner);
            *this = clone_and_set(key, value);
            return *this;
        }

        bool try_erase(Key key, T& victim) {
            auto [node, flag] = clone_and_try_erase(key, victim);
            mutator_overwrote(_inner);
            _inner = node._inner;
            return flag;
        }
        
        void parallel_for_each(auto&& action) const {
            if (_inner) {
                _inner->parallel_for_each([&action](uint64_t key, T value) {
                    action(H{}.unhash(key), value);
                });
            }
        }
        
        void for_each(auto&& action) const {
            if (_inner) {
                _inner->for_each([&action](uint64_t key, T value) {
                    action(H{}.unhash(key), value);
                });
            }
        }


    };
    
    template<typename Key, typename T, typename H>
    void garbage_collected_scan(const PersistentMap<Key, T, H>& x) {
        garbage_collected_scan(x._inner);
    }
    
    template<typename Key, typename T, typename H>
    void garbage_collected_shade(const PersistentMap<Key, T, H>& x) {
        garbage_collected_shade(x._inner);
    }
    
    template<typename T>
    struct ParallelRebuildAction {
        enum {
            NONE = 0,
            WRITE_VALUE,
            CLEAR_VALUE,
        } tag;
        T value;
    };
    
    template<typename Key, typename T, typename H, typename U, typename F>
    PersistentMap<Key, T, H> parallel_rebuild(const PersistentMap<Key, T, H>& source,
                                           const ConcurrentMap<Key, U>& modifier,
                                           F&& action_for_key) {
        // Simple single-threaded implementation
        
        // TODO: Descend the two trees and rebuild up from the leaves.
        // This can be made highly parallel.
        
        PersistentMap<Key, T, H> result{source};
        // SAFETY: We can iterate the concurrent map here because it is
        // immutable in this phase
        auto first = modifier.begin();
        auto last = modifier.end();
        for (; first != last; ++first) {
            ParallelRebuildAction<T> action;
            action_for_key(action, *first);
            switch (action.tag) {
                case ParallelRebuildAction<T>::NONE:
                    break;
                case ParallelRebuildAction<T>::WRITE_VALUE:
                    result.set(first->first, action.value);
                    break;
                case ParallelRebuildAction<T>::CLEAR_VALUE:
                    (void) result.try_erase(first->first, action.value);
                    break;
            }
        }
        return result;
    }
    
    
    
    template<typename Key, typename T, typename H, typename U, typename F>
    Coroutine::Future<PersistentMap<Key, T, H>>
    coroutine_parallel_rebuild(const PersistentMap<Key, T, H>& source,
                               const ConcurrentMap<Key, U>& modifier,
                               F&& action_for_key) {
        
        
        // TODO: Descend the two trees and rebuild up from the leaves.
        // This can be made highly parallel.
        
        PersistentMap<Key, T, H> result{source};
        // SAFETY: We can iterate the concurrent map here because it is
        // immutable in this phase
        auto first = modifier.begin();
        auto last = modifier.end();
        for (; first != last; ++first) {
            ParallelRebuildAction<T> action = co_await action_for_key(*first);
            switch (action.tag) {
                case ParallelRebuildAction<T>::NONE:
                    break;
                case ParallelRebuildAction<T>::WRITE_VALUE:
                    result.set(first->first, action.value);
                    break;
                case ParallelRebuildAction<T>::CLEAR_VALUE:
                    (void) result.try_erase(first->first, action.value);
                    break;
            }
        }
        co_return result;
    }
    
    
    template<typename Key, typename T, typename H, typename U, typename F>
    Coroutine::Future<PersistentMap<Key, T, H>>
    coroutine_parallel_rebuild2(const PersistentMap<Key, T, H>& source,
                                const ConcurrentMap<Key, U>& modifier,
                                F&& action_for_key) {
        
        co_return PersistentMap<Key, T, H>{
            coroutine_parallel_rebuild2(source.inner,
                                        modifier,
                                        std::move(action_for_key))
        };
    }
    
} // namespace wry

#endif /* persistent_map_hpp */



