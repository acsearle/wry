//
//  btree.hpp
//  wry
//
//  Created by Antony Searle on 15/1/2023.
//

#ifndef btree_hpp
#define btree_hpp

#include <cstring>
#include <iostream> // ugh

#include "algorithm.hpp"
#include "assert.hpp"
#include "memory.hpp"
#include "stdint.hpp"
#include "utility.hpp"


namespace wry {
    
    template<typename T>
    void relocate_prepend(const T* first, const T* last, T* d_first, T* d_last) {
        auto count = last - first;
        relocate_backward(d_first, d_last, d_last + count, false);
        return relocate(first, last, d_first, true);
    }

    
    // b-tree
    //
    // basic_btree performs ops on ordered Entry, which can be just a Key for
    // sets or a Key,Value worted by Key only for maps
        
    // for D
    // 2*D entries
    // 2*D+1 pointers
    
    // D = (128 - 8 - 4 - 4) / sizeof(Entry) / 2 = floor(56 / sizeof(Entry))
    
    // if D = 7, 14 entries, 15 pointer
    // leaf can be 2 + 14 = 16 pointers in size for pointer-sized Entries
    // likewise, inner can be 32 pointers in size
    
    // if D is 6, 4-byte Entry, 6*2*4 = 48 + 16
    
    // for larger sizes than 16, no benefit
    // for 16 byte entries, we would need an odd number of Entries
    
    // If D = 7 and sizeof(Entry) = 8, then sizeof(leaf) = 128 and
    // sizeof(inner) = 248 bytes
    
    // do we need leaf::index or should we just search for it
    
    template<size_t D /* = 7 */, typename Entry, typename Comparator>
    struct basic_btree {
        
        struct leaf;
        struct inner;
        struct iterator;
        
        struct leaf {
            
            // outstanding questions
            //
            // instead of storing and maintaining index, we could scan
            // parent->children when we need it; the premise of the structure
            // is that linear searches are cheap
            //
            // .size and .children end up far apart in memory
            //
            // "externally partitioned fixed size array" operations?
            
            leaf()
            : parent(nullptr)
            , size(0) {};
            
            // !parent || (this == parent->children[index])
            inner* parent;
            int32_t index;

            int32_t size;
            // entries [0, size) are initialized
            // entries [size, 2*D) are uninitialized
            union {
                Entry entries[2*D];
            };
        };
        
        struct inner : leaf {
            // [0, leaf->size] are initialized
            // (leaf->size, 2*D+1) are uninitialized
            union {
                leaf* children[2*D+1];
            };
        };
        
        leaf* _root;
        int _height;
        Comparator _comparator;

        // size is not naturally tracked by the btree and is needed only to
        // implement _size
        std::size_t _size;

        struct iterator {
            
            using difference_type = ptrdiff_t;
            using value_type = Entry;
            using pointer = Entry*;
            using reference = Entry&;
            using iterator_category = std::bidirectional_iterator_tag;
            
            leaf* _target;
            int32_t _index;
            int _height;
            
            iterator operator++(int) {
                iterator a(*this);
                ++*this;
                return a;
            }
            
            iterator operator--(int) {
                iterator a(*this);
                --*this;
                return a;
            }
            
            Entry* operator->() const {
                assert(_target);
                assert(0 <= _index);
                assert(_index < _target->size);
                return _target->entries + _index;
            }
            
            iterator& operator++() {
                assert(_target);
                assert(-1 <= _index);
                assert(_index < _target->size);
                assert(0 <= _height);
                ++_index; // across
                if (!_height) {
                    for (;;) {
                        if ((_index < _target->size) || !(_target->parent))
                            break;
                        // up right edge of subtree
                        ++_height;
                        _index = _target->index;
                        _target = _target->parent;
                    }
                } else {
                    // down left edge of subtree
                    for (;;) {
                        _target = static_cast<inner*>(_target)->children[_index];
                        _index = 0;
                        --_height;
                        if (!_height)
                            break;
                    }
                }
                return *this;
            }
            
