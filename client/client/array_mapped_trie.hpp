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
        
    namespace array_mapped_trie {
        
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
        
        inline void _assert_valid_prefix_and_shift(uint64_t prefix_and_shift) {
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
            // The (a >> shift) >> 6 saves us from shifting by (60 + 6) = 66 > 63
            assert((keylike_difference >> shift) && !((keylike_difference >> shift) >> 6));
            return shift;
        }
        
        
                
#pragma mark Mutable compressed array tools

        inline uint64_t mask_for_index(int index) {
            assert(0 <= index);
            assert(index < 64);
            return (uint64_t)1 << (index & 63);
        }
        
        inline uint64_t mask_below_index(int index) {
            assert(0 <= index);
            assert(index < 64);
            return ~(~(uint64_t)0 << (index & 63));
        }
        
        inline uint64_t mask_above_index(int index) {
            assert(0 <= index);
            assert(index < 64);
            return ~(uint64_t)1 << (index & 63);
        }
        
        inline bool bitmap_get_for_index(uint64_t bitmap, int index) {
            return bitmap & mask_for_index(index);
        }
        
        inline void bitmap_set_for_index(uint64_t& bitmap, int index) {
            bitmap |= mask_for_index(index);
        }
        
        inline void bitmap_clear_for_index(uint64_t& bitmap, int index) {
            bitmap &= ~mask_for_index(index);
        }
        
        // A compressed array is a bitmap and an array of T that compactly
        // represents std::array<std::optional<T>, 64>

        // The T for a given index, if it exists, is located in the underlying
        // array at the compressed_index = popcount(bitmap & ~(~0 << index))
        
        // Typically they are embedded in larger structures and use flexible
        // member arrays.  We cannot rely on them being consecutive in memory
        // so we don't reify the concept, instead passing it as arguments to
        // free functions.
        
        // Though public AMT nodes immutable, it can be useful to mutate newly
        // constructed nodes through intermediate states.  Internal methods
        // often follow a pattern of clone-and-modify.
        
        inline bool compressed_array_contains_for_index(uint64_t bitmap, int index) {
            return bitmap_get_for_index(bitmap, index);
        }
        
        inline int compressed_array_get_compressed_index_for_index(uint64_t bitmap, int index) {
            return popcount(bitmap & mask_below_index(index));
        }

        template<typename T>
        bool compressed_array_try_get_for_index(uint64_t bitmap,
                                               T* array,
                                               int index,
                                               std::remove_const_t<T>& victim) {
            bool result = compressed_array_contains_for_index(bitmap, index);
            if (result) {
                victim = array[compressed_array_get_compressed_index_for_index(bitmap,
                                                                               index)];
            }
            return result;
        }
        
        inline int compressed_array_get_compressed_size(uint64_t bitmap) {
            return popcount(bitmap);
        }

        template<typename T>
        bool compressed_array_insert_or_assign_for_index(size_t debug_capacity,
                                                        uint64_t& bitmap,
                                                        T* array,
                                                        int index,
                                                        std::type_identity_t<T> value,
                                                        std::type_identity_t<T>& victim) {
            bool was_found = compressed_array_contains_for_index(bitmap, index);
            int compressed_index = compressed_array_get_compressed_index_for_index(bitmap, index);
            if (was_found) {
                // Preserve the old value
                victim = std::move(array[compressed_index]);
            } else {
                // Make a hole
                int compressed_size = compressed_array_get_compressed_size(bitmap);
                assert(debug_capacity > compressed_size);
                std::copy_backward(array + compressed_index,
                                   array + compressed_size,
                                   array + compressed_size + 1);
                bitmap_set_for_index(bitmap, index);
            }
            array[compressed_index] = std::move(value);
            return was_found;
        }
        
        template<typename T>
        bool compressed_array_try_erase_for_index(uint64_t& bitmap,
                                                  T* array,
                                                  int index,
                                                  std::type_identity_t<T>& victim) {
            bool was_found = compressed_array_contains_for_index(bitmap, index);
            if (was_found) {
                int compressed_index = compressed_array_get_compressed_index_for_index(bitmap, index);
                int compressed_size = compressed_array_get_compressed_size(bitmap);
                victim = std::move(array[compressed_index]);
                std::copy(array + compressed_index + 1,
                          array + compressed_size,
                          array + compressed_index);
                bitmap_clear_for_index(bitmap, index);
            }
            return was_found;
        }
        
        
        
        // TODO: fixme
        
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
        
        
        
        // TODO: Naming
        //
        // Immutable and persistent integer map
        //
        // Implemented as an array-mapped-trie with a branching factor of 64.
        //
        // "Modifying" operations produce a new object that shares much of the
        // structure of the old map.  Nodes on the path to the modifcation
        // are cloned-with-modifications.  There are O(log N) such nodes.
        //
        // It is possible to bulk-modify the map efficiently by rebuilding up
        // from the leaf nodes, in parallel.
        //
        // Unlike a hash map, this structure is efficient for densely populated
        // regions of key space.  The key should be chosen, or transformed,
        // such that the low bits exhibit high entropy.
        //
        // For example, to encode a (int32_t, int32_t) coordinate, the bits
        // should be interleaved in Morton or Z-order.  The integer map then
        // encodes a quadtree. Spatial regions map to subtrees with a
        // particular prefix.  Chances of a common prefix can be maximized by
        // using or offsetting coordinates to be around INT_MAX / 3 = 0101010101...
        // where the alternating bit pattern stops the carries and borrows
        // produced by small coordinate differences from propagating all the
        // way up the prefix.
        
        template<typename T>
        struct Node : GarbageCollected {
            
            static void* operator new(size_t count, void* ptr) {
                return ptr;
            }
            
            uint64_t _prefix_and_shift; // 6 bit shift, 64 - (6 * shift) prefix
#ifndef NDEBUG
            // Capacity in items of either _children or values.  We only need
            // this value when debugging some array operations.
            size_t _debug_capacity;
#endif
            uint64_t _bitmap; // bitmap of which items are present
            union {
                const Node* _children[0]; // compressed flexible member array of children or values
                T _values[0];
            };
            
            Node(uint64_t prefix_and_shift,
                 size_t debug_capacity,
                 uint64_t bitmap)
            : _prefix_and_shift(prefix_and_shift)
            , _debug_capacity(debug_capacity)
            , _bitmap(bitmap) {
            }
            
            int get_compressed_index_for_index(int index) const {
                return compressed_array_get_compressed_index_for_index(_bitmap, index);
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
            
            bool prefix_covers_key(uint64_t key) const {
                auto [prefix, shift] = get_prefix_and_shift();
                return prefix == (key & (~(uint64_t)63 << shift));
            }
            
            bool bitmap_covers_key(uint64_t key) const {
                int index = get_index_for_key(key);
                return bitmap_get_for_index(_bitmap, index);
            }
            
            int get_index_for_key(uint64_t key) const {
                int shift = get_shift();
                assert(!((key ^ _prefix_and_shift) >> shift >> 6));
                return (int)((key >> get_shift()) & (uint64_t)63);
            }
                                    
            int get_compressed_index_for_key(uint64_t key) const {
                int index = get_index_for_key(key);
                return get_compressed_index_for_index(index);
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
                assert(count <= _debug_capacity);
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
                if (!prefix_covers_key(key)) {
                    // prefix excludes key
                    return false;
                }
                if (!bitmap_covers_key(key)) {
                    // bitmap excludes key
                    return false;
                }
                if (has_values()) {
                    // bitmap is authoritative for values
                    return true;
                }
                // recurse into children
                int compressed_index = get_compressed_index_for_key(key);
                const Node* child = _children[compressed_index];
                return child && child->contains(key);
            }
            
            [[nodiscard]] bool try_get(uint64_t key, T& victim) const {
                if (!prefix_covers_key(key)) {
                    return false;
                }
                if (!bitmap_covers_key(key)) {
                    // bitmap excludes key
                    return false;
                }
                int compressed_index = get_compressed_index_for_key(key);
                if (has_children()) {
                    const Node* child = _children[compressed_index];
                    return child && child->try_get(key, victim);
                } else {
                    victim = _values[compressed_index];
                    return true;
                }
            }
                        
            virtual void _garbage_collected_enumerate_fields(TraceContext* context) const override {
                int compressed_size = popcount(_bitmap);
                if (has_children()) {
                    assert(compressed_size <= _debug_capacity);
                    trace_n(_children, compressed_size, context);
                } else {
                    trace_n(_values, compressed_size, context);
                }
            }
            
            [[nodiscard]] static Node* make(uint64_t prefix_and_shift,
                              size_t capacity,
                              uint64_t bitmap) {
                bool has_children_ = prefix_and_shift & (uint64_t)63;
                size_t item_bytes = has_children_ ? sizeof(const Node*) : sizeof(T);
                void* pointer = GarbageCollected::operator new(sizeof(Node) + (capacity * item_bytes));
                Node* node = new(pointer) Node(prefix_and_shift,
                                               capacity,
                                               bitmap);
                return node;
            }
            
            [[nodiscard]] static Node* make_with_key_value(uint64_t key, T value) {
                int shift = 0;
                size_t capacity = 1;
                uint64_t bitmap = mask_for_index((int)(key & (uint64_t)63));
                Node* node = Node::make(prefix_and_shift_for_keylike_and_shift(key,
                                                                               shift),
                                        capacity,
                                        bitmap);
                node->_values[0] = std::move(value);
                return node;
            }

            
            [[nodiscard]] Node* clone_with_capacity(size_t capacity) const {
                int count = popcount(_bitmap);
                assert(capacity >= count);
                Node* node = make(_prefix_and_shift, capacity, _bitmap);
                size_t item_size = _prefix_and_shift & (uint64_t)63 ? sizeof(const Node*) : sizeof(T);
                memcpy(node->_children, _children, count * item_size);
                return node;
            }
            
            [[nodiscard]] Node* clone() const {
                return clone_with_capacity(popcount(_bitmap));
            }
            
            void unsafe_insert_or_replace_child(const Node* child) {
                auto [prefix, shift] = get_prefix_and_shift();
                auto [a_prefix, a_shift] = child->get_prefix_and_shift();
                // levels compatible with parent-child
                assert(a_shift < shift);
                // prefix compatibile
                assert(((prefix ^ a_prefix) >> shift) >> 6);
                int index = get_index_for_key(a_prefix);
                uint64_t select = mask_for_index(index);
                int compressed_index = get_compressed_index_for_index(index);
                if (!(select & _bitmap)) {
                    // There is no existing slot, we need to move everything up one
                    int count = popcount(_bitmap);
                    assert(count < _debug_capacity);
                    std::memmove(_children + compressed_index + 1,
                                 _children + compressed_index,
                                 (count - compressed_index) * sizeof(const Node*));
                    _bitmap |= select;
                }
                _children[compressed_index] = child;
            }
            
            void unsafe_insert_or_replace_key_value(uint64_t key, T value) {
                auto [prefix, shift] = get_prefix_and_shift();
                assert(shift == 0);
                assert(prefix == (key & ~(uint64_t)63));
                int index = get_index_for_key(key);
                uint64_t select = mask_for_index(index);
                int compressed_index = get_compressed_index_for_index(index);
                if (!(select & _bitmap)) {
                    // There is no existing slot, we need to move everything up one
                    int count = popcount(_bitmap);
                    assert(count < _debug_capacity);
                    std::memmove(_values + compressed_index + 1,
                                 _values + compressed_index,
                                 (count - compressed_index) * sizeof(T));
                    _bitmap |= select;
                }
                _values[compressed_index] = value;
            }
            
            void unsafe_erase_key(uint64_t key) {
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
                    p->unsafe_erase_key(key);
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
            
            // Merge two disjoint nodes by making them the children of a higher
            // level node
            [[nodiscard]] static Node* merge_disjoint(const Node* a, const Node* b) {
                assert(a && b);
                auto [a_prefix, a_shift] = a->get_prefix_and_shift();
                auto [b_prefix, b_shift] = b->get_prefix_and_shift();
                assert(a_prefix != b_prefix);
                uint64_t prefix_difference = a_prefix ^ b_prefix;
                int shift = shift_for_keylike_difference(prefix_difference);
                assert((shift > a_shift) && (shift > b_shift));
                uint64_t mask_a = decode(a_prefix >> shift);
                uint64_t mask_b = decode(b_prefix >> shift);
                uint64_t bitmap = mask_a | mask_b;
                assert(popcount(bitmap) == 2);
                uint64_t prefix_and_shift = prefix_and_shift_for_keylike_and_shift(a_prefix, shift);
                size_t capacity = 2;
                Node* node = make(prefix_and_shift, capacity, 0);
                const Node* _;
                (void) compressed_array_insert_or_assign_for_index(node->_debug_capacity,
                                                                   node->_bitmap,
                                                                   node->_children,
                                                                   (int)((a_prefix >> shift) & (uint64_t)63),
                                                                   a,
                                                                   _);
                (void) compressed_array_insert_or_assign_for_index(node->_debug_capacity,
                                                                   node->_bitmap,
                                                                   node->_children,
                                                                   (int)((b_prefix >> shift) & (uint64_t)63),
                                                                   b,
                                                                   _);
                return node;
            }
            
                       
            [[nodiscard]] Node* clone_and_insert(const Node* a) const {
                //printf("%s\n", __PRETTY_FUNCTION__);
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
            
            [[nodiscard]] Node* clone_and_replace(const Node* a) const {
                //printf("%s\n", __PRETTY_FUNCTION__);
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
                            abort();
                            d->_values[d_index2] = a->_values[a_index2]; // favor random
                            ++a_index2; a_map &= (a_map - 1);
                            ++b_index2; b_map &= (b_map - 1);
                        }
                        ++d_index2;
                    }                }
                
                return d;
                
            }
                                    
            [[nodiscard]] std::pair<Node*, bool> clone_and_insert_or_assign_key_value(uint64_t key, T value, T& victim) const {
                if (!prefix_covers_key(key)) {
                    return {
                        merge_disjoint(this,
                                       make_with_key_value(key, value)),
                        true
                    };
                }
                auto [prefix, shift] = get_prefix_and_shift();
                int index = get_index_for_key(key);
                uint64_t select = mask_for_index(index);
                int compressed_index = get_compressed_index_for_index(index);
                Node* new_node = clone_with_capacity(popcount(_bitmap | select));
                bool leaf_did_assign = false;
                if (has_values()) {
                    leaf_did_assign = compressed_array_insert_or_assign_for_index(new_node->_debug_capacity,
                                                                                  new_node->_bitmap,
                                                                                  new_node->_values,
                                                                                  index,
                                                                                  value,
                                                                                  victim);
                } else {
                    assert(has_children());
                    Node* new_child = nullptr;
                    if (_bitmap & select) {
                        const Node* child = _children[compressed_index];
                        std::tie(new_child, leaf_did_assign) = child->clone_and_insert_or_assign_key_value(key, value, victim);
                    } else {
                        new_child = make_with_key_value(key, value);
                    }
                    const Node* _;
                    (void) compressed_array_insert_or_assign_for_index(new_node->_debug_capacity,
                                                                       new_node->_bitmap,
                                                                       new_node->_children,
                                                                       index,
                                                                       new_child,
                                                                       _);
                }
                return { new_node, leaf_did_assign };
            }
            
            [[nodiscard]] std::pair<const Node*, bool> clone_and_erase_key(uint64_t key, T& victim) const {
                if (!prefix_covers_key(key) || !bitmap_covers_key(key))
                    return { this, false };
                int compressed_index = get_compressed_index_for_key(key);
                if (has_children()) {
                    const Node* child = _children[compressed_index];
                    assert(child);
                    auto [new_child, did_erase] = child->clone_and_erase_key(key, victim);
                    assert((new_child == child) == !did_erase);
                    return {
                        (did_erase ? clone_and_replace(new_child) : this),
                        did_erase
                    };
                } else {
                    assert(has_values());
                    Node* new_node = clone();
                    int index = get_index_for_key(key);
                    bool did_erase = compressed_array_try_erase_for_index(new_node->_bitmap,
                                                                          new_node->_values,
                                                                          index,
                                                                          victim);
                    assert(did_erase);
                    return { new_node, did_erase };
                }
            }
            
            
            
            void parallel_for_each(auto&& action) const {
                if (has_children()) {
                    int n = popcount(_bitmap);
                    for (int i = 0; i != n; ++i)
                        _children[i]->parallel_for_each(action);
                } else {
                    uint64_t b = _bitmap;
                    for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                        int j = ctz(b);
                        uint64_t key = _prefix_and_shift | j;
                        action(key, _values[i]);
                    }
                }
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
        
    } // namespace array_mapped_trie
        
} // namespace wry

#endif /* array_mapped_trie_hpp */
