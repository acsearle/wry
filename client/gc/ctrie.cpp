//
//  ctrie.cpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#include "utility.hpp"
#include "ctrie.hpp"
#include "HeapString.hpp"

namespace wry::gc {
    
    namespace _ctrie {
        
        struct MainNode : AnyNode {
            virtual void _ctrie_mn_clean(int level, const INode* parent) const;
            virtual bool _ctrie_mn_cleanParent(const INode* p, const INode* i, size_t hc, int lev, const MainNode* m) const;
            virtual bool _ctrie_mn_cleanParent2(const INode* p, const INode* i, size_t hc, int lev, const CNode* cn, int pos) const;
            virtual EraseResult _ctrie_mn_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const = 0;
            virtual void _ctrie_mn_erase2(const INode* p, const INode* i, size_t hc, int lev) const;
            virtual const HeapString* _ctrie_mn_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const = 0;
            virtual const BranchNode* _ctrie_mn_resurrect(const INode* i) const;
        };
        
        struct CNode : MainNode {
            
            static void* operator new(size_t fixed, size_t variable);
            
            static const CNode* make(int num);
            static const CNode* make(const HeapString* sn1, const HeapString* sn2, int lev);
            static std::pair<uint64_t, int> flagpos(uint64_t h, int lev, uint64_t bmp);
            
            uint64_t bmp;
            const BranchNode* array[0];
            
            CNode();
            virtual ~CNode() override final;
            
            const CNode* copy_insert(int pos, uint64_t flag, const BranchNode* bn) const;
            const CNode* copy_assign(int pos, const BranchNode* bn) const;
            const CNode* copy_erase(int pos, uint64_t flag) const;
            const CNode* resurrected() const;
            const MainNode* to_compressed(int level) const;
            const MainNode* to_contracted(int level) const;
            
            
            virtual void _ctrie_mn_clean(int level, const INode* parent) const override;
            virtual bool _ctrie_mn_cleanParent(const INode* p, const INode* i, size_t hc, int lev, const MainNode* m) const override;
            virtual EraseResult _ctrie_mn_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const override;
            virtual const HeapString* _ctrie_mn_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const override;
            
            virtual void _object_scan() const override;
            
        };
        
        struct INode : BranchNode {
            
            mutable Traced<Atomic<const MainNode*>> main;
            
            explicit INode(const MainNode*);
            virtual ~INode() final = default;
            
            void clean(int lev) const;
            const HeapString* find_or_emplace(Query query, int level, const INode* parent) const;
            EraseResult erase(const HeapString* key, int level, const INode* parent) const;
            const MainNode* load() const;
            bool compare_exchange(const MainNode* expected, const MainNode* desired) const;
            
            
            virtual void _object_scan() const override;
            
            virtual const BranchNode* _ctrie_bn_resurrect() const override;
            virtual const HeapString* _ctrie_bn_find_or_emplace(Query query, int level,
                                                                const INode* in, const CNode* cn, int pos) const override;
            virtual EraseResult _ctrie_bn_erase(const HeapString* key, int level, const INode* in,
                                                const CNode* cn, int pos, uint64_t flag) const override;
            
        };
        
        struct LNode : MainNode {
            
            const HeapString* sn;
            const LNode* next;
            
            LNode();
            virtual ~LNode() override final;
            
            const AnyNode* find_or_copy_emplace(Query query) const;
            const LNode* copy_erase(const HeapString* key) const;
            const LNode* copy_erase(const LNode* victim) const;
            
            virtual void _object_scan() const override;
            
            virtual const HeapString* _ctrie_any_find_or_emplace2(const INode* in, const LNode* ln) const override;
            
            virtual const HeapString* _ctrie_mn_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const override;
            virtual EraseResult _ctrie_mn_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const override;
            
        };
        
        
        struct TNode : MainNode {
            
            const HeapString* sn;
            
            explicit TNode(const HeapString* sn);
            virtual ~TNode() override final;
            
            const BranchNode* _ctrie_mn_resurrect(const INode* i) const override;
            virtual bool _ctrie_mn_cleanParent2(const INode* p, const INode* i, size_t hc, int lev,
                                                const CNode* cn, int pos) const override;
            virtual const HeapString* _ctrie_mn_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const override;
            virtual EraseResult _ctrie_mn_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const override;
            virtual void _ctrie_mn_erase2(const INode* p, const INode* i, size_t hc, int lev) const override;
            