            iterator& operator--() {
                assert(_target);
                assert(0 <= _index);
                assert(_index <= _target->size);
                assert(_height >= 0);
                if (!_height) {
                    for (;;) {
                        if (_index || !(_target->parent))
                            break;
                        // up left edge of subtree
                        ++_height;
                        _index = _target->index;
                        _target = _target->parent;
                    }
                } else {
                    for (;;) {
                        // down right edge of subtree
                        _target = static_cast<inner*>(_target)->children[_index];
                        _index = _target->size;
                        --_height;
                        if (_height == 0)
                            break;
                    }
                }
                // across
                --_index;
                return *this;
            }
            
            Entry& operator*() const {
                assert(_target);
                assert(0 <= _index);
                assert(_index < _target->size);
                return _target->entries[_index];
            }
            
            bool operator==(const iterator&) const = default;
            bool operator!=(const iterator&) const = default;
            
        };
                
        basic_btree()
        : _root(nullptr)
        , _height(0)
        , _comparator()
        , _size(0) {
        }
        
        basic_btree(const basic_btree& other) = delete;
        basic_btree(basic_btree&& other)
        : _root(std::exchange(other._root, nullptr))
        , _size(std::exchange(other._size, 0))
        , _height(std::exchange(other._root, 0))
        , _comparator(std::move(other._comparator)) {
        }
        
        void _clear(leaf* a, int h) {
            if (h) {
                inner* aa = static_cast<inner*>(a);
                for (int32_t i = 0; i <= aa->size; ++i) {
                    _clear(aa->children[i], h - 1);
                    delete aa->children[i];
                }
            }
            std::destroy_n(a->entries, a->size);
        }
        
        void clear() {
            if (_root) {
                _clear(_root, _height);
                // we don't delete root to avoid thrashing when transitioning
                // between the size 0 and size 1 case
                _root->size = 0;
                _height = 0;
                _size = 0;
            }
        }
        
        ~basic_btree() {
            clear();
        }
        
        basic_btree& swap(basic_btree& other) {
            using std::swap;
            swap(_root, other._root);
            swap(_size, other._size);
            swap(_height, other._height);
            swap(_comparator, other._comparator);
            return other;
        }
        
        basic_btree& operator=(const basic_btree& other) = delete;
        basic_btree& operator=(basic_btree&& other) {
            return btree(std::move(other)).swap(*this);
        }
        
        bool empty() const {
            return !_size;
        }
        
        std::size_t size() const {
            return _size;
        }
        
        explicit operator bool() const {
            return static_cast<bool>(_root);
        }
        
        template<typename Keylike>
        Entry* pfind(Keylike&& k) {
            if (!_root)
                return nullptr;
            leaf* p = _root;
            int h = _height;
            for (size_t i = 0;;) {
                if ((i == p->size) || _comparator(k, p->entries[i])) {
                    if (!h)
                        return nullptr;
                    p = static_cast<inner*>(p)->children[i]; --h; i = 0;
                } else {
                    if (!_comparator(p->entries[i], k))
                        return p->entries + i;
                    ++i;
                }
            }
        }
        
        template<typename Keylike>
        iterator find(Keylike&& k) {
            if (!_root)
                return end();
            iterator p{_root, 0, _height};
            for (;;) {
                if ((p._index == p._target->size) || _comparator(k, *p)) {
                    if (!p._height)
                        return end();
                    p._height--;
                    p._target = static_cast<inner*>(p._target)->children[p._index];
                    p._index = 0;
                    continue;
                }
                if (!_comparator(*p, k))
                    return p;
                ++p._index;
            }
        }
        
