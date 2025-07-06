//
//  persistent_set.hpp
//  client
//
//  Created by Antony Searle on 23/11/2024.
//

#ifndef persistent_set_hpp
#define persistent_set_hpp

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <algorithm>
#include <bit>
#include <memory>
#include <new>
#include <utility>

#include "garbage_collected.hpp"
#include "adl.hpp"

namespace wry {
    
    // A functional ordered set of 64 bit ints
    
    // TODO: for pure sets, the shift == 0 level nodes only hold the bitmap
    // we can pull them up into the shift == 1 level
    
    namespace _persistent_set {
        
        // bit tools
        
        inline void print_binary(uint64_t a) {
            fputs("0b", stdout);
            for (int i = 63; i-- != 0;)
                fputc((a >> i) & 1 ? '1' : '0', stdout);
        }

        inline int popcount(uint64_t x) {
            return __builtin_popcountll(x);
        }
        
        inline int clz(uint64_t x) {
            return __builtin_clzll(x);
        }

        inline int ctz(uint64_t x) {
            return __builtin_ctzll(x);
        }
                
        inline uint64_t ztc(uint64_t n) {
            return (uint64_t)1 << (n & 63);
        }
                        
        // immutable array tools
        
        template<typename T>
        T* memcat(T* destination) {
            return destination;
        }
        
        template<typename T, typename... Args>
        T* memcat(T* destination, const T* first, const T* last, Args&&... args) {
            destination = std::uninitialized_copy(first, last, destination);
            return memcat(destination, std::forward<Args>(args)...);
        }
        
        template<typename T, typename... Args>
        T* memcat(T* destination, const T* first, size_t count, Args&&... args) {
            destination = std::uninitialized_copy_n(first, count, destination);
            return memcat(destination, std::forward<Args>(args)...);
        }
        
        template<typename T, typename... Args>
        T* memcat(T* destination, T value, Args&&... args) {
            std::construct_at(destination++, value);
            return memcat(destination, std::forward<Args>(args)...);
        }
                        
        // work out the shift required to bring the 6-aligned block of 6 bits that
        // contains the msb into the least significant 6 bits
        inline int compute_shift(uint64_t a) {
            int shift = ((63 - clz(a)) / 6) * 6;
            assert((a >> shift) && !(a >> shift >> 6));
            return shift;
        }
        
        inline uint64_t prefix_from(uint64_t keylike, int shift) {
            assert(0 <= shift);
            assert(shift < 64);
            return keylike & (~(uint64_t)63 << shift);
        }
        
        struct node : GarbageCollected {
            
            const uint64_t _prefix;
            const int _shift;
            const uint64_t _bitmap;
            union {
                const node* _children[0];
                uint64_t _values[0];
            };
            
                        
            void assert_invariant_shallow() const {
                assert(0 <= _shift );
                assert(_shift < 64);
                assert(!(_shift % 6));
                assert(!(_prefix & ~((uint64_t)-1 << _shift)));
                assert(_bitmap);
                int count = popcount(_bitmap);
                if (!_shift)
                    return;
                assert(count > 1);
                for (int i = 0; i != count; ++i) {
                    const node* p = _children[i];
                    assert(p->_shift < _shift);
                    assert(!((p->_prefix ^ _prefix) >> _shift >> 6));
                    assert(_bitmap & ztc(p->_prefix >> _shift));
                }
            }
            
            size_t size() const {
                int n = popcount(_bitmap);;
                if (_shift == 0)
                    return n;
                size_t m = 0;
                for (int i = 0; i != n; ++i)
                    m += _children[i]->size();
                return m;
            }
            
            bool contains(uint64_t key) const {
                if ((key ^ _prefix) >> _shift >> 6)
                    // prefix does not match key
                    return false;
                uint64_t bit = ztc(key >> _shift);
                if (!(bit & _bitmap))
                    // bitmap does not not contain key
                    return false;
                if (_shift == 0)
                    // bitmap is authoritative
                    return true;
                int offset = popcount((bit - 1) & _bitmap);
                return _children[offset]->contains(key);
            }
            
