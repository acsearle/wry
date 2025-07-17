//
//  array_mapped_trie.hpp
//  client
//
//  Created by Antony Searle on 12/7/2025.
//

#ifndef array_mapped_trie_hpp
#define array_mapped_trie_hpp

#include <cassert>
#include <memory>

// TODO: for std::monostate, which should be in <utility>
#include <bit>
#include <variant>

#include "garbage_collected.hpp"
#include "algorithm.hpp"

namespace wry {
    
    inline void trace(std::monostate,void*) {}

    template<typename ForwardIterator, typename Size>
    ForwardIterator trace_n(ForwardIterator first, Size count,void*p) {
        for (; count > 0; (void)++first, --count) {
            trace(*first,p);
        }
        return first;
    }
        
    namespace _amt0 {
        
#pragma mark - Memory tools
        
        // forward declare to break cycles when using from another namespace
        template<typename T> T* memcat(T* destination);
        template<typename T, typename... Args> T* memcat(T* destination, const T* first, const T* last, Args&&... args);
        template<typename T, typename... Args> T* memcat(T* destination, const T* first, size_t count, Args&&... args);
        template<typename T, typename... Args> T* memcat(T* destination, T value, Args&&... args);
        
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
        
        
        
        
#pragma mark - Binary tools
        
        inline void print_binary(uint64_t a) {
            fputs("0b", stdout);
            for (int i = 63; i-- != 0;)
                fputc((a >> i) & 1 ? '1' : '0', stdout);
        }
        
        using std::has_single_bit;
        
        constexpr int popcount(uint64_t x) {
            return __builtin_popcountg(x);
        }
        
        constexpr int clz(uint64_t x) {
            assert(x);
            return __builtin_clzg(x);
        }
        
        constexpr int ctz(uint64_t x) {
            assert(x);
            return __builtin_ctzg(x);
        }

        constexpr uint64_t decode(int n) {
            return (uint64_t)1 << (n & 63);
        }

        constexpr uint64_t decode(uint64_t n) {
            return (uint64_t)1 << (n & (uint64_t)63);
        }
        
        constexpr int encode(uint64_t onehot) {
            assert(has_single_bit(onehot));
            return ctz(onehot);
        }

                
#pragma mark - Tools for packed prefix and shift
                
        inline void _assert_valid_shift(int shift) {
            assert(0 <= shift);
            assert(shift < 64);
            assert(!(shift % 6));
        }
                
        inline void _assert_valid_prefix_and_shift(uint64_t prefix, int shift) {
            _assert_valid_shift(shift);
            assert((prefix & ~(~(uint64_t)63 << shift)) == 0);
        }
                
        void _assert_valid_prefix_and_shift(uint64_t prefix_and_shift) {
            uint64_t prefix = ~(uint64_t)63 & prefix_and_shift;
            int shift = (int)((uint64_t)63 & prefix_and_shift);
            assert(!(shift % 6));
            assert((prefix & ~(~(uint64_t)63 << shift)) == 0);
        }
        
        inline uint64_t prefix_for_keylike_and_shift(uint64_t keylike, int shift) {
            _assert_valid_shift(shift);
            return keylike & (~(uint64_t)63 << shift);
        }
        
        inline uint64_t prefix_and_shift_for_keylike_and_shift(uint64_t keylike, int shift) {
            _assert_valid_shift(shift);
            return (keylike & ((~(uint64_t)63) << shift)) | (uint64_t)shift;
        };

        
        
        
        
        
        // work out the shift required to bring the 6-aligned block of 6 bits that
        // contains the msb into the least significant 6 bits
        inline int shift_for_keylike_difference(uint64_t keylike_difference) {
            assert(keylike_difference != 0);
            int shift = ((63 - clz(keylike_difference)) / 6) * 6;
            _assert_valid_shift(shift);
            // The (a >> shift) >> 6 saves us from shifting by 60 + 6 = 66 > 63
            assert((keylike_difference >> shift) && !((keylike_difference >> shift) >> 6));
            return shift;
        }
        
        

        
        uint64_t bit_for_index(int index) {
            assert(0 <= index);
            assert(index < 64);
            return (uint64_t)1 << (index & 63);
        }
        
