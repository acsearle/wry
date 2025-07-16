//
//  persistent_set.hpp
//  client
//
//  Created by Antony Searle on 23/11/2024.
//

#ifndef persistent_set_hpp
#define persistent_set_hpp

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <algorithm>
#include <bit>
#include <memory>
#include <set>
#include <new>

#include "garbage_collected.hpp"


namespace wry {
    
    template<typename Key, typename Compare>
    void trace(const std::set<Key, Compare>& s,void*q) {
        for (const Key& k : s)
            trace(k,q);
    }
    
    namespace _persistent_set0 {
        
        template<typename Key>
        struct PersistentSet : GarbageCollected {
            
            std::set<Key> data;
            
            virtual void _garbage_collected_scan(void* p) const override {
                //printf("%s\n", __PRETTY_FUNCTION__);
                trace(data, p);
            }
            
            explicit PersistentSet(auto&&... args) : data(FORWARD(args)...) {}
            
            virtual ~PersistentSet() {
                //printf("%s\n", __PRETTY_FUNCTION__);
            }
            
            bool contains(Key k) const {
                return data.contains(k);
            }
            
            template<typename K>
            [[nodiscard]] const PersistentSet* clone_and_erase(K&& k) const {
                std::set<Key> a{data}; // deep copy
                a.erase(std::forward<K>(k));
                return new PersistentSet{std::move(a)};
            }
            
            template<typename K>
            [[nodiscard]] const PersistentSet* clone_and_insert(K&& k) const {
                std::set<Key> a{data}; // deep copy
                a.insert(std::forward<K>(k));
                return new PersistentSet{std::move(a)};
            }
            
            template<typename F>
            void parallel_for_each(const F& f) const {
                for (const auto& k : data)
                    f(k);
            }
            
        };
        
    }
    
    using _persistent_set0::PersistentSet;
    

    
    
    namespace _persistent_map2 {
        
        template<typename T>
        struct Node : GarbageCollected {
        };
        
        template<typename T>
        struct Branch : GarbageCollected {
            uint64_t _prefix;      // (58 - _shift) upper bits common to keys
            int _shift;            // bits for indexing
            uint64_t _bitmap;      // map of bits for indexing
            Node<T>* _children[0]; // popcount(_bitmap) children
        };
                
        template<typename T>
        struct Leaf : GarbageCollected {
            uint64_t _prefix; // upper 58 bits common to keys
            uint64_t _bitmap; // map of lower six bits of keys
            T _values[0];     // popcount(_bitmap) values
            
            T* find(uint64_t key) {
                if ((_prefix ^ key) >> 6)
                    // not present
                    return nullptr;
                int index = (int)(key & 63);
                uint64_t select = (uint64_t)1 << index;
                if (!(select & _bitmap))
                    // not present
                    return nullptr;
                uint64_t mask = select - 1;
                int compressed_index = __builtin_popcountll(mask & _bitmap);
                return _values + compressed_index;
            }
            
        };
        
    } // namespace _persistent_map1
    
} // namespace wry

#endif /* persistent_set_hpp */