            node(uint64_t prefix, int shift, uint64_t bitmap)
            : _prefix(prefix)
            , _shift(shift)
            , _bitmap(bitmap) {
                assert(0 <= shift);
                assert(shift < 64);
                assert(!(shift % 6));
                assert(bitmap);
                assert(!(prefix & (~(uint64_t)0 << shift)));
            }
            
            static void* allocate_with_count(size_t n) {
                return GarbageCollected::operator new(sizeof(node) + n * sizeof(const node*));
            }
                        
            static node* make(uint64_t prefix, int shift, uint64_t bitmap) {
                size_t count = popcount(bitmap);
                node* p = (node*) node::allocate_with_count(count);
                std::construct_at(p, prefix, shift, bitmap);
                return p;
            }
            
            static node* make_with_key(uint64_t key) {
                return node::make(prefix_from(key, 0), 0, ztc(key));
            }
            
            static node* make_with_children(const node* a, const node* b) {
                assert(a);
                assert(b);
                assert(a->_prefix != b->_prefix);
                if (b->_prefix < a->_prefix)
                    std::swap(a, b);
                uint64_t delta = a->_prefix ^ b->_prefix;
                int shift = compute_shift(delta);
                assert(shift > a->_shift); // else we don't need a node at this level
                assert(shift > b->_shift);
                uint64_t mask_a = ztc(a->_prefix >> shift);
                uint64_t mask_b = ztc(b->_prefix >> shift);
                uint64_t bitmap = mask_a | mask_b;
                assert(popcount(bitmap) == 2);
                node* result = make(a->_prefix, shift, bitmap);
                memcat(result->_children, a, b);
                return result;
            }
            
            
            friend void trace(const node* p) {
                p->_garbage_collected_trace();
            }
            
            virtual void _garbage_collected_scan() const {
                if (_shift) {
                    size_t n = size();
                    for (size_t i = 0; i != n; ++i) {
                        trace(_children[i]);
                    }
                }
            }
            
            node* clone_and_insert(const node* a) const {
                assert(a);
                assert(_shift > a->_shift);
                assert(!((_prefix ^ a->_prefix) >> _shift >> 6));
                uint64_t bit  = ztc(a->_prefix >> _shift);
                uint64_t bitmap = _bitmap | bit;
                assert(bitmap != _bitmap); // caller should have merged and then replaced
                node* result = make(_prefix, _shift, bitmap);
                int offset = popcount(bitmap & (bit - 1));
                memcat(result->_children,
                       _children, _children + offset,
                       a,
                       _children + offset, _children + popcount(_bitmap));
                return result;
            }
            
            node* clone_and_replace(const node* other) const {
                assert(other);
                assert(_shift > other->_shift);
                assert(!((_prefix ^ other->_prefix) >> _shift >> 6));
                uint64_t bit  = ztc(other->_prefix >> _shift);
                uint64_t bitmap = _bitmap | bit;
                assert(bitmap == _bitmap);
                node* result = make(_prefix, _shift, bitmap);
                int offset = popcount(bitmap & (bit - 1));
                assert(_children[offset] != other); // caller should prevent redundant replaces
                memcat(result->_children,
                       _children, _children + offset,
                       other,
                       _children + offset + 1, _children + popcount(_bitmap));
                return result;
            }
            
            node* clone_and_erase(uint64_t key) const {
                assert(!((_prefix ^ key) >> _shift >> 6));
                uint64_t bit = ztc(key >> _shift);
                assert(_bitmap & bit);
                uint64_t bitmap = _bitmap ^ bit;
                node* result = node::make(_prefix, _shift, bitmap);
                int offset = popcount(bitmap & (bit - 1));
                memcat(result->_children,
                       _children, _children + offset,
                       _children + offset + 1, _children + popcount(_bitmap));
                return result;
            }
            
            
            
            int compressed_index_for_onehot(uint64_t h) const {
                return popcount(_bitmap & (h - 1));
            }
            
            const node*& at_onehot(uint64_t h) {
                return _children[compressed_index_for_onehot(h)];
            }
            
