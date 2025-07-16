//
//  wry/gc/HeapString.hpp
//  client
//
//  Created by Antony Searle on 22/6/2024.
//

#ifndef wry_gc_HeapString_hpp
#define wry_gc_HeapString_hpp

#include <string_view>

#include "garbage_collected.hpp"
#include "ctrie.hpp"

namespace wry {
    
    struct HeapString final : _ctrie::BranchNode {

        static void* operator new(std::size_t count, std::size_t extra);
        static const HeapString* make(std::size_t hash, std::string_view view);
        static const HeapString* make(std::string_view view);

        size_t _hash;
        size_t _size;
        char _bytes[0];
        
        std::string_view as_string_view() const;
        
        HeapString();
        virtual ~HeapString() override final;
        
        virtual void _garbage_collected_shade() const override final;
        virtual void _garbage_collected_scan(void*) const override final;
        virtual size_t _garbage_collected_hash() const override final { return _hash; }
        virtual void _garbage_collected_trace(void*) const override final;
        virtual void _garbage_collected_trace_weak(void*) const override final;
        virtual Color _garbage_collected_sweep() const override final;
        virtual void _garbage_collected_debug() const override final;

        virtual const HeapString* _ctrie_any_find_or_emplace2(const _ctrie::INode* in, const _ctrie::LNode* ln) const override final;
        
        virtual const _ctrie::MainNode* _ctrie_bn_to_contracted(const _ctrie::CNode*) const override final;
        virtual const HeapString* _ctrie_bn_find_or_emplace(_ctrie::Query query, int lev, const _ctrie::INode* i, const _ctrie::CNode* cn, int pos) const override final;
        virtual _ctrie::EraseResult _ctrie_bn_erase(const HeapString* key, int lev, const _ctrie::INode* i, const _ctrie::CNode* cn, int pos, uint64_t flag) const override final;
        
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
    
} // namespace wry

#endif /* wry_gc_HeapString_hpp */
