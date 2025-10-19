//
//  ctrie.hpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#ifndef ctrie_hpp
#define ctrie_hpp

#include "value.hpp"

namespace wry {
    
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
        
        struct AnyNode : HeapValue {
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
        
        struct MainNode : AnyNode {
            virtual void _ctrie_mn_clean(int level, const INode* parent) const;
            virtual bool _ctrie_mn_cleanParent(const INode* p, const INode* i, size_t hc, int lev, const MainNode* m) const;
            virtual bool _ctrie_mn_cleanParent2(const INode* p, const INode* i, size_t hc, int lev, const CNode* cn, int pos) const;
            virtual EraseResult _ctrie_mn_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const = 0;
            virtual void _ctrie_mn_erase2(const INode* p, const INode* i, size_t hc, int lev) const;
            virtual const HeapString* _ctrie_mn_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const = 0;
            virtual const BranchNode* _ctrie_mn_resurrect(const INode* i) const;
        };
        
        struct INode : BranchNode {
            
            mutable Atomic<const MainNode*> main;
            
            explicit INode(const MainNode*);
            virtual ~INode() final = default;
            
            void clean(int lev) const;
            const HeapString* find_or_emplace(Query query, int level, const INode* parent) const;
            EraseResult erase(const HeapString* key, int level, const INode* parent) const;
            const MainNode* load() const;
            bool compare_exchange(const MainNode* expected, const MainNode* desired) const;
            
            
            virtual void _garbage_collected_scan() const override;
            
            virtual const BranchNode* _ctrie_bn_resurrect() const override;
            virtual const HeapString* _ctrie_bn_find_or_emplace(Query query, int level,
                                                                const INode* in, const CNode* cn, int pos) const override;
            virtual EraseResult _ctrie_bn_erase(const HeapString* key, int level, const INode* in,
                                                const CNode* cn, int pos, uint64_t flag) const override;
            
        };
        
    } // namespace _ctrie
    
    
    
    struct Ctrie : GarbageCollected {
        
        _ctrie::INode const* root;
                                
        Ctrie();
        virtual ~Ctrie() override final;
        
        const HeapString* find_or_emplace(_ctrie::Query query);
        void erase(const HeapString* key);
        
        virtual void _garbage_collected_scan() const override;
        
    }; // struct Ctrie
    
    
    
} // namespace wry

#endif /* ctrie_hpp */