            virtual void _object_scan() const override;
            
        };
        
        
        constexpr int W = 6;
       
        
        bool operator==(const Query& left, const HeapString* right) {
            size_t n = left.view.size();
            return ((left.hash == right->_hash)
                    && (n == right->_size)
                    && !__builtin_memcmp(left.view.data(), right->_bytes, n)
                    );
        }
        
        void cleanParent(const INode* p, const INode* i, size_t hc, int lev) {
            for (;;) {
                const MainNode* m = i->load();
                const MainNode* pm = p->load();
                if (pm->_ctrie_mn_cleanParent(p, i, hc, lev, m))
                    break;
            }
        }
        
        
        HeapString* make_HeapString_from_Query(Query query) {
            size_t n = query.view.size();
            assert(n > 7);
            HeapString* a = new(n) HeapString;
            a->_hash = query.hash;
            a->_size = n;
            __builtin_memcpy(a->_bytes, query.view.data(), n);
            return a;
        }
        
        
        const HeapString* AnyNode::_ctrie_any_find_or_emplace2(const INode* in, const LNode* ln) const {
            abort();
        }

                
        void MainNode::_ctrie_mn_clean(int level, const INode *parent) const {
            // noop
        }
        
        bool MainNode::_ctrie_mn_cleanParent(const INode* parent, const INode* in, size_t hc, int lev, const MainNode* m) const {
            return true;
        }
        
        bool MainNode::_ctrie_mn_cleanParent2(const INode* p, const INode* i, size_t hc, int lev, const CNode* cn, int pos) const {
            return true;
        }
        
        void MainNode::_ctrie_mn_erase2(const INode* parent, const INode* i, size_t hc, int lev) const {
            // noop
        }

        const BranchNode* MainNode::_ctrie_mn_resurrect(const INode* i) const {
            return i;
        }
        
        
        const BranchNode* BranchNode::_ctrie_bn_resurrect() const {
            return this;
        }
                
        const MainNode* BranchNode::_ctrie_bn_to_contracted(const CNode* cn) const {
            return cn;
        }


        void* CNode::operator new(size_t self, size_t entries) {
            return Object::operator new(self + entries * sizeof(Object*));
        }
        
        CNode::CNode() {
        }
        
        CNode::~CNode() {
            printf("~CNode[%llx]\n", bmp);
        }
        
        
        
        const CNode* CNode::copy_assign(int pos, const BranchNode *bn) const {
            int num = __builtin_popcountll(this->bmp);
            CNode* ncn = new(num) CNode;
            ncn->bmp = this->bmp;
            for (int i = 0; i != num; ++i) {
                const BranchNode* sub = (i == pos) ? bn : this->array[i];
                object_shade(sub);
                ncn->array[i] = sub;
            }
            return ncn;
        }
        
        std::pair<uint64_t, int> CNode::flagpos(uint64_t hash, int lev, uint64_t bmp) {
            uint64_t a = (hash >> lev) & 63;
            uint64_t flag = ((uint64_t)1 << a);
            int pos = __builtin_popcountll(bmp & (flag - 1));
            return {flag, pos};
        }
        
        const CNode* CNode::resurrected() const {
            int num = __builtin_popcountll(this->bmp);
            CNode* ncn = new(num) CNode;
            ncn->bmp = this->bmp;
            for (int i = 0; i != num; ++i) {
                const BranchNode* bn = this->array[i]->_ctrie_bn_resurrect();
                object_shade(bn);
                ncn->array[i] = bn;
            }
            return ncn;
        }
        
        const MainNode* CNode::to_contracted(int level) const {
            if (level == 0)
                return this;
            int num = __builtin_popcountll(this->bmp);
            if (num != 1)
                return this;
            const BranchNode* bn = this->array[0];
            return bn->_ctrie_bn_to_contracted(this);
        }
        
        const MainNode* CNode::to_compressed(int level) const {
            return resurrected()->to_contracted(level);
        }
        
        
        void CNode::_ctrie_mn_clean(int level, const INode* parent) const {
            // INode::clean() is only invoked in contexts where we are already
            // going to RESTART descending the tree, so we don't need to report
            // if this compare_exchange fails
            parent->compare_exchange(this, this->to_compressed(level));
        }
        
        bool CNode::_ctrie_mn_cleanParent(const INode* parent, const INode* in, size_t hc, int lev, const MainNode* m) const {
            // this == READ(p->main)
            //    m == READ(i->main)
            auto [flag, pos] = flagpos(hc, lev, bmp);
            if (!(flag & bmp))
                return true;
            if (array[pos] != in)
                return true;
            // INode parent -> CNode this -> INode in
            return m->_ctrie_mn_cleanParent2(parent, in, hc, lev, this, pos);
        }
        
