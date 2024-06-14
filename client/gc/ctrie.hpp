//
//  ctrie.hpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#ifndef ctrie_hpp
#define ctrie_hpp

#include "value.hpp"

namespace gc {
    
    bool value_is_RESTART(const Value& self);
    bool value_is_NOTFOUND(const Value& self);
    bool value_is_OK(const Value& self);
    
    Value value_make_NOTFOUND();
    Value value_make_RESTART();
    Value value_make_OK();

    struct Ctrie : Object {
        
        struct Branch;
        struct MainNode;

        struct CNode;
        struct INode;
        struct LNode;
        struct SNode;
        struct TNode;

        enum {
            W = 6
        };
        

        struct Branch : Object {
            explicit Branch(Class class_) : Object(class_) {}
            Branch* resurrect();
        };
        

        struct MainNode : Object {
            explicit MainNode(Class class_) : Object(class_) {}
        };
        
        
        struct CNode : MainNode {
            static void* operator new(std::size_t fixed, std::size_t variable);
            static CNode* make(int num);
            static CNode* make(SNode* sn, SNode* nsn, int lev);
            CNode();
            uint64_t bmp;
            Branch* array[0];
            CNode* inserted(int pos, uint64_t flag, Branch* bn);
            CNode* updated(int pos, Branch* bn);
            CNode* removed(int pos, uint64_t flag);
            CNode* resurrected();
            MainNode* toCompressed(int level);
            MainNode* toContracted(int level);

        };
        
        struct INode : Branch {
            Traced<Atomic<MainNode*>> main;
            explicit INode(MainNode*);
            Value lookup(Value key, int level, INode* parent);
            bool insert(Value key, Value value, int lev, INode* parent);
            Value remove(Value key, int level, INode* parent);
            void clean(int lev);
        };
        
        struct LNode : MainNode {
            SNode* sn;
            LNode* next;
            Value lookup(Value);
            LNode* inserted(Value k, Value v);
            LNode* removed(Value k);
        };
        
        struct SNode : Branch {
            Value key;
            Value value;
            SNode(Value k, Value v);
            TNode* entomb();
        };
        
        struct TNode : MainNode {
            SNode* sn;
            explicit TNode(SNode* sn);
        };
                

        
       
        INode* root;
        
        
       
        
        
        
        static std::pair<uint64_t, int> flagpos(uint64_t h, int lev, uint64_t bmp);


        
        
        Value lookup(Value key);
        void insert(Value k, Value v);
        Value remove(Value key);
        
        
        
        
        static MainNode* READ(Traced<Atomic<MainNode*>>& main);
        static bool CAS(Traced<Atomic<MainNode*>>& main, MainNode* expected, MainNode* desired);
        
        
        
        
       
        
       
        
        static void cleanParent(INode* p, INode* i, size_t hc, int lev);
        
        
        Ctrie();
       
        
    }; // struct Ctrie
    
    
    
    

    
} // namespace gc

#endif /* ctrie_hpp */
