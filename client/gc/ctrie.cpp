//
//  ctrie.cpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#include "utility.hpp"
#include "ctrie.hpp"

namespace wry::gc {
    
    bool operator==(const Ctrie::Query& left, const HeapString* right) {
        size_t n = left.view.size();
        return ((left.hash == right->_hash)
                && (n == right->_size)
                && !__builtin_memcmp(left.view.data(), right->_bytes, n)
                );
    }
    
    HeapString* make_HeapString_from_Query(Ctrie::Query query) {
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
    
    Ctrie::MainNode* Ctrie::READ(Traced<Atomic<MainNode*>>& main) {
        return main.load(Ordering::ACQUIRE);
    }
    
    bool Ctrie::CAS(Traced<Atomic<MainNode*>>& main, MainNode* expected, MainNode* desired) {
        // Safety:
        //    We have already ACQUIRED the expected value
        return main.compare_exchange_strong(expected, desired, Ordering::RELEASE, Ordering::RELAXED);
    }

    Object* Ctrie::object_resurrect(Object* self) {
        switch (self->_class) {
            case Class::CTRIE_INODE: {
                INode* in = (INode*)self;
                MainNode* mn = READ(in->main);
                switch (mn->_class) {
                    case Class::CTRIE_TNODE: {
                        TNode* tn = (TNode*)mn;
                        return tn->sn;
                    }
                    default: {
                        return self;
                    }
                }
            }
            case Class::STRING: {
                return self;
            }
            default: {
                abort();
            }
        }
    }
    
    Ctrie::MainNode* Ctrie::CNode::toContracted(int level) {
        if (level == 0)
            return this;
        int num = __builtin_popcountll(this->bmp);
        if (num != 1)
            return this;
        Object* bn = this->array[0];
        switch (bn->_class) {
            case Class::CTRIE_INODE: {
                return this;
            }
            case Class::STRING: {
                HeapString* hs = (HeapString*)bn;
                return new TNode(hs);
            }
            default: {
                abort();
            }
        }
    }
    
    Ctrie::MainNode* Ctrie::CNode::toCompressed(int level) {
        return resurrected()->toContracted(level);
    }
        
    void Ctrie::INode::clean(int level) {
        MainNode* mn = READ(this->main);
        if (mn->_class == Class::CTRIE_CNODE) {
            CNode* cn = (CNode*)mn;
            CAS(this->main, mn, cn->toCompressed(level));
        }
    }
    
    void Ctrie::cleanParent(INode* p, INode* i, size_t hc, int lev) {
        MainNode* m = READ(i->main);
        MainNode* pm = READ(p->main);
        if (pm->_class != Class::CTRIE_CNODE)
            return;
        CNode* cn = (CNode*)pm;
        auto [flag, pos] = flagpos(hc, lev, cn->bmp);
        if (!(flag & cn->bmp))
            return;
        Object* sub = cn->array[pos];
        if (sub != i)
            return;
        if (m->_class != Class::CTRIE_TNODE)
            return;
        TNode* tn = (TNode*)m;
        CNode* ncn = cn->updated(pos, tn->sn);
        if (!CAS(p->main, pm, ncn->toContracted(lev)))
            [[clang::musttail]] return cleanParent(p, i, hc, lev);
    }
    
    Ctrie::CNode* Ctrie::CNode::resurrected() {
        int num = __builtin_popcountll(this->bmp);
        CNode* ncn = new(num) CNode;
        ncn->bmp = this->bmp;
        for (int i = 0; i != num; ++i) {
            Object* bn = object_resurrect(this->array[i]);
            object_shade(bn);
            ncn->array[i] = bn;
        }
        return ncn;
    }
    
    void* Ctrie::CNode::operator new(size_t self, size_t entries) {
        return Object::operator new(self + entries * sizeof(Object*));
    }
    
    Ctrie::CNode::CNode()
    : MainNode(Class::CTRIE_CNODE) {
    }
    
    Ctrie::CNode* Ctrie::CNode::updated(int pos, Object *bn) {
        int num = __builtin_popcountll(this->bmp);
        CNode* ncn = new(num) CNode;
        ncn->bmp = this->bmp;
        for (int i = 0; i != num; ++i) {
            Object* sub = (i == pos) ? bn : this->array[i];
            object_shade(sub);
            ncn->array[i] = sub;
        }
        return ncn;
    }
        
    Ctrie::TNode::TNode(HeapString* sn)
    : MainNode(Class::CTRIE_TNODE)
    , sn(sn) {
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

    HeapString* Ctrie::find_or_emplace(Ctrie::Query query) {
        for (;;) {
            INode* r = this->root;
            HeapString* result = r->find_or_emplace(query, 0, nullptr);
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
    
    void Ctrie::erase(HeapString* hs) {
        for (;;) {
            INode* r = this->root;
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
    
    HeapString* Ctrie::INode::find_or_emplace(Ctrie::Query query, int lev, INode* parent) {
        INode* i = this;
        MainNode* mn = READ(i->main);
        MainNode* nmn;
        HeapString* nhs;
        switch (mn->_class) {
            case Class::CTRIE_CNODE: {
                CNode* cn = (CNode*)mn;
                auto [flag, pos] = flagpos(query.hash, lev, cn->bmp);
                if (!(cn->bmp & flag)) {
                    nhs = make_HeapString_from_Query(query);
                    nmn = cn->inserted(pos, flag, nhs);
                    break;
                }
                Object* bn = cn->array[pos];
                switch (bn->_class) {
                    case Class::CTRIE_INODE: {
                        INode* sin = (INode*)bn;
                        return sin->find_or_emplace(query, lev + W, i);
                    }
                    case Class::STRING: {
                        // We have hashed to the same bucket as an existing
                        // string
                        
                        // Some of the hash bits match
                        HeapString* hs = (HeapString*)bn;
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
                                    
                                    // now we have to replace the thing:
                                    nhs = make_HeapString_from_Query(query);
                                    nmn = cn->updated(pos, nhs);
                                    break;
                                }
                            }
                        }
                        // We have to expand the HAMT one level
                        {
                            nhs = make_HeapString_from_Query(query);
                            INode* nbn = new INode(CNode::make(hs, nhs, lev + W));
                            nmn = cn->updated(pos, nbn);
                            break;
                        }
                    }
                    default: {
                        abort();
                    }
                }
                break;
            }
            case Class::CTRIE_TNODE: {
                if (parent)
                    parent->clean(lev - W);
                return nullptr;
            }
            case Class::CTRIE_LNODE: {
                LNode* ln = (LNode*)mn;
                abort();
                // TODO: ln->find_or_emplace
                // nmn = ln->inserted(k, v);
                break;
            }
            default: {
                abort();
            }
        }
        return CAS(i->main, mn, nmn) ? nhs : nullptr;
    }
    
    /*
    Value Ctrie::INode::remove(Value k, int lev, INode* parent) {
        INode* i = this;
        MainNode* mn = i->main.load(Order::ACQUIRE);
        switch (mn->_class) {
            case Class::CTRIE_CNODE: {
                CNode* cn = (CNode*)mn;
                auto [flag, pos] = flagpos(value_hash(k), lev, cn->bmp);
                if (!(flag & cn->bmp))
                    return value_make_NOTFOUND();
                Value res;
                Branch* bn = cn->array[pos];
                switch (bn->_class) {
                    case Class::CTRIE_INODE: {
                        INode* sin = (INode*)bn;
                        res = sin->remove(k, lev + W, i);
                        break;
                    }
                    case Class::CTRIE_SNODE: {
                        SNode* sn = (SNode*)bn;
                        if (sn->key != k) {
                            res = value_make_NOTFOUND();
                        } else {
                            CNode* ncn = cn->removed(pos, flag);
                            MainNode* cntr = ncn->toContracted(lev);
                            if (CAS(i->main, mn, cntr)) {
                                res = sn->value;
                            } else {
                                res = value_make_RESTART();
                            }
                        }
                        break;
                    }
                    default: {
                        abort();
                    }
                }
                if (value_is_NOTFOUND(res) || value_is_RESTART(res))
                    return res;
                mn = READ(i->main);
                if (mn->_class == Class::CTRIE_TNODE)
                    cleanParent(parent, i, value_hash(k), lev - W);
                return res;
            }
            case Class::CTRIE_TNODE: {
                parent->clean(lev - W);
                return value_make_RESTART();
            }
            case Class::CTRIE_LNODE: {
                LNode* ln = (LNode*)mn;
                LNode* nln = ln->removed(k);
                MainNode* nmn = nln;
                if (nln->next == nullptr) {
                    nmn = nln->sn->entomb();
                }
                if (CAS(i->main, mn, nmn)) {
                    return ln->lookup(k);
                } else {
                    return value_make_RESTART();
                }
            }
            default:
                abort();
        } // switch (mn->_class)
    }
     */
    
    Value Ctrie::INode::erase(HeapString* key, int lev, INode* parent) {
        INode* i = this;
        MainNode* mn = i->main.load(Ordering::ACQUIRE);
        switch (mn->_class) {
            case Class::CTRIE_CNODE: {
                CNode* cn = (CNode*)mn;
                auto [flag, pos] = flagpos(key->_hash, lev, cn->bmp);
                if (!(flag & cn->bmp))
                    // Key is not present; postcondition is satisfied
                    return value_make_OK();
                Value res;
                Object* bn = cn->array[pos];
                switch (bn->_class) {
                    case Class::CTRIE_INODE: {
                        INode* sin = (INode*)bn;
                        res = sin->erase(key, lev + W, i);
                        break;
                    }
                    case Class::STRING: {
                        HeapString* dhs = (HeapString*)bn;
                        if (dhs != key) {
                            res = value_make_NOTFOUND();
                        } else {
                            CNode* ncn = cn->removed(pos, flag);
                            MainNode* cntr = ncn->toContracted(lev);
                            if (CAS(i->main, mn, cntr)) {
                                res = value_make_OK();
                            } else {
                                res = value_make_RESTART();
                            }
                        }
                        break;
                    }
                    default: {
                        abort();
                    }
                }
                if (value_is_NOTFOUND(res) || value_is_RESTART(res))
                    return res;
                // We have modified the Ctrie, possibly making TNodes at lower
                // levels that mean we may need to contract this level.
                if (parent) {
                    mn = READ(i->main);
                    if (mn->_class == Class::CTRIE_TNODE)
                        cleanParent(parent, i, key->_hash, lev - W);
                }
                return res;
            }
            case Class::CTRIE_TNODE: {
                if (parent)
                    parent->clean(lev - W);
                return value_make_RESTART();
            }
            case Class::CTRIE_LNODE: {
                LNode* ln = (LNode*)mn;
                LNode* nln = ln->erase(key);
                assert(nln); // <-- any published LNode list should have had at least two nodes
                if (nln == ln) {
                    // Erasure did not change the list == the key was not found
                    return value_make_NOTFOUND();
                }
                MainNode* nmn = nln;
                if (!nln->next) {
                    // The list has only one element, so instead of intstalling
                    // it, we put its HashString (not the one we just erased)
                    // into a TNode
                    nmn = new TNode(nln->sn);
                    if (CAS(i->main, mn, nmn)) {
                        return value_make_OK();
                    } else {
                        return value_make_RESTART();
                    }
                }
            }
            default:
                abort();
        } // switch (mn->_class)   
    }
    
    Ctrie::CNode* Ctrie::CNode::make(HeapString* hs1, HeapString* hs2, int lev) {
        
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
    
    Ctrie::CNode* Ctrie::CNode::removed(int pos, uint64_t flag) {
        assert(bmp & flag);
        int num = __builtin_popcountll(bmp);
        CNode* ncn = new (num-1) CNode;
        ncn->bmp = bmp ^ flag;
        Object** dest = ncn->array;
        for (int i = 0; i != num; ++i) {
            if (i != pos) {
                object_shade(array[i]);
                *dest++ = array[i];
            }
        }
        assert(dest == ncn->array+(num-1));
        return ncn;
    }
    
    Ctrie::CNode* Ctrie::CNode::inserted(int pos, uint64_t flag, Object* bn) {
        assert(!(bmp & flag));
        int num = __builtin_popcountll(bmp);
        CNode* ncn = new (num+1) CNode;
        ncn->bmp = bmp ^ flag;
        Object** src = array;
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
    
    Ctrie::INode::INode(MainNode* mn)
    : Object(Class::CTRIE_INODE)
    , main(mn) {     
        
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
    
    
    Ctrie::Ctrie()
    : Object(Class::CTRIE) {
        CNode* ncn = new(0) CNode;
        ncn->bmp = 0;
        root = new INode(ncn);
    }
    
    
    Ctrie::LNode::LNode()
    : MainNode(Class::CTRIE_LNODE) {
    }
    
    
    Ctrie::LNode* Ctrie::LNode::removed(LNode* victim) {
        assert(victim);
        // Reuse any nodes after the victim
        LNode* head = victim->next;
        // Copy any nodes before the victim
        for (LNode* curr = this; curr != victim; curr = curr->next) {
            assert(curr); // <-- victim was not in the list!
            // Make a copy
            LNode* a = new LNode;
            a->sn = curr->sn;
            // Push onto the list
            a->next = exchange(head, a);
        }
        return head;
    }

    Object* Ctrie::LNode::find_or_emplace(Query query) {
        
        // This function will either find the Class::STRING key and return it,
        // or emplace it in, and return, a new Class::CTRIE_LNODE list
        
        HeapString* key = nullptr;
        LNode* head = this;
        // find it
        for (LNode* current = head; current; current = current->next) {
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
        head->sn = key;
        node->next = head;

        // Send it up for CAS
        return node;
        
        // TODO: test this path
        // by breaking the hash function?
        
    }
    
    
    Ctrie::LNode* Ctrie::LNode::erase(HeapString* key) {
        // this should only be called by the garbage collector
        for (LNode* current = this; current; current = current->next) {
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
    
} // namespace wry::gc