        const HeapString* CNode::_ctrie_mn_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const {
            const CNode* cn = this;
            const MainNode* mn = this;
            const MainNode* nmn;
            const HeapString* nhs;
            auto [flag, pos] = flagpos(query.hash, lev, cn->bmp);
            if (!(cn->bmp & flag)) {
                nhs = make_HeapString_from_Query(query);
                nmn = cn->copy_insert(pos, flag, nhs);
                return i->compare_exchange(mn, nmn) ? nhs : nullptr;
            }
            const BranchNode* bn = cn->array[pos];
            return bn->_ctrie_bn_find_or_emplace(query, lev, i, cn, pos);
        }
        

        
        
        
        void INode::clean(int level) const {
            load()->_ctrie_mn_clean(level, this);
        }

        const BranchNode* INode::_ctrie_bn_resurrect() const {
            const MainNode* mn = load();
            return mn->_ctrie_mn_resurrect(this);
        }
        
        const HeapString* INode::find_or_emplace(Query query, int lev, const INode* parent) const {
            return load()->_ctrie_mn_find_or_emplace(query, lev, parent, this);
        }


        
        
        TNode::TNode(const HeapString* sn)
        : sn(sn) {
        }
        
        TNode::~TNode() {
            printf("~TNode\n");
        }

        bool TNode::_ctrie_mn_cleanParent2(const INode* p, const INode* i, size_t hc, int lev, const CNode* cn, int pos) const {
            return p->compare_exchange(cn,  cn->copy_assign(pos, sn)->to_contracted(lev));
        }

        const BranchNode* TNode::_ctrie_mn_resurrect(const INode*) const {
            return sn;
        }
                
        
        
        
        
        
        
       
        const HeapString* TNode::_ctrie_mn_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const {
            if (parent)
                parent->clean(lev - W);
            return nullptr;
        }
        
        const HeapString* LNode::_ctrie_mn_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const {
            return this->find_or_copy_emplace(query)->_ctrie_any_find_or_emplace2(i, this);
        }
                
        const HeapString* LNode::_ctrie_any_find_or_emplace2(const INode* in, const LNode* ln) const {
            // We are the head of a new LNode list created to contain the new HeapString
            const MainNode* mn = ln;
            const MainNode* nmn = this;
            // Try and install the new list
            // On success, return our string
            // On failure, return null to indicate we start over
            return in->compare_exchange(mn, nmn) ? sn : nullptr;
        }
        
        
        
        const HeapString* INode::_ctrie_bn_find_or_emplace(Query query, int lev,
                                                           const INode* i,
                                                           const CNode* cn,
                                                           int pos) const {
            return find_or_emplace(query, lev + W, i);
        }
        
        
        
        
        
        
        EraseResult INode::erase(const HeapString* key, int lev, const INode* parent) const {
            return load()->_ctrie_mn_erase(key, lev, parent, this);
        }
        
        EraseResult CNode::_ctrie_mn_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const {
            auto [flag, pos] = flagpos(key->_hash, lev, this->bmp);
            if (!(flag & this->bmp))
                // Key is not present, so postcondition is satisfied
                return EraseResult::NOTFOUND;
            EraseResult result = this->array[pos]->_ctrie_bn_erase(key, lev, i, this, pos, flag);
            if ((result == EraseResult::OK) && parent)
                // Check if the erasure left a TNode to clean up
                i->load()->_ctrie_mn_erase2(parent, i, key->_hash, lev - W);
            return result;
        }
        
        EraseResult TNode::_ctrie_mn_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const {
            if (parent)
                parent->clean(lev - W);
            return EraseResult::RESTART;
        }
        
        EraseResult LNode::_ctrie_mn_erase(const HeapString* key, int lev, const INode* parent, const INode* in) const {
            const LNode* head = this->copy_erase(key);
            if (head == this)
                return EraseResult::NOTFOUND;
            const MainNode* desired = head;
            if (!(head->next)) // if list contains only one element
                desired = new TNode(head->sn);
            // TODO: if EraseResult::OK should we try and clean up any tombstone?
            return (in->compare_exchange(this, desired)
                    ? EraseResult::OK
                    : EraseResult::RESTART);
        }
           
        
        EraseResult INode::_ctrie_bn_erase(const HeapString* key, int lev, const INode* i, const CNode* cn, int pos, uint64_t flag) const {
            return this->erase(key, lev + W, i);
        }
        
        
        
        void TNode::_ctrie_mn_erase2(const INode* parent, const INode* i, size_t hc, int lev) const {
            cleanParent(parent, i, hc, lev);
        }
        
        
        
        
        
