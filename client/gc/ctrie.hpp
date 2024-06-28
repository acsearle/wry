//
//  ctrie.hpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#ifndef ctrie_hpp
#define ctrie_hpp

#include "value.hpp"

namespace wry::gc {
    
    struct HeapString;
    
    namespace _ctrie {

        struct Query;

        struct AnyNode;

        struct MainNode;
        struct BranchNode;

        struct CNode;
        struct LNode;
        struct TNode;
        
        struct INode;
        
        struct Query {
            size_t hash;
            std::string_view view;
        };
        
        struct AnyNode : Object {
            virtual const HeapString* 
            _ctrie_any_find_or_emplace2(const INode* in, const LNode* ln) const;
        };
        
        enum class EraseResult {
            RESTART,
            OK,
            NOTFOUND,
        };
        
        struct BranchNode : AnyNode {
            virtual EraseResult
            _ctrie_bn_erase(const HeapString* key, int level, const INode* in,
                            const CNode* cn, int pos, uint64_t flag) const = 0;
            virtual const HeapString*
            _ctrie_bn_find_or_emplace(Query query, int level, const INode* in,
                                      const CNode* cn, int pos) const = 0;
            virtual const BranchNode* _ctrie_bn_resurrect() const;
            virtual const MainNode*
            _ctrie_bn_to_contracted(const CNode* cn) const;
        };
        
    } // namespace _ctrie
    
    
    struct Ctrie : Object {
        
        const _ctrie::INode* root;
                                
        Ctrie();
        virtual ~Ctrie() override final;
        
        const HeapString* find_or_emplace(_ctrie::Query query);
        void erase(const HeapString* key);
        
        virtual void _object_scan() const override;
        
    }; // struct Ctrie
    
    
} // namespace wry::gc

#endif /* ctrie_hpp */
