//
//  ctrie.cpp
//  client
//
//  Created by Antony Searle on 14/6/2024.
//

#include "ctrie.hpp"

namespace gc {
            
    constexpr int W = 6;
    
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

    Object* Ctrie::object_resurrect(Object* self) {
        switch (self->_class) {
            case CLASS_CTRIE_INODE: {
                INode* in = (INode*)self;
                MainNode* mn = READ(in->main);
                switch (mn->_class) {
                    case CLASS_CTRIE_TNODE: {
                        TNode* tn = (TNode*)mn;
                        return tn->sn;
                    }
                    default: {
                        return self;
                    }
                }
            }
            case CLASS_STRING: {
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
            case CLASS_CTRIE_INODE: {
                return this;
            }
            case CLASS_STRING: {
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
        if (mn->_class == CLASS_CTRIE_CNODE) {
            CNode* cn = (CNode*)mn;
            CAS(this->main, mn, cn->toCompressed(level));
        }
    }
    
    void Ctrie::cleanParent(INode* p, INode* i, size_t hc, int lev) {
        MainNode* m = READ(i->main);
        MainNode* pm = READ(p->main);
        if (pm->_class != CLASS_CTRIE_CNODE)
            return;
        CNode* cn = (CNode*)pm;
        auto [flag, pos] = flagpos(hc, lev, cn->bmp);
        if (!(flag & cn->bmp))
            return;
        Object* sub = cn->array[pos];
        if (sub != i)
            return;
        if (m->_class != CLASS_CTRIE_TNODE)
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
    
    void* Ctrie::CNode::operator new(size_t fixed, size_t variable) {
        return object_allocate(fixed + variable * sizeof(Object*));
    }
    
    Ctrie::CNode::CNode()
    : MainNode(CLASS_CTRIE_CNODE) {
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
    : MainNode(CLASS_CTRIE_TNODE)
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

    HeapString* Ctrie::find_or_emplace(std::string_view sv, size_t hc) {
        for (;;) {
            INode* r = this->root;
            HeapString* result = r->find_or_emplace(sv, hc, 0, nullptr);
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
    */
    
    
    
    
    
    /*
    bool Ctrie::INode::insert(Value k, Value v, int lev, INode* parent) {
        INode* i = this;
        MainNode* mn = READ(i->main);
        MainNode* nmn;
        switch (mn->_class) {
            case CLASS_CTRIE_CNODE: {
                CNode* cn = (CNode*)mn;
                auto [flag, pos] = flagpos(value_hash(k), lev, cn->bmp);
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
     */
    
    HeapString* Ctrie::INode::find_or_emplace(std::string_view sv, std::size_t hc, int lev, INode* parent) {
        INode* i = this;
        MainNode* mn = READ(i->main);
        MainNode* nmn;
        HeapString* nhs;
        switch (mn->_class) {
            case CLASS_CTRIE_CNODE: {
                CNode* cn = (CNode*)mn;
                auto [flag, pos] = flagpos(hc, lev, cn->bmp);
                if (!(cn->bmp & flag)) {
                    // nmn = cn->inserted(pos, flag, new SNode(k, v));
                    // We have found a CNode with no entry for the hash
                    // The string is not in the table
                    // We must insert it
                    size_t count = sv.size();
                    assert(count > 7);
                    nhs = new(count) HeapString;
                    nhs->_hash = hc;
                    nhs->_size = count;
                    std::memcpy(nhs->_bytes, sv.data(), count);
                    nmn = cn->inserted(pos, flag, nhs);
                    break;
                }
                Object* bn = cn->array[pos];
                switch (bn->_class) {
                    case CLASS_CTRIE_INODE: {
                        INode* sin = (INode*)bn;
                        return sin->find_or_emplace(sv, hc, lev + W, i);
                    }
                    case CLASS_STRING: {
                        // We have hashed to the same bucket as an existing
                        // string
                        
                        // Some of the hash bits match
                        HeapString* hs = (HeapString*)bn;
                        if (hs->_hash == hc) {
                            // All the hash bits match
                            if (hs->_size == sv.size()) {
                                // The sizes match
                                if (!__builtin_memcmp(hs->_bytes, sv.data(), sv.size())) {
                                    // The strings match
                                    Color was = _color_white_to_black_color_was(hs->_color);
                                    switch (was) {
                                        case COLOR_GRAY: {
                                            // leafs are never GRAY
                                            object_debug(hs);
                                            abort();
                                        }
                                        case COLOR_RED:
                                            // we lost the race and have to
                                            // compete to replace it
                                            // don't interfere with the corpse
                                            hs = nullptr;
                                            break;
                                        default:
                                            // was white and became black, or was already black
                                            return hs;
                                    }
                                    
                                    // now we have to replace the thing:
                                    size_t count = sv.size();
                                    assert(count > 7);
                                    nhs = new(count) HeapString;
                                    nhs->_hash = hc;
                                    nhs->_size = count;
                                    std::memcpy(nhs->_bytes, sv.data(), count);
                                    nmn = cn->updated(pos, nhs);


                                }
                            }
                        }
                        // We have to expand the HAMT one level
                        {
                            size_t count = sv.size();
                            assert(count > 7);
                            nhs = new(count) HeapString;
                            nhs->_hash = hc;
                            nhs->_size = count;
                            std::memcpy(nhs->_bytes, sv.data(), count);
                            INode* nbn = new INode(CNode::make(hs, nhs, lev + W));
                            nmn = cn->updated(pos, nbn);
                        }
                    }
                    default: {
                        abort();
                    }
                }
                break;
            }
            case CLASS_CTRIE_TNODE: {
                parent->clean(lev - W);
                return nullptr;
            }
            case CLASS_CTRIE_LNODE: {
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
     */
    
    Value Ctrie::INode::erase(HeapString* hs, int lev, INode* parent) {
        INode* i = this;
        MainNode* mn = i->main.load(Order::ACQUIRE);
        switch (mn->_class) {
            case CLASS_CTRIE_CNODE: {
                CNode* cn = (CNode*)mn;
                auto [flag, pos] = flagpos(hs->_hash, lev, cn->bmp);
                if (!(flag & cn->bmp))
                    return true;
                Value res;
                Object* bn = cn->array[pos];
                switch (bn->_class) {
                    case CLASS_CTRIE_INODE: {
                        INode* sin = (INode*)bn;
                        res = sin->erase(hs, lev + W, i);
                        break;
                    }
                    case CLASS_STRING: {
                        HeapString* dhs = (HeapString*)bn;
                        if (dhs != hs) {
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
                mn = READ(i->main);
                if (mn->_class == CLASS_CTRIE_TNODE)
                    cleanParent(parent, i, hs->_hash, lev - W);
                return res;
            }
            case CLASS_CTRIE_TNODE: {
                parent->clean(lev - W);
                return value_make_RESTART();
            }
            case CLASS_CTRIE_LNODE: {
                LNode* ln = (LNode*)mn;
                abort();
                // TODO: collision
                /*
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
                 */
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
    : Object(CLASS_CTRIE_INODE)
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
    : Object(CLASS_CTRIE) {
        CNode* ncn = new(0) CNode;
        ncn->bmp = 0;
        root = new INode(ncn);
    }
    
    
    Ctrie::LNode::LNode()
    : MainNode(CLASS_CTRIE_LNODE) {
    }

    Object* Ctrie::LNode::find_or_emplace(string_view sv, size_t hc) {

        abort();

        LNode* a = this;
        for (;;) {
            assert(a->sn->_hash == hc);
            if (a->sn->_size == sv.size()) {
                if (!__builtin_memcmp(a->sn->_bytes, sv.data(), sv.size())) {
                    // found, but we have to upgrade it

                }
            }
        }
    }
    
    
    Ctrie::LNode* Ctrie::LNode::erase(HeapString* hs) {
        LNode* a = this;
        for (;;) {
            if (a->sn == hs)
                break;
            if (!a->next)
                return nullptr;
            a = a->next;
        }
        // a->sn == hs
        LNode* b = this;
        LNode* c = a->next;
        for (;;) {
            if (b == a)
                return c;
            
            // what about delete LNode to single?  to empty?
            
            LNode* d = new LNode;
            d->sn = b->sn;
            d->next = c;
            object_shade(d->sn);
            object_shade(d->next);
            b = b->next;
            c = d;
        }
    }
    
} // namespace gc