        uint64_t mask_below_index(int index) {
            assert(0 <= index);
            assert(index < 64);
            return ~(~(uint64_t)0 << index);
        }
        
        bool contains_for_index(uint64_t bitmap, int index) {
            assert(0 <= index);
            assert(index < 64);
            uint64_t select = (uint64_t)1 << index;
            return bitmap & select;
        }
        
        
        

        template<typename T>
        struct BitmapArrayRef {
            
            uint64_t* bitmap;
            T* pointer;

            bool contains(int i) const {
                assert(0 <= i);
                assert(i < 64);
                uint64_t select = (uint64_t)1 << i;
                return *bitmap & select;
            }
            
            bool try_get(int i, T* victim) const {
                assert(0 <= i);
                assert(i < 64);
                uint64_t select = (uint64_t)1 << i;
                bool result = *bitmap & select;
                if (result) {
                    int j = popcount(*bitmap & (select - 1));
                    *victim = pointer[j];
                }
                return result;
            }
            
            // capacity is unknown, so we have a predicate we cannot check
            bool unsafe_insert_or_assign(int i, T value) const {
                assert(0 <= i);
                assert(i < 64);
                uint64_t select = (uint64_t)1 << i;
                int j = popcount(*bitmap & (select-1));
                bool result = !(*bitmap & select);
                if (result) {
                    // This is a true insert, and we need to shift back any
                    // values
                    int k = popcount(*bitmap & ~(select - 1));
                    std::memmove(pointer + j + 1, pointer + j, k * sizeof(T));
                    *bitmap |= select;
                }
                pointer[j] = value;
                return result;
            }
            
        };
        
        
        
        template<typename T, typename U, typename V, typename F>
        void transform_compressed_arrays(uint64_t b1, uint64_t b2, const T* v1, const U* v2, V* v3, const F& f) {
            uint64_t common = b1 | b2;
            for (;;) {
                if (!common)
                    break;
                uint64_t select = common & ~(common - 1);
                *v3++ = f((b1 & select) ? v1++ : nullptr,
                          (b2 & select) ? v2++ : nullptr);
                common &= ~select;
            }
        }        

        template<typename T>
        struct Node : GarbageCollected {
            
            static void* operator new(size_t count, void* ptr) {
                return ptr;
            }
            
            constexpr static uint64_t get_select_for_index(int index) {
                return decode(index);
            }
            
            constexpr static uint64_t get_mask_for_index(int index) {
                return ~(~(uint64_t)0 << (index & 63));
            }

            
            uint64_t _prefix_and_shift; // 6 bit shift, 64 - (6 * shift) prefix
#ifndef NDEBUG
            size_t _capacity; // capacity in items of either _children or values
#endif
            uint64_t _bitmap; // bitmap of which items are present
            union {
                const Node* _children[0]; // compressed flexible member array of children or values
                T _values[0];
            };
                                    
            int get_compressed_index_for_mask(uint64_t mask) {
                return popcount(_bitmap & mask);
            }
            
            void compress_index(int index) {
                return get_compressed_index_for_mask(get_mask_for_index(index));
            }
              
            int get_shift() const {
                int shift = (int) (_prefix_and_shift & (uint64_t)63);
                assert(!(shift % 6));
                return shift;
            }
            
            uint64_t get_prefix() const {
                uint64_t prefix = _prefix_and_shift & ~(uint64_t)63;
                assert(!(prefix & ~(~(uint64_t)63 << get_shift())));
                return prefix;
            }

            std::pair<uint64_t, int> get_prefix_and_shift() const {
                return {
                    get_prefix(),
                    get_shift()
                };
            }
            
            int get_index_for_key(uint64_t key) const {
                assert(!((key ^ _prefix_and_shift) >> (get_shift() + 6)));
                return (int)((key >> get_shift()) & (uint64_t)63);
            }
                                    
