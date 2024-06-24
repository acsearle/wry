//
//  ctrie.hpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#ifndef ctrie_hpp
#define ctrie_hpp

#include "object.hpp"
#include "value.hpp"

namespace wry::gc {
    
    // struct Branch;
    struct MainNode;
    
    struct CNode;
    struct INode;
    struct LNode;
    // struct SNode;
    struct TNode;
    
    struct Query {
        size_t hash;
        string_view view;
    };
    
    struct MainNode : Object {
        
    };
    
    struct CNode : MainNode {
        virtual ~CNode() final = default;
        static void* operator new(std::size_t fixed, std::size_t variable);
        static const CNode* make(int num);
        static const CNode* make(const HeapString* sn1, const HeapString* sn2, int lev);
        CNode();
        uint64_t bmp;
        const Object* array[0];
        const CNode* inserted(int pos, uint64_t flag, const Object* bn) const;
        const CNode* updated(int pos, const Object* bn) const;
        const CNode* removed(int pos, uint64_t flag) const;
        const CNode* resurrected() const;
        const MainNode* toCompressed(int level) const;
        const MainNode* toContracted(int level) const;
        
        
        virtual void _ctrie_clean(int level, const INode* parent) const override;
        virtual void _ctrie_cleanParent(const INode* p, const INode* i, size_t hc, int lev, const MainNode* m) const override;
        virtual const HeapString* _ctrie_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const override;
        virtual Value _ctrie_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const override;

        
        virtual void _object_scan() const override;
        

        
    };
    
    struct INode : Object {
        mutable Traced<Atomic<const MainNode*>> main;
        explicit INode(const MainNode*);
        virtual ~INode() final = default;
        // Value lookup(Value key, int level, INode* parent);
        //bool insert(Value key, Value value, int lev, INode* parent);
        //Value remove(Value key, int level, INode* parent);
        void clean(int lev) const;
        
        const HeapString* find_or_emplace(Query query, int lev, const INode* parent) const;
        Value erase(const HeapString* key, int level, const INode* parent) const;
        
        
        virtual const Object* _ctrie_resurrect() const override;
        virtual const MainNode* _ctrie_toContracted(const MainNode*) const override;
        
        virtual const HeapString* _ctrie_find_or_emplace2(Query query, int lev, const INode* parent, const INode* i, const CNode* cn, int pos) const override;
        virtual Value _ctrie_erase2(const HeapString* key, int lev, const INode* parent, const INode* i, const CNode* cn, int pos, uint64_t flag) const override;

        virtual void _object_scan() const override;

        
    };
    
    
    
    struct LNode : MainNode {
        const HeapString* sn;
        const LNode* next;
        //Value lookup(Value);
        //LNode* inserted(Value k, Value v);
        //LNode* removed(Value k);
        const Object* find_or_emplace(Query query) const;
        const LNode* erase(const HeapString* key) const;
        LNode();
        virtual ~LNode() final = default;
        
        const LNode* removed(const LNode* victim) const;
        virtual const HeapString* _ctrie_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const override;
        virtual Value _ctrie_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const override;

        virtual void _object_scan() const override;

    };
    
    
    
    struct TNode : MainNode {
        const HeapString* sn;
        explicit TNode(const HeapString* sn);
        virtual ~TNode() final = default;
        
        virtual const Object* _ctrie_resurrect() const override;
        virtual void _ctrie_cleanParent2(const INode* p, const INode* i, size_t hc, int lev,
                                         const CNode* cn, int pos) const override;
        virtual const HeapString* _ctrie_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const override;
        virtual Value _ctrie_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const override;
        virtual void _ctrie_cleanParent3(const INode* p, const INode* i, size_t hc, int lev) const override;

        virtual void _object_scan() const override;

    };
    
    
    
    
    struct Ctrie : Object {
        
        

        const INode* root;
        
        static std::pair<uint64_t, int> flagpos(uint64_t h, int lev, uint64_t bmp);

        // Value lookup(Value key);
        // void insert(Value k, Value v);
        const HeapString* find_or_emplace(Query query);
        void erase(const HeapString* key);
        
        static const MainNode* READ(const Traced<Atomic<const MainNode*>>& main);
        static bool CAS(Traced<Atomic<const MainNode*>>& main, const MainNode* expected, const MainNode* desired);
        
        static void cleanParent(const INode* p, const INode* i, size_t hc, int lev);
        
        
        Ctrie();
        virtual ~Ctrie() final = default;
        
        virtual void _object_scan() const override;
       
        
    }; // struct Ctrie
    
    
} // namespace gc

#endif /* ctrie_hpp */
