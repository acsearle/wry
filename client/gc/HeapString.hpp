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

namespace wry::gc {
    
    struct HeapString : Object {
        std::size_t _hash;
        std::size_t _size;
        char _bytes[0];
        static void* operator new(std::size_t count, std::size_t extra);
        static const HeapString* make(std::size_t hash, std::string_view view);
        static const HeapString* make(std::string_view view);
        std::string_view as_string_view() const;
        HeapString();
        virtual ~HeapString() final = default;
        
        
        virtual const MainNode* _ctrie_toContracted(const MainNode*) const override;
        virtual const HeapString* _ctrie_find_or_emplace2(Query query, int lev, const INode* parent, const INode* i, const CNode* cn, int pos) const override;
        virtual Value _ctrie_erase2(const HeapString* key, int lev, const INode* parent, const INode* i, const CNode* cn, int pos, uint64_t flag) const override;

        virtual void _object_shade() const override;
        
        virtual size_t _object_hash() const override {
            return _hash;
        }

        
        virtual void _object_trace() const override;
        virtual void _object_trace_weak() const override;
        
        virtual Color _object_sweep() const override;

        
    }; // struct HeapString
    
}

#endif /* wry_gc_HeapString_hpp */
