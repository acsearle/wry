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
    
    bool operator==(const Query& left, const HeapString* right) {
        size_t n = left.view.size();
        return ((left.hash == right->_hash)
                && (n == right->_size)
                && !__builtin_memcmp(left.view.data(), right->_bytes, n)
                );
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
            
    constexpr int W = 6;
    
    std::pair<uint64_t, int> Ctrie::flagpos(uint64_t hash, int lev, uint64_t bmp) {
        uint64_t a = (hash >> lev) & 63;
        uint64_t flag = ((uint64_t)1 << a);
        int pos = __builtin_popcountll(bmp & (flag - 1));
        return {flag, pos};
    }
    
    const MainNode* Ctrie::READ(const Traced<Atomic<const MainNode*>>& main) {
        return main.load(Ordering::ACQUIRE);
    }
    
    bool Ctrie::CAS(Traced<Atomic<const MainNode*>>& main, const MainNode* expected, const MainNode* desired) {
        // Safety:
        //    We have already ACQUIRED the expected value
        return main.compare_exchange_strong(expected, desired, Ordering::RELEASE, Ordering::RELAXED);
    }

    const BranchNode* INode::_ctrie_resurrect() const {
        const MainNode* mn = Ctrie::READ(main);
        return mn->_ctrie_resurrect2(this);
    }

    const BranchNode* MainNode::_ctrie_resurrect2(const INode* i) const {
        return i;
    }

    const BranchNode* TNode::_ctrie_resurrect2(const INode*) const {
        return sn;
    }


    const BranchNode* BranchNode::_ctrie_resurrect() const {
        return this;
    }
    
    const MainNode* CNode::toContracted(int level) const {
        if (level == 0)
            return this;
        int num = __builtin_popcountll(this->bmp);
        if (num != 1)
            return this;
        const BranchNode* bn = this->array[0];
        return bn->_ctrie_toContracted(this);
    }
    
    const MainNode* INode::_ctrie_toContracted(const MainNode* up) const {
        return up;
    }
    
    const MainNode* HeapString::_ctrie_toContracted(const MainNode* up) const {
        return new TNode(this);
    }
    
    
    
    const MainNode* CNode::toCompressed(int level) const {
        return resurrected()->toContracted(level);
    }
        
    void INode::clean(int level) const {
        const MainNode* mn = Ctrie::READ(this->main);
        mn->_ctrie_clean(level, this);
    }
    
    void CNode::_ctrie_clean(int level, const INode* parent) const {
        Ctrie::CAS(parent->main, this, this->toCompressed(level));
    }
    
    void Ctrie::cleanParent(const INode* p, const INode* i, size_t hc, int lev) {
        const MainNode* m = READ(i->main);
        const MainNode* pm = READ(p->main);
        pm->_ctrie_cleanParent(p, i, hc, lev, m);
    }
    
    void MainNode::_ctrie_cleanParent(const INode* p, const INode* i, size_t hc, int lev, const MainNode* m) const {
        // noop
    }

    
    void CNode::_ctrie_cleanParent(const INode* p, const INode* i, size_t hc, int lev, const MainNode* m) const {
        auto [flag, pos] = Ctrie::flagpos(hc, lev, bmp);
        if (!(flag & bmp))
            return;
        const Object* sub = array[pos];
        if (sub != i)
            return;
        m->_ctrie_cleanParent2(p, i, hc, lev, this, pos);
    }
     
    void TNode::_ctrie_cleanParent2(const INode* p, const INode* i, size_t hc, int lev, const CNode* cn, int pos) const {
        const CNode* ncn = cn->updated(pos, sn);
        const MainNode* pm = cn;
        if (!Ctrie::CAS(p->main, pm, ncn->toContracted(lev)))
            // TODO: restart this porperly rather than explode the stack
            return Ctrie::cleanParent(p, i, hc, lev);
    }
    
    const CNode* CNode::resurrected() const {
        int num = __builtin_popcountll(this->bmp);
        CNode* ncn = new(num) CNode;
        ncn->bmp = this->bmp;
        for (int i = 0; i != num; ++i) {
            const BranchNode* bn = this->array[i]->_ctrie_resurrect();
            object_shade(bn);
            ncn->array[i] = bn;
        }
        return ncn;
    }
    
    void* CNode::operator new(size_t self, size_t entries) {
        return Object::operator new(self + entries * sizeof(Object*));
    }
    
    CNode::CNode() {
    }
    
    const CNode* CNode::updated(int pos, const BranchNode *bn) const {
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
        
    TNode::TNode(const HeapString* sn)
    : sn(sn) {
    }
        

    
    /*
    Value Ctrie::lookup(Value key) {
        for (;;) {
            INode* r = this->root;
            Value result = r->lookup(key, 0, nullptr);
            if (!value_is_RESTART(result))
                return result;
        }
    }
     */
    
    /*
    void Ctrie::insert(Value k, Value v) {
        for (;;) {
            INode* r = this->root;
            if (r->insert(k, v, 0, nullptr))
                return;
        }
    }*/

    const HeapString* Ctrie::find_or_emplace(Query query) {
        for (;;) {
            const INode* r = this->root;
            const HeapString* result = r->find_or_emplace(query, 0, nullptr);
            if (result)
                return result;
        }
    }

    /*
    Value Ctrie::remove(Value k) {
        for (;;) {
            INode* r = this->root;
            Value res = r->remove(k, 0, nullptr);
            if (!value_is_RESTART(res))
                return res;
        }
    } */
    
    void Ctrie::erase(const HeapString* hs) {
        for (;;) {
            const INode* r = this->root;
            Value flag = r->erase(hs, 0, nullptr);
            if (flag != value_make_RESTART())
                return;
        }
    }
    
    
    
    
    
    /*
    Value Ctrie::INode::lookup(Value k, int lev, INode* parent) {
        INode* i = this;
        MainNode* mn = READ(i->main);
        switch (mn->_class) {
            case Class::CTRIE_CNODE: {
                CNode* cn = (CNode*)mn;
                auto [flag, pos] = flagpos(value_hash(k), lev, cn->bmp);
                if (!(flag & cn->bmp))
                    return value_make_NOTFOUND();
                Branch* bn = cn->array[pos];
                switch (bn->_class) {
                    case Class::CTRIE_INODE: {
                        INode* sin = (INode*)bn;
                        return sin->lookup(k, lev + W, i);
                    }
                    case Class::CTRIE_SNODE: {
                        SNode* sn = (SNode*)bn;
                        if (sn->key == k)
                            return sn->value;
                        else
                            return value_make_NOTFOUND();
                    }
                    default:
                        abort();
                }
            }
            case Class::CTRIE_TNODE: {
                parent->clean(lev - W);
                return value_make_RESTART();
            }
            case Class::CTRIE_LNODE: {
                LNode* ln = (LNode*)mn;
                return ln->lookup(k);
            }
            default:
                abort();
        } // switch (mn->_class)
    }
    */
    
    
    
    
    
    /*
    bool Ctrie::INode::insert(Value k, Value v, int lev, INode* parent) {
        INode* i = this;
        MainNode* mn = READ(i->main);
        MainNode* nmn;
        switch (mn->_class) {
            case Class::CTRIE_CNODE: {
                CNode* cn = (CNode*)mn;
                auto [flag, pos] = flagpos(value_hash(k), lev, cn->bmp);
                if (!(cn->bmp & flag)) {
                    nmn = cn->inserted(pos, flag, new SNode(k, v));
                    break;
                }
                Branch* bn = cn->array[pos];
                switch (bn->_class) {
                    case Class::CTRIE_INODE: {
                        INode* sin = (INode*)bn;
                        return sin->insert(k, v, lev + W, i);
                    }
                    case Class::CTRIE_SNODE: {
                        SNode* sn = (SNode*)bn;
                        SNode* nsn = new SNode(k, v);
                        Branch* nbn = nsn;
                        if (sn->key != k)
                            nbn = new INode(CNode::make(sn, nsn, lev + W));
                        nmn = cn->updated(pos, nbn);
                        break;
                    }
                    default: {
                        abort();
                    }
                }
                break;
            }
            case Class::CTRIE_TNODE: {
                parent->clean(lev - W);
                return false;
            }
            case Class::CTRIE_LNODE: {
                LNode* ln = (LNode*)mn;
                nmn = ln->inserted(k, v);
                break;
            }
            default: {
                abort();
            }
        }
        return CAS(i->main, mn, nmn);
    }
     */
    
    const HeapString* INode::find_or_emplace(Query query, int lev, const INode* parent) const {
        return Ctrie::READ(main)->_ctrie_find_or_emplace(query, lev, parent, this);
    }
    
    const HeapString* CNode::_ctrie_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const {
        const CNode* cn = this;
        const MainNode* mn = this;
        const MainNode* nmn;
        const HeapString* nhs;
        auto [flag, pos] = Ctrie::flagpos(query.hash, lev, cn->bmp);
        if (!(cn->bmp & flag)) {
            nhs = make_HeapString_from_Query(query);
            nmn = cn->inserted(pos, flag, nhs);
            return Ctrie::CAS(i->main, mn, nmn) ? nhs : nullptr;
        }
        const BranchNode* bn = cn->array[pos];
        return bn->_ctrie_find_or_emplace2(query, lev, parent, i, cn, pos);
    }
    
    const HeapString* TNode::_ctrie_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const {
        if (parent)
            parent->clean(lev - W);
        return nullptr;
    }
    
    const HeapString* LNode::_ctrie_find_or_emplace(Query query, int lev, const INode* parent, const INode* i) const {
        const MainNode* mn = this;
        const MainNode* nmn;
        const HeapString* nhs;
        // nmn = ln->inserted(k, v);
        // ...
        abort();
        return Ctrie::CAS(i->main, mn, nmn) ? nhs : nullptr;
    }


    
    const HeapString* INode::_ctrie_find_or_emplace2(Query query, int lev,
                                                     const INode* parent, 
                                                     const INode* i,
                                                     const CNode* cn,
                                                     int pos) const {
        return find_or_emplace(query, lev + W, i);
    }
    
    const HeapString* HeapString::_ctrie_find_or_emplace2(Query query, int lev, 
                                                          const INode* parent,
                                                          const INode* i,
                                                          const CNode* cn,
                                                          int pos) const {
        const MainNode* mn = cn;
        const MainNode* nmn;
        const HeapString* nhs;
        const Object* bn = this;

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
                    nmn = cn->updated(pos, nhs);
                    return Ctrie::CAS(i->main, mn, nmn) ? nhs : nullptr;
                }
            }
        }
        // We have to expand the HAMT one level
        {
            nhs = make_HeapString_from_Query(query);
            INode* nbn = new INode(CNode::make(hs, nhs, lev + W));
            nmn = cn->updated(pos, nbn);
            return Ctrie::CAS(i->main, mn, nmn) ? nhs : nullptr;
        }
    }
    
    
    
    
    Value INode::erase(const HeapString* key, int lev, const INode* parent) const {
        const INode* i = this;
        const MainNode* mn = i->main.load(Ordering::ACQUIRE);
        return mn->_ctrie_erase(key, lev, parent, i);
    }
    
    void TNode::_ctrie_cleanParent3(const INode* parent, const INode* i, size_t hc, int lev) const {
        Ctrie::cleanParent(parent, i, hc, lev - W);
    }
    
    Value CNode::_ctrie_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const {
        const MainNode* mn = this;
        const CNode* cn = this;
        auto [flag, pos] = Ctrie::flagpos(key->_hash, lev, cn->bmp);
        if (!(flag & cn->bmp))
            // Key is not present; postcondition is satisfied
            return value_make_OK();
        Value res;
        const BranchNode* bn = cn->array[pos];
        res = bn->_ctrie_erase2(key, lev, parent, i, this, pos, flag);
        if (value_is_NOTFOUND(res) || value_is_RESTART(res))
            return res;
        // We have modified the Ctrie, possibly making TNodes at lower
        // levels that mean we may need to contract this level.
        if (parent) {
            mn = Ctrie::READ(i->main);
            mn->_ctrie_cleanParent3(parent, i, key->_hash, lev - W);
        }
        return res;
    }
    
    Value TNode::_ctrie_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const {
        if (parent)
            parent->clean(lev - W);
        return value_make_RESTART();
    }

    Value LNode::_ctrie_erase(const HeapString* key, int lev, const INode* parent, const INode* i) const {
        const MainNode* mn = this;
        const LNode* ln = this;
        const LNode* nln = ln->erase(key);
        assert(nln); // <-- any published LNode list should have had at least two nodes
        if (nln == ln) {
            // Erasure did not change the list == the key was not found
            return value_make_NOTFOUND();
        }
        // Install the new list head
        const MainNode* nmn = nln;
        if (!nln->next) {
            // The new list has only one element, so instead of installing
            // it, we put its HashString (not the one we just erased)
            // into a TNode
            nmn = new TNode(nln->sn);
        }
        if (Ctrie::CAS(i->main, mn, nmn)) {
            return value_make_OK();
        } else {
            return value_make_RESTART();
        }
    }
    
    Value INode::_ctrie_erase2(const HeapString* key, int lev, const INode* parent, const INode* i, const CNode* cn, int pos, uint64_t flag) const {
        return this->erase(key, lev + W, i);
    }
        
    Value HeapString::_ctrie_erase2(const HeapString* key, int lev, const INode* parent, const INode* i, const CNode* cn, int pos, uint64_t flag) const {
        Value res;
        const HeapString* bn = this;
        const HeapString* dhs = (const HeapString*)bn;
        const MainNode* mn = cn;
        if (dhs != key) {
            res = value_make_NOTFOUND();
        } else {
            const CNode* ncn = cn->removed(pos, flag);
            const MainNode* cntr = ncn->toContracted(lev);
            if (Ctrie::CAS(i->main, mn, cntr)) {
                res = value_make_OK();
            } else {
                res = value_make_RESTART();
            }
        }
        return res;
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
    
    const CNode* CNode::removed(int pos, uint64_t flag) const {
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
    
    const CNode* CNode::inserted(int pos, uint64_t flag, const BranchNode* bn) const {
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
    
    /*
    Value Ctrie::LNode::lookup(Value key) {
        abort();
    }
    
    Ctrie::LNode* Ctrie::LNode::inserted(Value key, Value value) {
        abort();
    }

    Ctrie::LNode* Ctrie::LNode::removed(Value key) {
        abort();
    }
     */
    
    
    Ctrie::Ctrie() {
        CNode* ncn = new(0) CNode;
        ncn->bmp = 0;
        root = new INode(ncn);
    }
    
    
    LNode::LNode() {
    }
    
    
    const LNode* LNode::removed(const LNode* victim) const {
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

    const Object* LNode::find_or_emplace(Query query) const {
        
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
            head = this->removed(current);
            
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
    
    
    const LNode* LNode::erase(const HeapString* key) const {
        // this should only be called by the garbage collector
        for (const LNode* current = this; current; current = current->next) {
            if (current->sn != key)
                continue;
            // Found it
            // We should only be erasing nodes whose keys we have marked RED
            assert(key->color.load() == Color::RED);
            return this->removed(current);
        }
        // Not present in the list
        return this;
    }
    
    
    

    
    void CNode::_object_scan() const {
        int num = __builtin_popcountll(bmp);
        for (int i = 0; i != num; ++i)
            object_trace_weak(array[i]);
    }
    
    void Ctrie::_object_scan() const {
        object_trace(root);
    }
    
    void INode::_object_scan() const {
        main.trace();
    }
    
    void LNode::_object_scan() const {
        object_trace(sn);
        object_trace(next);
    }
    void TNode::_object_scan() const {
        object_trace(sn);
    }


    
} // namespace wry::gc