        template<typename Keylike>
        iterator find(Keylike&& k, iterator hint) {
            
            iterator predecessor, successor;
            
            if (!(hint._index < hint._target->size))
                goto bounded_above;
            
        classify_hint:
            
            if (_comparator(k, *hint))
                goto bounded_above;
            if (_comparator(*hint, k))
                goto bounded_below;
            return hint;
            
        bounded_above:
            
            predecessor = hint;
            while (predecessor._index == 0) {
                if (predecessor._target->parent == nullptr)
                    goto descend;
                ++predecessor._height;
                predecessor._index = predecessor._target->index;
                predecessor._target = predecessor._target->parent;
            }
            --predecessor._index;
            if (_comparator(k, *predecessor)) {
                hint = predecessor;
                goto bounded_above;
            }
            if (!_comparator(*predecessor, k))
                return predecessor;
            goto descend;
            
        bounded_below:
            
            assert(_comparator(*hint, k));
            successor = hint;
            ++successor._index;
            while (successor._index == successor._target->size) {
                if (successor._target->parent == nullptr) {
                    ++hint._index;
                    goto descend;
                }
                ++successor._height;
                successor._index = successor._target->index;
                successor._target = successor._target->parent;
            }
            if (_comparator(*successor, k)) {
                hint = successor;
                goto bounded_below;
            }
            if (!_comparator(k, *successor))
                return successor;
            ++hint._index;
            goto descend;
            
        descend:
            if (!hint._height)
                return end();
            --hint._height;
            hint._target = static_cast<inner*>(hint._target)->children[hint._index];
            for (hint._index = 0;; ++hint._index) {
                if ((hint._index == hint._target->size) || _comparator(k, *hint))
                    goto descend;
                if (!_comparator(*hint, k))
                    return hint;
            }
            
        }
        
        leaf* _insert_or_assign(leaf* a, int h, Entry& e) {
            
            for (int32_t i = 0;;) {
                
                assert(a);
                assert(0 < a->size);
                assert(a->size <= 2*D);
                assert(i <= a->size);
                assert(h >= 0);
                
                if ((i == a->size) || _comparator(e, a->entries[i])) {
                    
                    // we have to insert e before [i]
                    
                    leaf* c = nullptr;
                    if (h) {
                        inner* aa = static_cast<inner*>(a);
                        c = _insert_or_assign(aa->children[i], h - 1, e);
                        if (!c)
                            return nullptr;
                    }
                    
                    // we have to insert the new value of e at a->entries + i
                    // if h, we also have to insert the new node c at aa->children + (i+1)
                    
                    if (a->size != 2*D) {
                        // easy case: insert e at entries + i
                        assert(a->size <= 2*D);
                        Entry* p = a->entries + i;
                        relocate(p, a->entries + a->size, p + 1);
                        new (p) Entry(std::move(e));
                        ++_size;
                        if (h) {
                            // insert c at children + i + 1
                            inner* aa = static_cast<inner*>(a);
                            leaf** q = aa->children + (i+1);
                            relocate(q, aa->children + (aa->size+1), q+1);
                            *q = c;
                            c->parent = aa;
                            for (int32_t j = i+1; j != (aa->size+2); ++j)
                                aa->children[j]->index = j;
                            for (int32_t j = 0; j != (aa->size+2); ++j) {
                                assert(aa->children[j]->parent == aa);
                                assert(aa->children[j]->index == j);
                            }
                        }
                        a->size += 1;
                        return nullptr;
                    }
                    
                    // hard case: insert e at i, then split at D
                    // but we have to do it backwards and inline
                    assert(a->size == 2 * D);
                    
                    inner* bb = h ? (new inner) : nullptr;
                    leaf* b = h ? bb : (new leaf);
                    
                    if (i <= D) {
                        // inserted Entry doesn't go in b
                        Entry* p = a->entries + D;
                        relocate(p, a->entries + (2*D), b->entries);
                        if (i < D) {
                            // inserted Entry goes in a
                            Entry* q = a->entries + i;
                            relocate(q, p, q+1);
                            relocate(&e, q); // swap e
                            relocate(p, &e);
                        }
                        // else e is already the partitioning Entry
                    } else {
                        assert(i > D);
                        // inserted Entry goes in b
                        Entry* p = a->entries + i;
                        Entry* q = relocate(a->entries + D+1, p, b->entries);
                        relocate(&e, q++);
                        relocate(p, a->entries+ 2*D, q);
                        relocate(a->entries + D, &e);
                    }
                    
                    if (h) {
                        inner* aa = static_cast<inner*>(a);
                        if (i+1 < D+1) {
                            // inserted child goes in aa
                            leaf** p = aa->children + D;
                            relocate(p, aa->children + (2*D+1), bb->children);
                            leaf** q = aa->children + (i+1);
                            relocate(q, p, q+1);
                            *q = c;
                            c->parent = aa;
                            // index the changed children of aa
                            for (int32_t j = i + 1; j != D+1; ++j)
                                aa->children[j]->index = j;
                        } else {
                            // inserted child goes in bb
                            leaf** p = relocate(aa->children + D+1, aa->children + (i+1), bb->children);
                            *p = c;
                            relocate(aa->children + (i+1), aa->children + (2*D+1), p+1);
                        }
                        // index all children of bb
                        for (int32_t j = 0; j != D+1; ++j) {
                            assert(aa->children[j]->parent == aa);
                            assert(aa->children[j]->index == j);
                            bb->children[j]->parent = bb;
                            bb->children[j]->index = j;
                        }
                    }
                    
                    a->size = D;
                    b->size = D;
                    return b;
                    
                } // end insert before [i]
                
                if (!_comparator(a->entries[i], e)) {
                    // assign to exact match [i]
                    a->entries[i] = std::move(e);
                    return nullptr;
                }
                
                // must insert after [i]
                ++i;
            }
        }
        