            const node* const& at_onehot(uint64_t h) const {
                return _children[compressed_index_for_onehot(h)];
            }
            
            
            
            const node* insert(uint64_t key) const {
                
                uint64_t delta = key ^ _prefix;
                if ((delta >> _shift) >> 6)
                    // prefix does not match
                    return node::make_with_children(this, node::make_with_key(key));
                
                // prefix does match
                // we have to modify the node at this level
                uint64_t bit = ztc(key >> _shift);
                
                if (!(bit & _bitmap)) {
                    // we have to make a new slot
                    if (_shift == 0)
                        // we just need to mark a bit
                        return node::make(_prefix, _shift, _bitmap | bit);
                    // we have to make a new node
                    return clone_and_insert(node::make_with_key(key));
                }
                
                
                // we have to update an existing slot
                if (_shift == 0)
                    // we are at the bottom level, so this is a blocked insert
                    return this;
                // not at the bottom level, so we have to modify the existing
                // element
                int offset = popcount((bit - 1) & _bitmap);
                const node* child = _children[offset];
                const node* replacement = child->insert(key);
                if (replacement == child)
                    // the element already existed so we don't need to do anything else
                    return this;
                return this->clone_and_replace(replacement);
                
            }
            
            
            
            
            
            const node* erase(uint64_t key) const {
                
                assert_invariant_shallow();
                
                uint64_t delta = key ^ _prefix;
                if (delta >> _shift >> 6)
                    // prefix doesn't match so set does not contain key
                    return this;
                
                uint64_t index = (key >> _shift) & 0x3F;
                uint64_t bit = ztc(index);
                if (!(bit & _bitmap))
                    // bitmap doesn't match so set does not contain key
                    return this;
                
                if (_shift == 0) {
                    // erase from a leaf
                    uint64_t bitmap = _bitmap ^ bit;
                    if (!bitmap)
                        // erased last entry
                        return nullptr;
                    return node::make(_prefix, _shift, bitmap);
                }
                
                assert(_shift > 0);
                // erase by recusion
                int count = popcount(_bitmap);
                assert(count >= 2);
                int offset = popcount((bit - 1) & _bitmap);
                const node* child = _children[offset];
                const node* replacement = child->erase(key);
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
            }
            
            
            
            static const node* merge(const node* a, const node* b) {
                if (a == nullptr)
                    return b;
                if (b == nullptr)
                    return a;
                // structural sharing may let us prove that this merge is unnecessary
                if (a == b)
                    return a;
                
                assert(a->_shift == 0 || popcount(a->_bitmap) > 1);
                assert(b->_shift == 0 || popcount(b->_bitmap) > 1);
                
                
                uint64_t delta = a->_prefix ^ b->_prefix;
                
                uint64_t c_shift = std::max(a->_shift, b->_shift);
                if (delta >> c_shift >> 6) {
                    
                    // High bits don't match, sets are disjoint
                    return node::make_with_children(a, b);
                    
                }
                
                if (a->_shift != b->_shift) {
                    
                    // Levels don't match
                    if (a->_shift < b->_shift)
                        std::swap(a, b);
                    assert(a->_shift > b->_shift);
                    
                    auto index = (b->_prefix >> a->_shift) & 63;
                    auto bit = ztc(index);
                    
                    if (!(bit & a->_bitmap))
                        return a->clone_and_insert(b);
                    
                    // b conflicts with a->_child[...]
                    
                    int index2 = popcount((bit - 1) & a->_bitmap);
                    const node* c = a->_children[index2];
                    const node* d = merge(c, b);
                    if (d == c)
                        return a;
                    return a->clone_and_replace(d);
                }
                
                // assert(a->_prefix == b->_prefix);
                assert(a->_shift == b->_shift);
                
                uint64_t bitmap = a->_bitmap | b->_bitmap;
                if (a->_shift == 0) {
                    if (bitmap == a->_bitmap)
                        return a;
                    if (bitmap == b->_bitmap)
                        return b;
                    return node::make(a->_prefix, a->_shift, bitmap);
                }
                
                node* d = node::make(a->_prefix, a->_shift, bitmap);
                // fill the output from a, b, or merge
                
                // TODO: the merge does not need a new node when one is a subset of
                // the other, but we can't prove this without recursing down all
                // the common children.
                
                // We can either allocate a new node and discard it in the (rare?)
                // case it is not needed
                
                // or we can construct in an alloca arena and copy over if needed
                
                uint64_t a_map = a->_bitmap;
                uint64_t b_map = b->_bitmap;
                int a_index2 = 0;
                int b_index2 = 0;
                int d_index2 = 0;
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
                
                return d;
                
            }
            
        }; // node
        
