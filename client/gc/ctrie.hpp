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
        
        // struct Branch;
        struct MainNode;

        struct CNode;
        struct INode;
        struct LNode;
        // struct SNode;
        struct TNode;

        static Object* object_resurrect(Object* self);


        struct MainNode : Object {
            explicit MainNode(Class class_) : Object(class_) {}
        };
        
        
        struct CNode : MainNode {
            static void* operator new(std::size_t fixed, std::size_t variable);
            static CNode* make(int num);
            static CNode* make(HeapString* sn1, HeapString* sn2, int lev);
            CNode();
            uint64_t bmp;
            Object* array[0];
            CNode* inserted(int pos, uint64_t flag, Object* bn);
            CNode* updated(int pos, Object* bn);
            CNode* removed(int pos, uint64_t flag);
            CNode* resurrected();
            MainNode* toCompressed(int level);
            MainNode* toContracted(int level);

        };
        
        struct INode : Object {
            Traced<Atomic<MainNode*>> main;
            explicit INode(MainNode*);
            // Value lookup(Value key, int level, INode* parent);
            //bool insert(Value key, Value value, int lev, INode* parent);
            //Value remove(Value key, int level, INode* parent);
            void clean(int lev);
            
            HeapString* find_or_emplace(std::string_view sv, std::size_t hc, int lev, INode* parent);
            Value erase(HeapString* hs, int level, INode* parent);

            
        };
        
        struct LNode : MainNode {
            HeapString* sn;
            LNode* next;
            //Value lookup(Value);
            //LNode* inserted(Value k, Value v);
            //LNode* removed(Value k);
            Object* find_or_emplace(string_view, size_t hc);
            LNode* erase(HeapString* hs);
            LNode();
            
        };
        
        /*
        struct SNode : Branch {
            Value key;
            Value value;
            SNode(Value k, Value v);
            TNode* entomb();
        };
         */
        
        struct TNode : MainNode {
            HeapString* sn;
            explicit TNode(HeapString* sn);
        };
                

        
       
        INode* root;
        
        
       
        
        
        
        static std::pair<uint64_t, int> flagpos(uint64_t h, int lev, uint64_t bmp);


        
        
        // Value lookup(Value key);
        // void insert(Value k, Value v);
        HeapString* find_or_emplace(std::string_view sv, size_t hc);
        void erase(HeapString* key);
        
        
        
        
        static MainNode* READ(Traced<Atomic<MainNode*>>& main);
        static bool CAS(Traced<Atomic<MainNode*>>& main, MainNode* expected, MainNode* desired);
        
        
        
        
       
        
       
        
        static void cleanParent(INode* p, INode* i, size_t hc, int lev);
        
        
        Ctrie();
       
        
    }; // struct Ctrie
    
    
    
    

    
} // namespace gc

#endif /* ctrie_hpp */
