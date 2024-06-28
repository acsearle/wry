//
//  wry/gc/HeapString.hpp
//  client
//
//  Created by Antony Searle on 22/6/2024.
//

#ifndef wry_gc_HeapString_hpp
#define wry_gc_HeapString_hpp

#include <string_view>

#include "object.hpp"
#include "ctrie.hpp"

namespace wry::gc {
    
    struct HeapString : _ctrie::BranchNode {
        
        size_t _hash;
        size_t _size;
        char _bytes[0];
        
        static void* operator new(std::size_t count, std::size_t extra);
        static const HeapString* make(std::size_t hash, std::string_view view);
        static const HeapString* make(std::string_view view);
        std::string_view as_string_view() const;
        
        HeapString();
        virtual ~HeapString() final;
        
        virtual void _object_shade() const override;
        virtual size_t _object_hash() const override { return _hash; }
        virtual void _object_trace() const override;
        virtual void _object_trace_weak() const override;
        virtual Color _object_sweep() const override;

        virtual const HeapString* _ctrie_any_find_or_emplace2(const _ctrie::INode* in, const _ctrie::LNode* ln) const override;
        
        virtual const _ctrie::MainNode* _ctrie_bn_to_contracted(const _ctrie::CNode*) const override;
        virtual const HeapString* _ctrie_bn_find_or_emplace(_ctrie::Query query, int lev, const _ctrie::INode* i, const _ctrie::CNode* cn, int pos) const override;
        virtual _ctrie::EraseResult _ctrie_bn_erase(const HeapString* key, int lev, const _ctrie::INode* i, const _ctrie::CNode* cn, int pos, uint64_t flag) const override;
        
    }; // struct HeapString
    
    
    template<size_t N, typename>
    constexpr Value::Value(const char (&ntbs)[N]) {
        const size_t M = N - 1;
        assert(ntbs[M] == '\0');
        if (M < 8) {
            _short_string_t s;
            s._tag_and_len = (M << VALUE_SHIFT) | VALUE_TAG_SHORT_STRING;
            // builtin for constexpr
            __builtin_memcpy(s._chars, ntbs, M);
            __builtin_memcpy(&_data, &s, 8);
        } else {
            _data = (uint64_t)HeapString::make(ntbs);
        }
    }
    
} // namespace wry::gc

#endif /* wry_gc_HeapString_hpp */
