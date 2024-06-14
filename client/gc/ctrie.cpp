//
//  ctrie.cpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#include "ctrie.hpp"

namespace gc {
    
    std::pair<uint64_t, int> Ctrie::flagpos(uint64_t h, int lev, uint64_t bmp) {
        uint64_t a = (h >> lev) & 63;
        uint64_t flag = ((uint64_t)1 << a);
        int pos = __builtin_popcountll(bmp & (flag - 1));
        return {flag, pos};
    }
    
    Ctrie::MainNode* Ctrie::READ(Traced<Atomic<MainNode*>>& main) {
        return main.load(Order::ACQUIRE);
    }
    
    bool Ctrie::CAS(Traced<Atomic<MainNode*>>& main, MainNode* expected, MainNode* desired) {
        // Safety:
        //    We have already ACQUIRED the expected value
        return main.compare_exchange_strong(expected, desired, Order::RELEASE, Order::RELAXED);
    }

    
    
    Ctrie::Branch* Ctrie::Branch::resurrect() {
        switch (this->_class) {
            case CLASS_CTRIE_INODE: {
                INode* in = (INode*)this;
                MainNode* mn = READ(in->main);
                switch (mn->_class) {
                    case CLASS_CTRIE_TNODE: {
                        TNode* tn = (TNode*)mn;
                        return tn->sn;
                    }
                    default: {
                        return in;
                    }
                }
            }
            default: {
                return this;
            }
        }
    }
    
    Ctrie::SNode::SNode(Value k, Value v)
    : Branch(CLASS_CTRIE_SNODE)
    , key(k)
    , value(v) {
    }
    
    
    Ctrie::MainNode* Ctrie::CNode::toContracted(int level) {
        if (level == 0)
            return this;
        int num = __builtin_popcountll(this->bmp);
        if (num != 1)
            return this;
        Branch* bn = this->array[0];
        if (bn->_class != CLASS_CTRIE_SNODE)
            return this;
        SNode* sn = (SNode*)bn;
        return sn->entomb();
    }
    
    Ctrie::MainNode* Ctrie::CNode::toCompressed(int level) {
        return resurrected()->toContracted(level);
    }
    
    
    void Ctrie::INode::clean(int level) {
        MainNode* mn = READ(this->main);
        if (mn->_class == CLASS_CTRIE_CNODE) {
            CNode* cn = (CNode*)mn;
            CAS(this->main, mn, cn->toCompressed(level));
        }
    }
    
    
    void Ctrie::cleanParent(INode* p, INode* i, size_t hc, int lev) {
        MainNode* m = READ(i->main);
        MainNode* pm = READ(p->main);
        switch (pm->_class) {
            case CLASS_CTRIE_CNODE: {
                CNode* cn = (CNode*)pm;
                auto [flag, pos] = flagpos(hc, lev, cn->bmp);
                if (!(flag & cn->bmp))
                    return;
                Branch* sub = cn->array[pos];
                if (sub != i)
                    return;
                if (m->_class == CLASS_CTRIE_TNODE) {
                    TNode* tn = (TNode*)m;
                    CNode* ncn = cn->updated(pos, tn->sn);
                    if (!CAS(p->main, pm, ncn->toContracted(lev)))
                        [[clang::musttail]] return cleanParent(p, i, hc, lev);
                }
                return;
            }
            default: {
                return;
            }
        }
    }
    
    Ctrie::CNode* Ctrie::CNode::resurrected() {
        int num = __builtin_popcountll(this->bmp);
        CNode* ncn = new(num) CNode;
        ncn->bmp = this->bmp;
        for (int i = 0; i != num; ++i) {
            Branch* bn = this->array[i]->resurrect();
            object_shade(bn);
            ncn->array[i] = bn;
        }
        return ncn;
    }
    
    void* Ctrie::CNode::operator new(size_t fixed, size_t variable) {
        return object_allocate(fixed + variable * sizeof(Branch*));
    }
    
    Ctrie::CNode::CNode()
    : MainNode(CLASS_CTRIE_CNODE) {
    }
    
    Ctrie::CNode* Ctrie::CNode::updated(int pos, Branch *bn) {
        int num = __builtin_popcountll(this->bmp);
        CNode* ncn = new(num) CNode;
        ncn->bmp = this->bmp;
        for (int i = 0; i != num; ++i) {
            Branch* sub = (i == pos) ? bn : this->array[i];
            object_shade(sub);
            ncn->array[i] = sub;
        }
        return ncn;
    }
    
    Ctrie::TNode* Ctrie::SNode::entomb() {
        return new TNode(this);
    }
    
    Ctrie::TNode::TNode(SNode* sn) 
    : MainNode(CLASS_CTRIE_TNODE)
    , sn(sn) {
    }
        

    
    Value Ctrie::lookup(Value key) {
        for (;;) {
            INode* r = this->root;
            Value result = r->lookup(key, 0, nullptr);
            if (!value_is_RESTART(result))
                return result;
        }
    }
    
    void Ctrie::insert(Value k, Value v) {
        for (;;) {
            INode* r = this->root;
            if (r->insert(k, v, 0, nullptr))
                return;
        }
    }

