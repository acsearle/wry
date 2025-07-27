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

namespace wry {
        
    template<typename Key, typename T, typename Compare>
    void trace(const std::map<Key, T, Compare>& m,void*q) {
        for (const auto& p : m)
            trace(p,q);
    }

    inline uint64_t persistent_map_index_for_key(uint64_t key) {
        return key;
    }
    
    template<typename Key, typename T>
    struct PersistentMap {
        
        const array_mapped_trie::Node<T>* _inner = nullptr;
        
        bool contains(Key key) const {
            uint64_t j = persistent_map_index_for_key(key);
            return _inner && _inner->contains(j);
        }
                
        bool try_get(Key key, T& victim) const {
            uint64_t j = persistent_map_index_for_key(key);
            return _inner && _inner->try_get(j,
                                             victim);
        }
                        
        [[nodiscard]] PersistentMap clone_and_set(Key key, T value) const {
            uint64_t j = persistent_map_index_for_key(key);
            T _ = {};
            return PersistentMap{
                _inner
                ? _inner->clone_and_insert_or_assign_key_value(j, value, _).first
                : array_mapped_trie::Node<T>::make_with_key_value(j, value)
            };
        }
        
        [[nodiscard]] std::pair<PersistentMap, bool> clone_and_try_erase(Key key, T& victim) const {
            uint64_t j = persistent_map_index_for_key(key);
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
            return *this = clone_and_set(key, value);
        }

        bool try_erase(Key key, T& victim) {
            auto [node, flag] = clone_and_try_erase(key, victim);
            _inner = node._inner;
            return flag;
        }

        
        void parallel_for_each(auto&& action) const {
            if (_inner) {
                _inner->parallel_for_each([&action](uint64_t key, T value) {
                    // TODO: we need a better way of mapping the Key type to
                    // and from the integer type
                    action(Key{key}, value);
                });
            }
        }

    };
    
    template<typename Key, typename T>
    void trace(const PersistentMap<Key, T>& x, void* context) {
        trace(x._inner, context);
    }
    
    template<typename Key, typename T>
    void shade(const PersistentMap<Key, T>& x) {
        shade(x._inner);
    }
    
    template<typename T>
    struct ParallelRebuildAction {
        enum {
            NONE,
            WRITE,
            ERASE,
        } tag;
        T value;
    };
    
    template<typename Key, typename T, typename U, typename F>
    PersistentMap<Key, T> parallel_rebuild(const PersistentMap<Key, T>& source,
                                           const ConcurrentMap<Key, U>& modifier,
                                           F&& action_for_key) {
        // Simple single-threaded implementation
        
        // TODO: Descend the two trees and rebuild up from the leaves.
        // This can be made highly parallel.
        
        PersistentMap<Key, T> result{source};
        // SAFETY: We can use the map unlocked here because it is immutable in
        // this phase
        auto first = modifier.begin();
        auto last = modifier.end();
        for (; first != last; ++first) {
            ParallelRebuildAction<T> action = action_for_key(*first);
            switch (action.tag) {
                case ParallelRebuildAction<T>::NONE:
                    break;
                case ParallelRebuildAction<T>::WRITE:
                    result.set(first->first, action.value);
                    break;
                case ParallelRebuildAction<T>::ERASE:
                    (void) result.try_erase(first->first, action.value);
                    break;
            }
        }
        return result;
    }

} // namespace wry

#endif /* persistent_map_hpp */



