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
        void compressed_array_insert_for_index(size_t debug_capacity,
                                               uint64_t& bitmap,
                                               T* array,
                                               int index,
                                               std::type_identity_t<T> value) {
            assert(!compressed_array_contains_for_index(bitmap, index));
            int compressed_index = compressed_array_get_compressed_index_for_index(bitmap, index);
            int compressed_size = compressed_array_get_compressed_size(bitmap);
            assert(debug_capacity > compressed_size);
            std::copy_backward(array + compressed_index,
                               array + compressed_size,
                               array + compressed_size + 1);
            bitmap_set_for_index(bitmap, index);
            array[compressed_index] = std::move(value);
        }
        
        template<typename T>
        T compressed_array_exchange_for_index(uint64_t& bitmap,
                                              T* array,
                                              int index,
                                              std::type_identity_t<T> value) {
            assert(compressed_array_contains_for_index(bitmap, index));
            int compressed_index = compressed_array_get_compressed_index_for_index(bitmap, index);
            return std::exchange(array[compressed_index], std::move(value));
        }
        

        template<typename T>
        bool compressed_array_insert_or_exchange_for_index(size_t debug_capacity,
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
            
            
            
            bool has_children() const {
                return (bool)(_prefix_and_shift & (uint64_t)63);
            }
            
            bool has_values() const {
                return !has_children();
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

            int get_index_for_key(uint64_t key) const {
                int shift = get_shift();
                assert(!((key ^ _prefix_and_shift) >> shift >> 6));
                return (int)((key >> get_shift()) & (uint64_t)63);
            }
                        
            bool bitmap_covers_key(uint64_t key) const {
                int index = get_index_for_key(key);
                return bitmap_get_for_index(_bitmap, index);
            }

            int get_compressed_index_for_index(int index) const {
                return compressed_array_get_compressed_index_for_index(_bitmap, index);
            }
            
            int get_compressed_index_for_key(uint64_t key) const {
                int index = get_index_for_key(key);
                return get_compressed_index_for_index(index);
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
            


            
            
            Node(uint64_t prefix_and_shift,
                 size_t debug_capacity,
                 uint64_t bitmap)
            : _prefix_and_shift(prefix_and_shift)
            , _debug_capacity(debug_capacity)
            , _bitmap(bitmap) {
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
                uint64_t bitmap = (uint64_t)1 << (int)(key & (uint64_t)63);
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
            
            
            
            void insert_child(const Node* new_child) {
                assert(has_children());
                assert(new_child);
                uint64_t key = new_child->get_prefix();
                assert(prefix_covers_key(key));
                compressed_array_insert_for_index(_debug_capacity,
                                                  _bitmap,
                                                  _children,
                                                  get_index_for_key(key),
                                                  new_child);
            }
            
            const Node* exchange_child(const Node* new_child) {
                assert(has_children());
                assert(new_child);
                uint64_t key = new_child->get_prefix();
                assert(prefix_covers_key(key));
                return compressed_array_exchange_for_index(_bitmap,
                                                           _children,
                                                           get_index_for_key(key),
                                                           new_child);
            }
            
            void insert_key_value(uint64_t key, T value) {
                assert(has_values());
                assert(prefix_covers_key(key));
                compressed_array_insert_for_index(_debug_capacity,
                                                  _bitmap,
                                                  _values,
                                                  get_index_for_key(key),
                                                  value);
            }
            
            T exchange_key_value(uint64_t key, T value) {
                assert(has_values());
                assert(prefix_covers_key(key));
                return compressed_array_insert_for_index(_bitmap,
                                                         _values,
                                                         get_index_for_key(key),
                                                         value);
            }

            

            // Merge two disjoint nodes by making them the children of a higher
            // level node
            [[nodiscard]] static Node* merge_disjoint(const Node* a, const Node* b) {
                assert(a && b);
                uint64_t prefix_difference = a->_prefix_and_shift ^ b->_prefix_and_shift;
                int shift = shift_for_keylike_difference(prefix_difference);
                Node* new_node = make(prefix_and_shift_for_keylike_and_shift(a->_prefix_and_shift, shift),
                                      /* capacity */ 2,
                                      /* bitmap */ 0);
                new_node->insert_child(a);
                new_node->insert_child(b);
                return new_node;
            }
            
                       
            [[nodiscard]] Node* clone_and_insert_child(const Node* new_child) const {
                assert(has_children());
                uint64_t key = new_child->get_prefix();
                assert(prefix_covers_key(key));
                Node* new_node = clone_with_capacity(popcount(_bitmap) + 1);
                const Node* _ = nullptr;
                bool did_assign = compressed_array_insert_or_assign_for_index(new_node->_debug_capacity,
                                                                              new_node->_bitmap,
                                                                              new_node->_children,
                                                                              get_index_for_key(key),
                                                                              new_child,
                                                                              _);
                assert(!did_assign);
                return new_node;
            }
            
            [[nodiscard]] Node* clone_and_assign_child(const Node* new_child) const {
                assert(has_children());
                uint64_t key = new_child->get_prefix();
                assert(prefix_covers_key(key));
                Node* new_node = clone_with_capacity(popcount(_bitmap));
                (void) compressed_array_exchange_for_index(new_node->_bitmap,
                                                           new_node->_children,
                                                           get_index_for_key(key),
                                                           new_child);
                return new_node;
            }
                        
            Node* clone_and_erase_child_containing_key(uint64_t key) const {
                assert(has_children());
                Node* new_node = clone_with_capacity(popcount(_bitmap));
                const Node* _ = nullptr;
                bool did_erase = compressed_array_erase_for_index(new_node->_debug_capacity,
                                                                  new_node->_bitmap,
                                                                  new_node->_children,
                                                                  get_index_for_key(key),
                                                                  _);
                assert(did_erase);
                return new_node;
            }
            
            
        
                                    
            [[nodiscard]] std::pair<Node*, bool> clone_and_insert_or_assign_key_value(uint64_t key, T value, T& victim) const {
                if (!prefix_covers_key(key)) {
                    return {
                        merge_disjoint(this,
                                       make_with_key_value(key, value)),
                        true
                    };
                }
                int index = get_index_for_key(key);
                uint64_t select = mask_for_index(index);
                int compressed_index = get_compressed_index_for_index(index);
                Node* new_node = clone_with_capacity(popcount(_bitmap | select));
                bool leaf_did_assign = false;
                if (has_values()) {
                    leaf_did_assign = compressed_array_insert_or_exchange_for_index(new_node->_debug_capacity,
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
                    (void) compressed_array_insert_or_exchange_for_index(new_node->_debug_capacity,
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
                        (did_erase ? clone_and_assign_child(new_child) : this),
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