        const CNode* CNode::make(const HeapString* hs1, const HeapString* hs2, int lev) {
            
            //TODO: LNode
            assert(lev < 60);
            
            if (hs2->_hash < hs1->_hash) {
                std::swap(hs1, hs2);
            }
            
            uint64_t pos1 = (hs1->_hash >> lev) & 63;
            uint64_t pos2 = (hs2->_hash >> lev) & 63;
            uint64_t flag1 = (uint64_t)1 << pos1;
            uint64_t flag2 = (uint64_t)1 << pos2;
            uint64_t bmp = flag1 | flag2;
            int num = __builtin_popcountll(bmp);
            
            CNode* ncn = new(num) CNode;
            ncn->bmp = bmp;
            switch (num) {
                case 1: {
                    ncn->array[0] = new INode(CNode::make(hs1, hs2, lev + W));
                    return ncn;
                }
                case 2: {
                    ncn->array[0] = hs1;
                    ncn->array[1] = hs2;
                    return ncn;
                }
                default:
                    abort();
            }
        }
        
        const CNode* CNode::copy_erase(int pos, uint64_t flag) const {
            assert(bmp & flag);
            int num = __builtin_popcountll(bmp);
            CNode* ncn = new (num-1) CNode;
            ncn->bmp = bmp ^ flag;
            const BranchNode** dest = ncn->array;
            for (int i = 0; i != num; ++i) {
                if (i != pos) {
                    object_shade(array[i]);
                    *dest++ = array[i];
                }
            }
            assert(dest == ncn->array+(num-1));
            return ncn;
        }
        
        const CNode* CNode::copy_insert(int pos, uint64_t flag, const BranchNode* bn) const {
            assert(!(bmp & flag));
            int num = __builtin_popcountll(bmp);
            CNode* ncn = new (num+1) CNode;
            ncn->bmp = bmp ^ flag;
            const BranchNode* const* src = array;
            for (int i = 0; i != num+1; ++i) {
                if (i != pos) {
                    ncn->array[i] = *src++;
                } else {
                    ncn->array[i] = bn;
                }
                object_shade(ncn->array[i]);
            }
            assert(src == array+num);
            return ncn;
        }
        
        INode::INode(const MainNode* mn)
        : main(mn) {
        }
        
        
        
        
        
        
        
        LNode::LNode() {
        }
        
        LNode::~LNode() {
            printf("~LNode\n");
        }
        
        
        const LNode* LNode::copy_erase(const LNode* victim) const {
            assert(victim);
            // Reuse any nodes after the victim
            const LNode* head = victim->next;
            // Copy any nodes before the victim
            for (const LNode* curr = this; curr != victim; curr = curr->next) {
                assert(curr); // <-- victim was not in the list!
                              // Make a copy
                LNode* a = new LNode;
                a->sn = curr->sn;
                // Push onto the list
                a->next = exchange(head, a);
            }
            return head;
        }
        
        

        const AnyNode* LNode::find_or_copy_emplace(Query query) const {
            
            // This function will either find the Class::STRING key and return it,
            // or emplace it in, and return, a new Class::CTRIE_LNODE list
            
            const HeapString* key = nullptr;
            const LNode* head = this;
            // find it
            for (const LNode* current = head; current; current = current->next) {
                key = current->sn;
                assert(key);
                if (query != key)
                    continue;
                
                // We found the key, but we must obtain a strong reference to it
                // before we can return it
                
                Color expected = Color::WHITE;
                key->color.compare_exchange(expected, Color::BLACK);
                switch (expected) {
                    case Color::WHITE:
                    case Color::BLACK: {
                        // We have a strong ref
                        return key;
                    }
                    case Color::RED: {
                        // Already condemned, we have to replace it
                        key = nullptr;
                        break;
                    }
                    case Color::GRAY:
                    default: {
                        // Impossible
                        object_debug(key);
                        abort();
                    }
                }
                
                // The collector has condemned, and will soon erase, the key.
                // We must race to install a new one.
                
                // Point head at a version of the list with the current node
                // removed
                head = this->copy_erase(current);
                
                break;
            }
            
            // The key didn't exist or was condemned
            //
            // Either way, "head" is now a list that does not contain the key
            // We must prepend a new key
            //
            // ("this", as the first node of the old version, will not be in the new
            // version, having been either copied or excluded)
            
            // Make the new HeapString
            key = make_HeapString_from_Query(query);
            
            LNode* node = new LNode;
            node->sn = key;
            node->next = head;
            
            // Send it up for CAS
            return node;
            
            // TODO: test this path
            // by breaking the hash function?
            
        }
        
        
        const LNode* LNode::copy_erase(const HeapString* key) const {
            // this should only be called by the garbage collector
            for (const LNode* current = this; current; current = current->next) {
                if (current->sn != key)
                    continue;
                // Found it
                // We should only be erasing nodes whose keys we have marked RED
                assert(key->color.load() == Color::RED);
                return this->copy_erase(current);
            }
            // Not present in the list
            return this;
        }
        
        
        
        
        
