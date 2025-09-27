//
//  array_mapped_trie.cpp
//  client
//
//  Created by Antony Searle on 12/7/2025.
//

#include <map>

#include "array_mapped_trie.hpp"

#include "test.hpp"

#if 0



/*
 
 struct persistent_set {
 
 const Node<std::monostate>* root = nullptr;
 
 bool contains(uint64_t key) {
 return (root != nullptr) && root->contains(key);
 }
 
 persistent_set insert(uint64_t key) const {
 return persistent_set{
 root
 ? root->insert(key, std::monostate{})
 : Node<std::monostate>::make_with_key_value(key, std::monostate{})
 };
 };
 
 
 //size_t size() const {
 //    return root ? root->size() : 0;
 //}
 
 };
 
 inline persistent_set merge(persistent_set a, persistent_set b) {
 return persistent_set{Node<std::monostate>::merge(a.root, b.root)};
 }
 
 
 inline bool is_empty(persistent_set a) {
 return a.root == nullptr;
 }
 
 
 //inline persistent_set erase(uint64_t key, persistent_set a) {
 //    return persistent_set{a.root ? a.root->erase(key) : nullptr};
 //};
 
 template<typename F>
 void parallel_for_each(persistent_set s, F&& f) {
 parallel_for_each(s.root, std::forward<F>(f));
 }
 
 template<typename T, typename F>
 void parallel_for_each(const Node<T>* p, F&& f) {
 if (p == nullptr) {
 return;
 } else if (p->_shift) {
 int n = popcount(p->_bitmap);
 for (int i = 0; i != n; ++i)
 parallel_for_each(p->_children[i], f);
 return;
 } else {
 uint64_t b = p->_bitmap;
 // int i = 0;
 for (;;) {
 if (!b)
 return;
 int j = ctz(b);
 f(p->_prefix | j);
 b &= (b - 1);
 // ++i;
 }
 }
 }
 
 template<typename T, typename F>
 void parallel_rebuild(uint64_t lower_bound, uint64_t upper_bound,
 const Node<T>* left, const T& right,
 F&& f) {
 // recurse into 6-bit chunked keyspace
 // left->_prefix is lower bound
 // left->_prefix + ((uint64_t) 64 << _shift) is upper bound
 }
 */





/*
 [[nodiscard]] const Node* insert(uint64_t key, T value) const {
 //printf("%s\n", __PRETTY_FUNCTION__);
 auto [prefix, shift] = get_prefix_and_shift();
 uint64_t delta = key ^ prefix;
 if ((delta >> shift) >> 6)
 // prefix does not match
 return Node::merge_disjoint(this, Node::make_with_key_value(key, value));
 
 // prefix does match
 // we have to modify the node at this level
 uint64_t bit = decode(key >> shift);
 
 if (!(bit & _bitmap)) {
 // we have to make a new slot
 if (shift == 0) {
 // we just need to mark a bit
 return Node::make(prefix, shift, _bitmap | bit);
 }
 // we have to make a new node
 return clone_and_insert(Node::make_with_key_value(key, value));
 }
 
 
 // we have to update an existing slot
 if (shift == 0)
 // we are at the bottom level, so this is a blocked insert
 return this;
 // not at the bottom level, so we have to modify the existing
 // element
 int offset = popcount((bit - 1) & _bitmap);
 const Node* child = _children[offset];
 const Node* replacement = child->insert(key, value);
 if (replacement == child)
 // the element already existed so we don't need to do anything else
 return this;
 return this->clone_and_replace_child(replacement);
 
 }
 */




/*
 const Node* erase(uint64_t key) const {
 printf("%s\n", __PRETTY_FUNCTION__);
 auto [prefix, shift] = get_prefix_and_shift();
 
 _assert_invariant_shallow();
 
 uint64_t delta = key ^ prefix;
 if (delta >> shift >> 6)
 // prefix doesn't match so set does not contain key
 return this;
 
 uint64_t index = (key >> shift) & 0x3F;
 uint64_t bit = ztc(index);
 if (!(bit & _bitmap))
 // bitmap doesn't match so set does not contain key
 return this;
 
 if (shift == 0) {
 // erase from a leaf
 uint64_t bitmap = _bitmap ^ bit;
 if (!bitmap)
 // erased last entry
 return nullptr;
 return Node::make(prefix, shift, bitmap);
 }
 
 assert(shift > 0);
 // erase by recusion
 int count = popcount(_bitmap);
 assert(count >= 2);
 int offset = popcount((bit - 1) & _bitmap);
 const Node* child = _children[offset];
 const Node* replacement = child->erase(key);
 if (replacement == child)
 // key not present
 return this;
 if (replacement)
 // node not trivial
 return this->clone_and_replace(replacement);
 
 assert(replacement == nullptr);
 if (count == 2) {
 // this level would contain only one entry, so replace it
 // with its surviving child
 return _children[offset ^ 1];
 }
 return this->clone_and_erase(key);
 }*/