        void insert_or_assign(Entry& e) {
            if (!_root) {
                _root = new leaf;
                new (_root->entries) Entry(std::move(e));
                _root->size = 1;
                _size = 1;
                return;
            }
            leaf* b = _insert_or_assign(_root, _height, e);
            if (!b)
                return;
            
            // _root has been split
            inner* cc = new inner;
            new (cc->entries) Entry(std::move(e));
            ++_size;
            cc->size = 1;
            cc->children[0] = _root; _root->parent = cc; _root->index = 0;
            cc->children[1] = b; b->parent = cc; b->index = 1;
            _root = cc;
            ++_height;
        }
        
        void _rotate_left(inner* aa, int32_t i, int h) {
            assert(h > 0);
            leaf* b = aa->children[i];
            leaf* c = aa->children[i+1];
            relocate_n(aa->entries + i, 1, b->entries + b->size++);
            relocate_n(c->entries, 1, aa->entries + i);
            relocate_n(c->entries + 1, --c->size, c->entries);
            if (h == 1)
                return;
            inner* bb = static_cast<inner*>(b);
            inner* cc = static_cast<inner*>(c);
            bb->children[b->size] = cc->children[0];
            relocate_n(cc->children + 1, cc->size + 1, cc->children);
            bb->children[bb->size]->parent = bb;
            bb->children[bb->size]->index = bb->size;
            for (int32_t j = 0; j <= cc->size; ++j)
                cc->children[j]->index = j;
        }
        
        void _rotate_right(inner* aa, int32_t i, int h) {
            assert(h > 0);
            leaf* b = aa->children[i];
            leaf* c = aa->children[i+1];
            relocate_n(c->entries, c->size++, c->entries + 1);
            relocate_n(aa->entries + i, 1, c->entries);
            relocate_n(b->entries + (--b->size), 1, aa->entries + i);
            if (h == 1)
                return;
            inner* bb = static_cast<inner*>(b);
            inner* cc = static_cast<inner*>(c);
            relocate_n(cc->children, cc->size, cc->children + 1);
            relocate_n(bb->children + bb->size + 1, 1, cc->children);
            cc->children[0]->parent = cc;
            for (int32_t j = 0; j <= cc->size; ++j)
                cc->children[j]->index = j;
        }
        