    Value Ctrie::remove(Value k) {
        for (;;) {
            INode* r = this->root;
            Value res = r->remove(k, 0, nullptr);
            if (!value_is_RESTART(res))
                return res;
        }
        
    }
    
    
    
    
    
    
    Value Ctrie::INode::lookup(Value k, int lev, INode* parent) {
        INode* i = this;
        MainNode* mn = READ(i->main);
        switch (mn->_class) {
            case CLASS_CTRIE_CNODE: {
                CNode* cn = (CNode*)mn;
                auto [flag, pos] = flagpos(value_hash(k), lev, cn->bmp);
                if (!(flag & cn->bmp))
                    return value_make_NOTFOUND();
                Branch* bn = cn->array[pos];
                switch (bn->_class) {
                    case CLASS_CTRIE_INODE: {
                        INode* sin = (INode*)bn;
                        return sin->lookup(k, lev + W, i);
                    }
                    case CLASS_CTRIE_SNODE: {
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
            case CLASS_CTRIE_TNODE: {
                parent->clean(lev - W);
                return value_make_RESTART();
            }
            case CLASS_CTRIE_LNODE: {
                LNode* ln = (LNode*)mn;
                return ln->lookup(k);
            }
            default:
                abort();
        } // switch (mn->_class)
    }
    
    
    
    
    
    
    
    bool Ctrie::INode::insert(Value k, Value v, int lev, INode* parent) {
        INode* i = this;
        MainNode* mn = READ(i->main);
        MainNode* nmn;
        switch (mn->_class) {
            case CLASS_CTRIE_CNODE: {
                CNode* cn = (CNode*)mn;
                auto [flag, pos] = flagpos(value_hash(k), lev, cn->bmp);
                printf("{%llx, %d}\n", flag, pos);
                if (!(cn->bmp & flag)) {
                    nmn = cn->inserted(pos, flag, new SNode(k, v));
                    break;
                }
                Branch* bn = cn->array[pos];
                switch (bn->_class) {
                    case CLASS_CTRIE_INODE: {
                        INode* sin = (INode*)bn;
                        return sin->insert(k, v, lev + W, i);
                    }
                    case CLASS_CTRIE_SNODE: {
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
            case CLASS_CTRIE_TNODE: {
                parent->clean(lev - W);
                return false;
            }
            case CLASS_CTRIE_LNODE: {
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
    
    
    
    
    Value Ctrie::INode::remove(Value k, int lev, INode* parent) {
        INode* i = this;
        MainNode* mn = i->main.load(Order::ACQUIRE);
        switch (mn->_class) {
            case CLASS_CTRIE_CNODE: {
                CNode* cn = (CNode*)mn;
                auto [flag, pos] = flagpos(value_hash(k), lev, cn->bmp);
                if (!(flag & cn->bmp))
                    return value_make_NOTFOUND();
                Value res;
                Branch* bn = cn->array[pos];
                switch (bn->_class) {
                    case CLASS_CTRIE_INODE: {
                        INode* sin = (INode*)bn;
                        res = sin->remove(k, lev + W, i);
                        break;
                    }
                    case CLASS_CTRIE_SNODE: {
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
                if (mn->_class == CLASS_CTRIE_TNODE)
                    cleanParent(parent, i, value_hash(k), lev - W);
                return res;
            }
            case CLASS_CTRIE_TNODE: {
                parent->clean(lev - W);
                return value_make_RESTART();
            }
            case CLASS_CTRIE_LNODE: {
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
    
    
    Ctrie::CNode* Ctrie::CNode::make(SNode* sn1, SNode* sn2, int lev) {
        
        //TODO: LNode
        assert(lev < 60);
        
        uint64_t hc1 = value_hash(sn1->key);
        uint64_t hc2 = value_hash(sn2->key);
        if (hc2 < hc1) {
            std::swap(sn1, sn2);
            std::swap(hc1, hc2);
        }
            
        uint64_t pos1 = (hc1 >> lev) & 63;
        uint64_t pos2 = (hc2 >> lev) & 63;
        uint64_t flag1 = (uint64_t)1 << pos1;
        uint64_t flag2 = (uint64_t)1 << pos2;
        uint64_t bmp = flag1 | flag2;
        int num = __builtin_popcountll(bmp);

        CNode* ncn = new(num) CNode;
        ncn->bmp = bmp;
        switch (num) {
            case 1: {
                ncn->array[0] = new INode(CNode::make(sn1, sn2, lev + W));
                return ncn;
            }
            case 2: {
                ncn->array[0] = sn1;
                ncn->array[1] = sn2;
                return ncn;
            }
            default:
                abort();
        }
    }
    
    Ctrie::CNode* Ctrie::CNode::removed(int pos, uint64_t flag) {
        assert((flag >> pos) == 1);
        assert(bmp & flag);
        int num = __builtin_popcountll(bmp);
        CNode* ncn = new (num-1) CNode;
        ncn->bmp = bmp ^ flag;
        Branch** dest = ncn->array;
        for (int i = 0; i != num; ++i) {
            if (i != pos) {
                object_shade(array[i]);
                *dest++ = array[i];
            }
        }
        assert(dest == ncn->array+(num-1));
        return ncn;
    }
    
    Ctrie::CNode* Ctrie::CNode::inserted(int pos, uint64_t flag, Branch* bn) {
        assert(!(bmp & flag));
        int num = __builtin_popcountll(bmp);
        CNode* ncn = new (num+1) CNode;
        ncn->bmp = bmp ^ flag;
        Branch** src = array;
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
    : Branch(CLASS_CTRIE_INODE)
    , main(mn) {     
        
    }
    
    Value Ctrie::LNode::lookup(Value key) {
        abort();
    }
    
    Ctrie::LNode* Ctrie::LNode::inserted(Value key, Value value) {
        abort();
    }

    Ctrie::LNode* Ctrie::LNode::removed(Value key) {
        abort();
    }
    
    
    Ctrie::Ctrie()
    : Object(CLASS_CTRIE) {
        CNode* ncn = new(0) CNode;
        ncn->bmp = 0;
        root = new INode(ncn);
    }

    
} // namespace gc