        void CNode::_object_scan() const {
            int num = __builtin_popcountll(bmp);
            for (int i = 0; i != num; ++i)
                object_trace_weak(array[i]);
        }
        
        void INode::_object_scan() const {
            main.trace();
        }
        
        void LNode::_object_scan() const {
            object_trace_weak(sn);
            object_trace(next);
        }
        void TNode::_object_scan() const {
            object_trace_weak(sn);
        }
        
        
        const MainNode* INode::load() const {
            return main.load(Ordering::ACQUIRE);
        }
        
        bool INode::compare_exchange(const MainNode* expected, const MainNode* desired) const {
            // Safety:
            //    We have already ACQUIRED the expected value
            return main.compare_exchange_strong(expected, desired, Ordering::RELEASE, Ordering::RELAXED);
        }
        
        
        
    } // namespace _ctrie
    
    using namespace _ctrie;

    Ctrie::~Ctrie() {
        printf("~Ctrie\n");
    }
    
    Ctrie::Ctrie() {
        CNode* ncn = new(0) CNode;
        ncn->bmp = 0;
        root = new INode(ncn);
    }
                    
    const HeapString* Ctrie::find_or_emplace(Query query) {
        for (;;) {
            const INode* r = this->root;
            const HeapString* result = r->find_or_emplace(query, 0, nullptr);
            if (result)
                return result;
        }
    }
    
    void Ctrie::erase(const HeapString* hs) {
        while (this->root->erase(hs, 0, nullptr) == EraseResult::RESTART)
            ;
    }

    void Ctrie::_object_scan() const {
        object_trace(root);
    }
    
    
    EraseResult HeapString::_ctrie_bn_erase(const HeapString* key,
                                      int lev,
                                      const INode* i,
                                      const CNode* cn,
                                      int pos,
                                      uint64_t flag) const {
        if (this != key)
            return EraseResult::NOTFOUND;
        return (i->compare_exchange(cn, cn->copy_erase(pos, flag)->to_contracted(lev))
                ? EraseResult::OK
                : EraseResult::RESTART);
    }
    
    const HeapString* HeapString::_ctrie_bn_find_or_emplace(Query query, int lev,
                                                          const INode* i,
                                                          const CNode* cn,
                                                          int pos) const {
        const MainNode* mn = cn;
        const MainNode* nmn;
        const HeapString* nhs;
        const BranchNode* bn = this;
        
        // We have hashed to the same bucket as an existing
        // string
        
        
        // Some of the hash bits match
        const HeapString* hs = (const HeapString*)bn;
        if (hs->_hash == query.hash) {
            // All the hash bits match
            if (hs->_size == query.view.size()) {
                // The sizes match
                if (!__builtin_memcmp(hs->_bytes, query.view.data(), query.view.size())) {
                    // The strings match
                    Color expected = Color::WHITE;
                    hs->color.compare_exchange(expected, Color::BLACK);
                    switch (expected) {
                        case Color::WHITE:
                        case Color::BLACK: {
                            // We have a strong ref
                            return hs;
                        }
                        case Color::RED: {
                            // Already condemned, we have to replace it
                            break;
                        }
                        case Color::GRAY:
                        default: {
                            // Impossible
                            object_debug(hs);
                            abort();
                        }
                    }
                    // was RED; now we have to replace the string
                    nhs = make_HeapString_from_Query(query);
                    nmn = cn->copy_assign(pos, nhs);
                    return i->compare_exchange(mn, nmn) ? nhs : nullptr;
                }
            }
        }
        // We have to expand the HAMT one level
        {
            nhs = make_HeapString_from_Query(query);
            INode* nbn = new INode(CNode::make(hs, nhs, lev + W));
            nmn = cn->copy_assign(pos, nbn);
            return i->compare_exchange(mn, nmn) ? nhs : nullptr;
        }
    }
    
    const MainNode* HeapString::_ctrie_bn_to_contracted(const CNode* cn) const {
        return new TNode(this);
    }
    
    const HeapString* HeapString::_ctrie_any_find_or_emplace2(const INode* in, const LNode* ln) const {
        return this;
    }
    
} // namespace wry::gc

