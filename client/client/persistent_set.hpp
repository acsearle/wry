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

namespace wry {
        
    template<typename Key>
    struct PersistentSet {
        
        array_mapped_trie::Node<uint64_t>* _inner = nullptr;
        
        bool contains(Key key) const {
            uint64_t j = persistent_map_index_for_key(key);
            uint64_t _ = {};
            return _inner && _inner->try_get(j, _);
        }
        
        [[nodiscard]] PersistentSet clone_and_set(Key key) const {
            uint64_t j = persistent_map_index_for_key(key);
            uint64_t value = {};
            uint64_t _ = {};
            return PersistentSet{
                _inner
                ? _inner->clone_and_insert_or_assign_key_value(j, value, _).first
                : array_mapped_trie::Node<uint64_t>::make_with_key_value(j, value)
            };
        }
        
        // Mutable interface.  The backing structure remains immutable; this is
        // just sugar to tersely swing the pointer.
        PersistentSet& set(Key key) {
            return *this = clone_and_set(key);
        }
                
        void for_each(auto&& action) const {
            if (_inner) {
                _inner->parallel_for_each([&action](uint64_t key, uint64_t) {
                    // TODO: we need a better way of mapping the Key type to
                    // and from the integer type
                    action(Key{key});
                });
            }
        }

        void parallel_for_each(auto&& action) const {
            // TODO: parallel implementation
            for_each(std::forward<decltype(action)>(action));
        }

        
    }; // PersistentSet
    
    template<typename Key> auto
    merge(PersistentSet<Key> const& left, PersistentSet<Key> const& right) -> PersistentSet<Key> {
        // TODO: this implementation fails to reuse right subtrees
        PersistentSet<Key> result = left;
        for (auto key : right) {
            result.set(key);
        }
        return result;
    }
    
    
    
    template<typename Key>
    void garbage_collected_scan(const PersistentSet<Key>& x) {
        garbage_collected_scan(x._inner);
    }
            
} // namespace wry

#endif /* persistent_set_hpp */