[[nodiscard]] static const Node* merge(const Node* a, const Node* b) {
    //printf("%s\n", __PRETTY_FUNCTION__);
    
    if (a) {
        a->_assert_invariant_shallow();
    }
    if (b) {
        b->_assert_invariant_shallow();
    }
    
    if (a == nullptr)
        return b;
    if (b == nullptr)
        return a;
    // structural sharing may let us prove that this merge is unnecessary
    if (a == b)
        return a;
    auto [a_prefix, a_shift] = a->get_prefix_and_shift();
    auto [b_prefix, b_shift] = b->get_prefix_and_shift();
    
    
    // assert(a_shift == 0 || popcount(a->_bitmap) > 1);
    // assert(b_shift == 0 || popcount(b->_bitmap) > 1);
    
    
    uint64_t delta = a_prefix ^ b_prefix;
    
    uint64_t c_shift = std::max(a_shift, b_shift);
    if (delta >> (c_shift + 6)) {
        // High bits don't match, sets are disjoint
        return Node::merge_disjoint(a, b);
    }
    
    if (a_shift != b_shift) {
        
        // Levels don't match
        if (a_shift < b_shift) {
            using std::swap;
            swap(a, b);
            swap(a_prefix, b_prefix);
            swap(a_shift, b_shift);
        }
        assert(a_shift > b_shift);
        
        auto index = (b_prefix >> a_shift) & 63;
        auto bit = decode(index);
        
        if (!(bit & a->_bitmap))
            return a->clone_and_insert_child(b);
        
        // b conflicts with a->_child[...]
        
        int index2 = popcount((bit - 1) & a->_bitmap);
        const Node* c = a->_children[index2];
        auto [c_prefix, c_shift] = c->get_prefix_and_shift();
        assert(c_shift < a_shift);
        const Node* d = merge(c, b);
        auto [d_prefix, d_shift] = d->get_prefix_and_shift();
        if (!(d_shift < a_shift)) {
            printf("\"a\" %llx : %d\n", a_prefix, a_shift);
            printf("\"b\" %llx : %d\n", b_prefix, b_shift);
            printf("\"c\" %llx : %d\n", c_prefix, c_shift);
            printf("\"d\" %llx : %d\n", d_prefix, d_shift);
            a->_assert_invariant_shallow();
            b->_assert_invariant_shallow();
            c->_assert_invariant_shallow();
            d->_assert_invariant_shallow();
        }
        assert(d_shift < a_shift);
        if (d == c)
            return a;
        
        return a->clone_and_assign_child(d);
    }
    
    assert(a_prefix == b_prefix);
    assert(a_shift == b_shift);
    
    uint64_t bitmap = a->_bitmap | b->_bitmap;
    Node* d = Node::make(prefix_and_shift_for_keylike_and_shift(a_prefix, a_shift), popcount(bitmap), bitmap);
    // fill the output from a, b, or merge
    
    // TODO: the merge does not need a new node when one is a subset of
    // the other, but we can't prove this without recursing down all
    // the common children, and then knowing how to compare T if T is
    // not monostate
    
    // We can either allocate a new node and discard it in the (rare?)
    // case it is not needed
    
    // or we can construct in an alloca arena and copy over if needed
    
    uint64_t a_map = a->_bitmap;
    uint64_t b_map = b->_bitmap;
    int a_index2 = 0;
    int b_index2 = 0;
    int d_index2 = 0;
    if (a_shift) {
        while (a_map | b_map) {
            int a_n = a_map ? ctz(a_map) : 64;
            int b_n = b_map ? ctz(b_map) : 64;
            if (a_n < b_n) {
                d->_children[d_index2] = a->_children[a_index2];
                ++a_index2; a_map &= (a_map - 1);
            } else if (b_n < a_n) {
                d->_children[d_index2] = b->_children[b_index2];
                ++b_index2; b_map &= (b_map - 1);
            } else {
                d->_children[d_index2] = merge(a->_children[a_index2], b->_children[b_index2]);
                ++a_index2; a_map &= (a_map - 1);
                ++b_index2; b_map &= (b_map - 1);
            }
            ++d_index2;
        }
    } else {
        while (a_map | b_map) {
            int a_n = a_map ? ctz(a_map) : 64;
            int b_n = b_map ? ctz(b_map) : 64;
            if (a_n < b_n) {
                d->_values[d_index2] = a->_values[a_index2];
                ++a_index2; a_map &= (a_map - 1);
            } else if (b_n < a_n) {
                d->_values[d_index2] = b->_values[b_index2];
                ++b_index2; b_map &= (b_map - 1);
            } else {
                abort();
                d->_values[d_index2] = a->_values[a_index2]; // favor random
                ++a_index2; a_map &= (a_map - 1);
                ++b_index2; b_map &= (b_map - 1);
            }
            ++d_index2;
        }                }
    
    return d;
    
}

/*
 void erase_key(uint64_t key) {
 if (!prefix_covers_key(key)) {
 return;
 }
 if (!bitmap_covers_key(key)) {
 return;
 }
 if (has_children()) {
 int compressed_index = get_compressed_index_for_key(key);
 const Node* child = _children[compressed_index];
 Node* p = child->clone();
 p->erase_key(key);
 Node* q = clone();
 q->_children[compressed_index] = p;
 } else {
 T _ = {};
 (void) compressed_array_try_erase_for_index(_bitmap,
 _values,
 index,
 _);
 }
 }
 */
#endif