            int get_compressed_index_for_key(uint64_t key) {
                int index = get_index_for_key(key);
                assert(_bitmap & get_select_for_index(index));
                uint64_t mask = get_mask_for_index(index);
                return get_compressed_index_for_mask(mask);
            }
            
            bool has_children() const {
                return (bool)(_prefix_and_shift & (uint64_t)63);
            }
            
            bool has_values() const {
                return !has_children();
            }
            
            void _assert_invariant_shallow() const {
                auto [prefix, shift] = get_prefix_and_shift();
                assert(_bitmap);
                int count = popcount(_bitmap);
                assert(count > 0);
                assert(count <= _capacity);
                if (has_children()) {
                    uint64_t prefix_mask = ~(uint64_t)63 << shift;
                    for (int j = 0; j != count; ++j) {
                        const Node* child = _children[j];
                        auto [child_prefix, child_shift] = child->get_prefix_and_shift();
                        assert(child_shift < shift);
                        if ((child_prefix & prefix_mask) != prefix) {
                            printf("%llx : %d\n", prefix, shift);
                            printf("%llx : %d\n", child_prefix, child_shift);
                        }
                        assert((child_prefix & prefix_mask) == prefix);
                        int child_index = get_index_for_key(child_prefix);
                        uint64_t select = (uint64_t)1 << child_index;
                        assert(_bitmap & select);
                        if (popcount(_bitmap & (select - 1)) != j) {
                            printf("%llx : %d\n", prefix, shift);
                            printf("%llx : %d\n", child_prefix, child_shift);
                            printf("j: %d\n", j);
                            printf("child_index : %d\n", child_index);
                            printf("bitmap : %llx (%d)\n", _bitmap, popcount(_bitmap));
                            printf("popcount: %d\n", popcount(_bitmap & (select - 1)));
                        }
                        assert(popcount(_bitmap & (select - 1)) == j);
                    }
                }
            }
                        
            bool contains(uint64_t key) const {
                auto [prefix, shift] = get_prefix_and_shift();
                if ((key & ((~(uint64_t)63) << shift)) != prefix) {
                    // prefix excludes key
                    return false;
                }
                int index = (int)((key >> shift) & (uint64_t)63);
                uint64_t select = (uint64_t)1 << index;
                if (!(_bitmap & select)) {
                    // bitmap excludes key
                    return false;
                }
                if (shift == 0) {
                    // bitmap is authoritative
                    return true;
                }
                int compressed_index = popcount(_bitmap & (select - 1));
                const Node* child = _children[compressed_index];
                return child ? child->contains(key) : false;
            }
            
            bool try_get(uint64_t key, T& victim) const {
                printf("try_get: %p\n", this);
                auto [prefix, shift] = get_prefix_and_shift();
                if ((key & ((~(uint64_t)63) << shift)) != prefix) {
                    // prefix excludes key
                    return false;
                }
                int index = (int)((key >> shift) & (uint64_t)63);
                printf("index %d\n", index);
                uint64_t select = (uint64_t)1 << index;
                if (!(_bitmap & select)) {
                    // bitmap excludes key
                    return false;
                }
                int compressed_index = popcount(_bitmap & (select - 1));
                if (shift) {
                    const Node* child = _children[compressed_index];
                    return child && child->try_get(key, victim);
                } else {
                    victim = _values[compressed_index];
                    return true;
                }
            }
            
            virtual ~Node() {
                /*
                 auto [prefix, shift] = prefix_and_shift();
                 int count = popcount(_bitmap);
                 if (!shift) {
                 std::destroy_n(_values, count);
                 }
                 */
            }
            
            virtual void _garbage_collected_enumerate_fields(TraceContext*p) const override {
                //printf("AMT scan %p\n", this);
                int count = popcount(_bitmap);
                if (get_shift()) {
                    assert(count == _capacity);
                    trace_n(_children, count,p);
                    //for (int j = 0; j != count; ++j) {
                    //    _children[j]->_garbage_collected_trace(p);
                    //}
                } else {
                    trace_n(_values, count, p);
                }
            }
            