        void _merge(inner* aa, int32_t i, int h) {
            assert(h > 0);
            leaf* b = aa->children[i];
            leaf* c = aa->children[i+1];
            assert((b->size + 1 + c->size) <= (2 * D));
            relocate_n(aa->entries + i, 1, b->entries + b->size++);
            relocate_n(aa->entries + i + 1, aa->size - (i + 1), aa->entries + i);
            relocate_n(aa->children + i + 2, aa->size - (i + 1), aa->children + i + 1);
            --(aa->size);
            for (int32_t j = (i + 1); j <= aa->size; ++j)
                aa->children[j]->index = j;
            
            relocate_n(c->entries, c->size, b->entries + b->size);
            if (h - 1) {
                inner* bb = static_cast<inner*>(b);
                inner* cc = static_cast<inner*>(c);
                relocate_n(cc->children, cc->size + 1, bb->children + bb->size);
                for (int32_t j = b->size; j <= bb->size + c->size; ++j) {
                    bb->children[j]->parent = bb;
                    bb->children[j]->index = j;
                }
            }
            b->size += std::exchange(c->size, 0);
            delete c;
        }
        
        void _maybe_repair(inner* aa, int32_t i, int h) {
            leaf* b = aa->children[i];
            assert(b);
            if (b->size >= D)
                return;
            if ((i > 0) && (aa->children[i-1]->size > D)) {
                _rotate_right(aa, i-1, h);
            } else if ((i < aa->size) && (aa->children[i+1]->size > D)) {
                _rotate_left(aa, i, h);
            } else if (i < aa->size) {
                _merge(aa, i, h);
            } else {
                _merge(aa, i-1, h);
            }
        }
        
        void _tail_swap(leaf* a, int h, Entry& victim) {
            if (h == 0) {
                --(a->size);
                victim = std::move(a->entries[a->size]);
                std::destroy_at(a->entries + a->size);
                --_size;
            } else {
                inner* aa = static_cast<inner*>(a);
                _tail_swap(aa->children[a->size], h-1, victim);
                _maybe_repair(aa, aa->size, h);
            }
        }
        
        template<typename Key>
        void _erase(leaf* a, int h, Key&& k) {
            for (int32_t i = 0;;) {
                if ((i == a->size) || _comparator(k, a->entries[i])) {
                    if (!h)
                        return; // not present
                    inner* aa = static_cast<inner*>(a);
                    _erase(aa->children[i], h-1, std::forward<Key>(k));
                    _maybe_repair(aa, i, h);
                    return;
                }
                if (!_comparator(a->entries[i], k)) {
                    if (h) {
                        inner* aa = static_cast<inner*>(a);
                        _tail_swap(aa->children[i], h-1, aa->entries[i]);
                        _maybe_repair(aa, i, h);
                    } else {
                        std::destroy_at(a->entries + i);
                        --_size;
                        relocate_n(a->entries + (i + 1), a->size - (i + 1), a->entries + i);
                        --(a->size);
                    }
                    return;
                }
                ++i;
            }
            
        }
        
        template<typename Keylike>
        void erase(Keylike&& k) {
            if (!_root)
                return;
            _erase(_root, _height, std::forward<Keylike>(k));
            if (_root->size)
                return;
            leaf* b = nullptr;
            if (_height) {
                inner* aa = static_cast<inner*>(_root);
                b = aa->children[0];
                b->parent = nullptr;
                b->index = 0;
                --_height;
            }
            delete std::exchange(_root, b);
        }
        
