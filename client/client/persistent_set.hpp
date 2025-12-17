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

#include "array_mapped_trie.hpp"
#include "utility.hpp"
#include "coroutine.hpp"

namespace wry {
    
    using Coroutine::Task;
        
    template<typename Key, typename H>
    struct PersistentSet {
        
        using U = typename H::hash_type;
        
        array_mapped_trie::Node<U>* _inner = nullptr;
        
        bool contains(Key key) const {
            U j = H{}.hash(key);
            uint64_t _ = {};
            return _inner && _inner->try_get(j, _);
        }
        
        [[nodiscard]] PersistentSet clone_and_set(Key key) const {
            U j = H{}.hash(key);
            uint64_t value = {};
            uint64_t _ = {};
            return PersistentSet{
                _inner
                ? _inner->clone_and_insert_or_assign_key_value(j, value, _).first
                : array_mapped_trie::Node<uint64_t>::make_singleton(j, value)
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
                _inner->parallel_for_each([&action](uint64_t key, uint64_t) {
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
                co_await _inner->coroutine_parallel_for_each([&action](uint64_t key, uint64_t) {
                    action(H{}.unhash(key));
                });
            }
        }

        Task coroutine_parallel_for_each_coroutine(auto&& action) const {
            if (_inner) {
                co_await _inner->coroutine_parallel_for_each_coroutine([&action](uint64_t key, uint64_t) -> Task {
                    co_await action(H{}.unhash(key));
                });
            }
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
            
} // namespace wry

#endif /* persistent_set_hpp */