            virtual void _garbage_collected_trace(void*p) const override {
                //printf("AMT trace %p\n", this);
                GarbageCollected::_garbage_collected_trace(p);
            }
            
            Node(uint64_t prefix_and_shift, size_t capacity, uint64_t bitmap)
            : _prefix_and_shift(prefix_and_shift)
            , _capacity(capacity)
            , _bitmap(bitmap) {
                assert(capacity >= popcount(bitmap));
            }

            static Node* make(uint64_t prefix_and_shift, size_t capacity, uint64_t bitmap) {
                size_t item_size = prefix_and_shift & (uint64_t)63 ? sizeof(const Node*) : sizeof(T);
                void* v = GarbageCollected::operator new(sizeof(Node) + capacity * item_size);
                Node* n = new(v) Node(prefix_and_shift, capacity, bitmap);
                return n;
            }
            
            Node* clone_with_capacity(size_t capacity) const {
                assert(capacity >= _capacity);
                Node* node = make(_prefix_and_shift, capacity, _bitmap);
                size_t item_size = _prefix_and_shift & (uint64_t)63 ? sizeof(const Node*) : sizeof(T);
                int count = popcount(_bitmap);
                memcpy(node->_children, _children, count * item_size);
            }
            
            // Make a map with a single key-value pair
            static Node* make_with_key_value(uint64_t key, T value) {
                int shift = 0;
                size_t capacity = 1;
                uint64_t bitmap = decode(key);
                Node* node = Node::make(prefix_and_shift_for_keylike_and_shift(key,
                                                                               shift),
                                        capacity,
                                        bitmap);
                node->_values[0] = std::move(value);
                return node;
            }
            
            // This is a true mutating method, can't be used after node escapes
            void insert(const Node* a) {
                auto [prefix, shift] = get_prefix_and_shift();
                auto [a_prefix, a_shift] = a->get_prefix_and_shift();
                // levels compatible with parent-child
                assert(a_shift < shift);
                // prefix compatibile
                assert(((prefix ^ a_prefix) >> shift) >> 6);
                int index = get_index_for_key(a_prefix);
                // assert not occupied
                // assert capacity is sufficient
                // memmove tail sequence
                // must be child
                // blah
                abort();
            }
            
            // Merge two disjoint nodes by making them the children of a higher
            // level node
            static Node* merge_disjoint(const Node* a, const Node* b) {
                // printf("%s\n", __PRETTY_FUNCTION__);
                assert(a);
                assert(b);
                auto [a_prefix, a_shift] = a->get_prefix_and_shift();
                auto [b_prefix, b_shift] = b->get_prefix_and_shift();
                assert(a_prefix != b_prefix);
                if (b_prefix < a_prefix) {
                    using std::swap;
                    swap(a, b);
                    swap(a_prefix, b_prefix);
                    swap(a_shift, b_shift);
                }
                uint64_t prefix_difference = a_prefix ^ b_prefix;
                int shift = shift_for_keylike_difference(prefix_difference);
                assert(shift > a_shift); // else we don't need a node at this level
                assert(shift > b_shift);
                uint64_t mask_a = decode(a_prefix >> shift);
                uint64_t mask_b = decode(b_prefix >> shift);
                uint64_t bitmap = mask_a | mask_b;
                assert(popcount(bitmap) == 2);
                Node* result = make(prefix_and_shift_for_keylike_and_shift(a_prefix, shift), popcount(bitmap), bitmap);
                memcat(result->_children, a, b);
                result->_assert_invariant_shallow();
                return result;
            }
            
                       
            Node* clone_and_insert(const Node* a) const {
                // printf("%s\n", __PRETTY_FUNCTION__);
                auto [prefix, shift] = get_prefix_and_shift();
                assert(a);
                auto [a_prefix, a_shift] = a->get_prefix_and_shift();
                assert(shift > a_shift);
                assert(!((prefix ^ a_prefix) >> shift >> 6));
                uint64_t bit  = decode(a_prefix >> shift);
                uint64_t bitmap = _bitmap | bit;
                assert(bitmap != _bitmap); // caller should have merged and replaced
                Node* result = make(prefix_and_shift_for_keylike_and_shift(prefix, shift), popcount(bitmap), bitmap);
                int offset = popcount(bitmap & (bit - 1));
                memcat(result->_children,
                       _children, _children + offset,
                       a,
                       _children + offset, _children + popcount(_bitmap));
                result->_assert_invariant_shallow();
                return result;
            }
            