        std::size_t _assert_invariant(leaf* a, int h, const Entry* p, const Entry* q) {
            
            std::size_t n = a->size;
            
            // entries are ordered and within parental bounds
            assert(!p || _comparator(*p, a->entries[0]));
            for (size_t i = 1; i != a->size; ++i) {
                assert(_comparator(a->entries[i-1], a->entries[i]));
                assert(!_comparator(a->entries[i], a->entries[i-1])); // comparator consistency
            }
            assert(!q || _comparator(a->entries[a->size-1], *q));
            
            assert(h >= 0);
            if (h) {
                
                inner* aa = static_cast<inner*>(a);
                
                // all children exist
                for (size_t i = 0; i <= aa->size; ++i) {
                    leaf* b = aa->children[i];
                    assert(b);
                    assert(b->parent == aa);
                    assert(b->index == i);
                }
                
                // recurse check
                for (size_t i = 0; i <= aa->size; ++i) {
                    leaf* b = aa->children[i];
                    assert(D <= b->size);
                    assert(b->size <= 2 * D);
                    n += _assert_invariant(b,
                                      h - 1,
                                      ((i > 0)
                                       ? (aa->entries + (i - 1))
                                       : nullptr),
                                      ((i < aa->size)
                                       ? (aa->entries + i)
                                       : nullptr));
                }
                
            }
            return n;
        }
        
        void _assert_invariant() {
            if (_root) {
                assert(_height >= 0);
                assert(0 < _root->size);
                assert(_root->size <= 2 * D);
                assert(_root->parent == nullptr);
                std::size_t n = _assert_invariant(_root, _height, nullptr, nullptr);
                assert(_size == n);
            } else {
                assert(_height == 0);
            }
        }
        
        template<typename F>
        void _visit(leaf* a, int h, F& f) {
            assert(a);
            for (size_t i = 0;; ++i) {
                if (h) {
                    inner* aa = static_cast<inner*>(a);
                    _visit(aa->children[i], h - 1, f);
                }
                if (i == a->size)
                    break;
                f(a->entries[i]);
            }
        }
        
        template<typename F>
        void visit(F&& f) {
            if (_root)
                _visit(_root, _height, f);
        }
        
        iterator begin() {
            leaf* p = _root;
            int h = _height;
            for (;;) {
                if (h == 0)
                    return iterator{p, 0, 0};
                p = static_cast<inner*>(p)->children[0];
                --h;
            }
        }
        
        iterator end() {
            return iterator{_root, _root ? _root->size : 1, _height};
        }
        
    };
    
    
    
    template<typename Key, typename T, typename Compare = std::less<Key>>
    struct btree_map {
        
        using value_type = std::pair<Key, T>;
        
        struct value_compare {
            Compare comp;
            bool operator()(const value_type& left, const value_type& right) const {
                return comp(left.first, right.first);
            }
            template<typename Keylike>
            bool operator()(const Keylike& left, const value_type& right) const {
                return comp(left, right.first);
            }
            template<typename Keylike>
            bool operator()(const value_type& left, const Keylike& right) const {
                return comp(left.first, right);
            }
        };
        
        using _inner_t = basic_btree<7, value_type, value_compare>;
        
        _inner_t _inner;
        
        using iterator = typename _inner_t::iterator;
        
        void insert_or_assign(value_type&& value) {
            _inner.insert_or_assign(value);
        }
        
        template<typename Keylike>
        iterator find(Keylike&& k) {
            return _inner.find(std::forward<Keylike>(k));
        }
        
        template<typename Keylike>
        iterator find(Keylike&& k, iterator hint) {
            return _inner.find(std::forward<Keylike>(k), std::move(hint));
        }
        
        iterator begin() { return _inner.begin(); }
        iterator end() { return _inner.end(); }
        
        template<typename Keylike>
        void erase(Keylike&& k) {
            return _inner.erase(k);
        }
        
        bool empty() const {
            return _inner.empty();
        }
        
        std::size_t size() const {
            return _inner.size();
        }
        
        void clear() {
            _inner.clear();
        }
        
    };
    
}

#endif /* btree_hpp */