        inline void print(const node* s) {
            if (!s) {
                printf("nullptr\n");
            }
            int count = popcount(s->_bitmap);
            printf("%llx:%d:", s->_prefix & ((uint64_t)-1 << s->_shift), s->_shift);
            print_binary(s->_bitmap);
            printf("(%d)\n", count);
            if (s->_shift) {
                assert(count >= 2);
                for (int i = 0; i != count; ++i)
                    print(s->_children[i]);
            }
        }
        
        
        inline bool equality(const node* a, const node* b) {
            if (a == b)
                // by identity
                return true;
            if (a->_prefix != b->_prefix)
                return false;
            if (a->_shift != b->_shift)
                return false;
            if (a->_bitmap != b->_bitmap)
                return false;
            if (a->_shift == 0)
                return true;
            // by recursion
            int n = popcount(a->_bitmap);
            for (int i = 0; i != n; ++i)
                if (!equality(a->_children[i], b->_children[i]))
                    return false;
            return true;
        }
        
        
        
        struct persistent_set {
            
            const node* root = nullptr;
            
            bool contains(uint64_t key) {
                return (root != nullptr) && root->contains(key);
            }
            
            persistent_set insert(uint64_t key) const {
                return persistent_set{root ? root->insert(key) : node::make_with_key(key)};
            };
            
            
            size_t size() const {
                return root ? root->size() : 0;
            }
            
        };
        
        inline persistent_set merge(persistent_set a, persistent_set b) {
            return persistent_set{node::merge(a.root, b.root)};
        }
        
        
        inline bool is_empty(persistent_set a) {
            return a.root == nullptr;
        }
        
        
        inline persistent_set erase(uint64_t key, persistent_set a) {
            return persistent_set{a.root ? a.root->erase(key) : nullptr};
        };

        template<typename F>
        void parallel_for_each(persistent_set s, F&& f) {
            parallel_for_each(s.root, std::forward<F>(f));
        }
        
        template<typename F>
        void parallel_for_each(const node* p, F&& f) {
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
                              const node* left, const T& right,
                              F&& f) {
            // recurse into 6-bit chunked keyspace
            // left->_prefix is lower bound
            // left->_prefix + ((uint64_t) 64 << _shift) is upper bound
        }
        
    } // namespace _persistent_set
    
    // using _persistent_set::persistent_set;
    
    
    
    namespace _persistent_map {
        
        template<typename T>
        struct Node : GarbageCollected {
        };
        
        template<typename T>
        struct Branch : GarbageCollected {
            uint64_t _prefix;      // (58 - _shift) upper bits common to keys
            int _shift;            // bits for indexing
            uint64_t _bitmap;      // map of bits for indexing
            Node<T>* _children[0]; // popcount(_bitmap) children
        };
                
        template<typename T>
        struct Leaf : GarbageCollected {
            uint64_t _prefix; // upper 58 bits common to keys
            uint64_t _bitmap; // map of lower six bits of keys
            T _values[0];     // popcount(_bitmap) values
            
            T* find(uint64_t key) {
                if ((_prefix ^ key) >> 6)
                    // not present
                    return nullptr;
                int index = (int)(key & 63);
                uint64_t select = (uint64_t)1 << index;
                if (!(select & _bitmap))
                    // not present
                    return nullptr;
                uint64_t mask = select - 1;
                int compressed_index = __builtin_popcountll(mask & _bitmap);
                return _values + compressed_index;
            }
            
        };
        
        
        
    };
    
} // namespace wry

#endif /* persistent_set_hpp */