            Node* clone_and_replace(const Node* a) const {
                // printf("%s\n", __PRETTY_FUNCTION__);
                auto [prefix, shift] = get_prefix_and_shift();
                assert(a);
                assert(shift);
                auto [a_prefix, a_shift] = a->get_prefix_and_shift();
                assert(shift > a_shift);
                assert(!((prefix ^ a_prefix) >> shift >> 6));
                uint64_t bit  = decode(a_prefix >> shift);
                uint64_t bitmap = _bitmap | bit;
                assert(bitmap == _bitmap);
                Node* result = make(prefix_and_shift_for_keylike_and_shift(prefix, shift), popcount(bitmap), bitmap);
                int offset = popcount(bitmap & (bit - 1));
                assert(_children[offset] != a); // caller should prevent redundant replaces
                memcat(result->_children,
                       _children, _children + offset,
                       a,
                       _children + offset + 1, _children + popcount(_bitmap));
                result->_assert_invariant_shallow();
                return result;
            }

            /* BUG: this erases subtree not a single element
            Node* clone_and_erase(uint64_t key) const {
                printf("%s\n", __PRETTY_FUNCTION__);
                auto [prefix, shift] = get_prefix_and_shift();
                assert(!((prefix ^ key) >> shift >> 6));
                uint64_t bit = ztc(key >> shift);
                assert(_bitmap & bit);
                uint64_t bitmap = _bitmap ^ bit;
                if (!bitmap)
                    return nullptr;
                Node* result = Node::make(prefix, shift, bitmap);
                int offset = popcount(bitmap & (bit - 1));
                memcat(result->_children,
                       _children, _children + offset,
                       _children + offset + 1, _children + popcount(_bitmap));
                return result;
            }
             */
            
            
            
            int compressed_index_for_onehot(uint64_t h) const {
                return popcount(_bitmap & (h - 1));
            }
            
            const Node*& at_onehot(uint64_t h) {
                return _children[compressed_index_for_onehot(h)];
            }
            
            const Node* const& at_onehot(uint64_t h) const {
                return _children[compressed_index_for_onehot(h)];
            }
            
            
            
            const Node* insert(uint64_t key, T value) const {
                // printf("%s\n", __PRETTY_FUNCTION__);
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
                return this->clone_and_replace(replacement);
                
            }
            
            
            
            
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
            
            
            
            static const Node* merge(const Node* a, const Node* b) {
                // printf("%s\n", __PRETTY_FUNCTION__);
                
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
                        return a->clone_and_insert(b);
                    
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
                    
                    return a->clone_and_replace(d);
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
                            d->_values[d_index2] = a->_values[a_index2]; // favor random
                            ++a_index2; a_map &= (a_map - 1);
                            ++b_index2; b_map &= (b_map - 1);
                        }
                        ++d_index2;
                    }                }
                
                return d;
                
            }
            
        }; // Node
        
        template<typename T>
        void print(const Node<T>* s) {
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
        
        template<typename T>
        bool equality(const Node<T>* a, const Node<T>* b) {
            if (a == b)
                // by identity
                return true;
            if (a->_prefix != b->_prefix)
                // by prefix
                return false;
            if (a->_shift != b->_shift)
                // by level
                return false;
            if (a->_bitmap != b->_bitmap)
                // by contents
                return false;
            if (a->_shift == 0)
                // by leaf
                return true;
            // by recursion
            int n = popcount(a->_bitmap);
            for (int i = 0; i != n; ++i)
                if (!equality(a->_children[i], b->_children[i]))
                    return false;
            return true;
        }
        
        
        
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
        
    } // namespace _amt0
    
} // namespace wry

#endif /* array_mapped_trie_hpp */
