//
//  trie.hpp
//  client
//
//  Created by Antony Searle on 4/11/2025.
//

#ifndef trie_hpp
#define trie_hpp

#include "garbage_collected.hpp"
#include "stdint.hpp"

namespace wry::_trie {
    
    template<typename T>
    struct Node : GarbageCollected {
        
        virtual bool try_get(uint64_t key, T& victim) const = 0;
        
    };
    
    template<typename T>
    struct FlatMapLeaf : Node<T> {
        
        size_t _size;
        std::pair<uint64_t, T> _array[0] __counted_by(_size);
        
        virtual bool try_get(uint64_t key, T& victim) const override {
            for (size_t i = 0; i != _size; ++i)
                if (_array[i].first == key) {
                    victim = _array[i].second;
                    return true;
                }
            return false;
        };

    };
    
    template<typename T>
    struct FlatMapBranch : Node<T> {
        uint64_t _mask;
        size_t _size;
        std::pair<uint64_t, Node<T>const* _Nonnull> _array[0] __counted_by(_size);

        virtual bool try_get(uint64_t key, T& victim) const override {
            for (size_t i = 0; i != _size; ++i)
                if ((_array[i].first & _mask) == (key & _mask))
                    return _array[i].second->try_get(key, victim);
            return false;
        };
        
    };
    
    template<typename T>
    struct SlotLeaf : Node<T> {
        enum : uint64_t {
            MASK = 0x000000000000003F,
        };
        uint64_t _prefix;
        T _array[64];
        virtual bool try_get(uint64_t key, T& victim) const override {
            if ((_prefix & ~MASK) != (key & ~MASK))
                return false;
            victim = _array[key & MASK];
            return true;
        }
    };
    
    template<typename T>
    struct CompressedArrayLeaf : Node<T> {
        enum : uint64_t {
            MASK = 0x000000000000003F,
        };
        uint64_t _prefix;
        uint64_t _bitmap;
        size_t _debug_count;
        T _array[0] __counted_by(_debug_count);
        virtual bool try_get(uint64_t key, T& victim) const override {
            if ((_prefix & ~MASK) != (key & ~MASK))
                return false;
            uint64_t i = key & MASK;
            uint64_t j = (uint64_t)1 << i;
            if (!(j & _bitmap))
                return false;
            int k = __builtin_popcountg((j - 1) & _bitmap);
            return _array[k];
        }
    };
    
} // namespace wry::_trie

#endif /* trie_hpp */
