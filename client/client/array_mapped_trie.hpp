//
//  array_mapped_trie.hpp
//  client
//
//  Created by Antony Searle on 12/7/2025.
//

#ifndef array_mapped_trie_hpp
#define array_mapped_trie_hpp

#include <memory>

#include "compressed_array.hpp"
#include "garbage_collected.hpp"
#include "variant.hpp"
#include "coroutine.hpp"


namespace wry::array_mapped_trie {
    
#pragma mark - Tools for packed prefix and shift
    
    using Coroutine::Task;
    
    template<typename T, unsigned LOG2M = 5, typename BITMAP = uint32_t, typename KEY = __uint128_t>
    struct Node : GarbageCollected {
        
        static const unsigned LOG2N = (unsigned)(sizeof(KEY) * CHAR_BIT);
        static const unsigned M = 1 << LOG2M;
        static const KEY INDEX_MASK = ~(~(KEY)0 << LOG2M);
        
        static_assert(sizeof(BITMAP) * CHAR_BIT >= M);
                
        static void assert_valid_shift(int shift) {
            assert(0 <= shift); // non-negative
            assert(shift < LOG2N); // non-wrapping
            assert(!(shift % LOG2M)); // a multiple of log2(M)
        }
        
        static void assert_valid_prefix_and_shift(KEY prefix, int shift) {
            assert_valid_shift(shift);
            assert((prefix & ~(~INDEX_MASK << shift)) == 0);
        }
        
        static KEY prefix_for_keylike_and_shift(KEY keylike, int shift) {
            assert_valid_shift(shift);
            return keylike & (~INDEX_MASK << shift);
        }
        
        // work out the shift required to bring the 6-aligned block of 6 bits that
        // contains the msb into the least significant 6 bits
        static int shift_for_keylike_difference(KEY keylike_difference) {
            assert(keylike_difference != 0);
            int shift = ((LOG2N - 1 - __builtin_clzg(keylike_difference)) / LOG2M) * LOG2M;
            assert_valid_shift(shift);
            assert((keylike_difference >> shift) && !((keylike_difference >> shift) >> LOG2M));
            return shift;
        }
        
        static void* _Nonnull operator new(size_t count, void* _Nonnull ptr) {
            return ptr;
        }
        
        KEY _prefix;
        int _shift;
        unsigned _debug_capacity;
        unsigned _debug_count;
        BITMAP _bitmap; // bitmap of which items are present
        union {
            // compressed flexible member array of children or values
            Node const* _Nonnull _children[0] __counted_by(_debug_count);
            T _values[0] __counted_by(_debug_count);
        };
        
        bool has_children() const {
            return _shift;
        }
        
        bool has_values() const {
            return !has_children();
        }
        
        int get_shift() const {
            assert(!(_shift % LOG2M));
            return _shift;
        }
        
        KEY get_prefix() const {
            assert(!(_prefix & ~(~INDEX_MASK << _shift)));
            return _prefix;
        }
        
        std::pair<KEY, int> get_prefix_and_shift() const {
            return {
                get_prefix(),
                get_shift()
            };
        }
        
        bool prefix_covers_key(KEY key) const {
            return _prefix == (key & (~INDEX_MASK << _shift));
        }
        
        int get_index_for_key(KEY key) const {
            assert(!((key ^ _prefix) >> _shift >> LOG2M));
            return (int)((key >> _shift) & INDEX_MASK);
        }
        
        bool bitmap_covers_key(KEY key) const {
            int index = get_index_for_key(key);
            return bitmap_get_for_index(_bitmap, index);
        }
        
        int get_compressed_index_for_index(int index) const {
            return compressed_array_get_compressed_index_for_index(_bitmap, index);
        }
        
        int get_compressed_index_for_key(KEY key) const {
            int index = get_index_for_key(key);
            return get_compressed_index_for_index(index);
        }
        
        void _assert_invariant_shallow() const {
            auto [prefix, shift] = get_prefix_and_shift();
            assert(_bitmap);
            int count = popcount(_bitmap);
            assert(count > 0);
            assert(count <= _debug_capacity);
            assert(count == _debug_count);
            if (has_children()) {
                KEY prefix_mask = ~INDEX_MASK << shift;
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
                    KEY select = (KEY)1 << child_index;
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
        
        
        
        
        
        Node(KEY prefix,
             int shift,
             unsigned debug_capacity,
             unsigned debug_count,
             BITMAP bitmap)
        : _prefix(prefix)
        , _shift(shift)
        , _debug_capacity(debug_capacity)
        , _debug_count(debug_count)
        , _bitmap(bitmap) {
        }
        
        virtual void _garbage_collected_scan() const override {
            int compressed_size = popcount(_bitmap);
            if (has_children()) {
                assert(compressed_size <= _debug_capacity);
                // trace_n(_children, compressed_size);
                for (int i = 0; i != compressed_size; ++i)
                    garbage_collected_scan(_children[i]);
            } else {
                // trace_n(_values, compressed_size);
                for (int i = 0; i != compressed_size; ++i)
                    garbage_collected_scan(_values[i]);
            }
        }
        
        [[nodiscard]] static Node* _Nonnull make(KEY prefix,
                                                 int shift,
                                                 unsigned capacity,
                                                 BITMAP bitmap) {
            size_t item_bytes = shift ? sizeof(const Node*) : sizeof(T);
            void* _Nonnull pointer = GarbageCollected::operator new(sizeof(Node) + (capacity * item_bytes));
            return new(pointer) Node(prefix,
                                     shift,
                                     capacity,
                                     popcount(bitmap),
                                     bitmap);
        }
        
        [[nodiscard]] static Node* _Nonnull make_with_key_value(KEY key, T value) {
            int shift = 0;
            BITMAP bitmap = (BITMAP)1 << (int)(key & INDEX_MASK);
            Node* _Nonnull new_node = Node::make(prefix_for_keylike_and_shift(key,
                                                                              shift),
                                                 shift,
                                                 /* capacity = */ 1,
                                                 bitmap);
            new_node->_values[0] = std::move(value);
            return new_node;
        }
        
        [[nodiscard]] Node* _Nonnull clone_with_capacity(size_t capacity) const {
            int count = popcount(_bitmap);
            assert((int)capacity >= count);
            Node* _Nonnull node = make(_prefix, _shift, (uint32_t)capacity, _bitmap);
            size_t item_size = has_children() ? sizeof(const Node*) : sizeof(T);
            memcpy(node->_children, _children, count * item_size);
            return node;
        }
        
        [[nodiscard]] Node* _Nonnull clone() const {
            return clone_with_capacity(popcount(_bitmap));
        }
        
        bool contains(KEY key) const {
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
        
        [[nodiscard]] bool try_get(KEY key, T& victim) const {
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
        
        
        
        void insert_child(Node const* _Nonnull new_child) {
            assert(has_children());
            KEY key = new_child->get_prefix();
            assert(prefix_covers_key(key));
            ++_debug_count;
            compressed_array_insert_for_index(_debug_capacity,
                                              _bitmap,
                                              _children,
                                              get_index_for_key(key),
                                              new_child);
        }
        
        Node const* _Nonnull exchange_child(Node const* _Nonnull new_child) {
            assert(has_children());
            KEY key = new_child->get_prefix();
            assert(prefix_covers_key(key));
            return compressed_array_exchange_for_index(_bitmap,
                                                       _children,
                                                       get_index_for_key(key),
                                                       new_child);
        }
        
        void insert_key_value(KEY key, T value) {
            assert(has_values());
            assert(prefix_covers_key(key));
            ++_debug_count;
            compressed_array_insert_for_index(_debug_capacity,
                                              _bitmap,
                                              _values,
                                              get_index_for_key(key),
                                              value);
        }
        
        T exchange_key_value(KEY key, T value) {
            assert(has_values());
            assert(prefix_covers_key(key));
            return compressed_array_insert_for_index(_bitmap,
                                                     _values,
                                                     get_index_for_key(key),
                                                     value);
        }
        
        
        
        // Merge two disjoint nodes by making them the children of a higher
        // level node
        [[nodiscard]] static Node* _Nonnull merge_disjoint(Node const* _Nonnull a, Node const* _Nonnull b) {
            assert(a && b);
            KEY prefix_difference = a->_prefix ^ b->_prefix;
            int shift = shift_for_keylike_difference(prefix_difference);
            Node* new_node = make(prefix_for_keylike_and_shift(a->_prefix, shift),
                                  shift,
                                  /* capacity */ 2,
                                  /* bitmap */ 0);
            new_node->insert_child(a);
            new_node->insert_child(b);
            return new_node;
        }
        
        
        [[nodiscard]] Node* _Nonnull clone_and_insert_child(const Node* _Nonnull new_child) const {
            assert(has_children());
            KEY key = new_child->get_prefix();
            assert(prefix_covers_key(key));
            Node* _Nonnull new_node = clone_with_capacity(popcount(_bitmap) + 1);
            Node const* _Nullable _ = nullptr;
            ++(new_node->_debug_count);
            bool did_assign = compressed_array_insert_or_assign_for_index(new_node->_debug_capacity,
                                                                          new_node->_bitmap,
                                                                          new_node->_children,
                                                                          get_index_for_key(key),
                                                                          new_child,
                                                                          _);
            assert(!did_assign);
            return new_node;
        }
        
        [[nodiscard]] Node* _Nonnull clone_and_assign_child(Node const* _Nonnull new_child) const {
            assert(has_children());
            KEY key = new_child->get_prefix();
            assert(prefix_covers_key(key));
            Node* _Nonnull new_node = clone_with_capacity(popcount(_bitmap));
            Node const* old_child = compressed_array_exchange_for_index(new_node->_bitmap,
                                                                        new_node->_children,
                                                                        get_index_for_key(key),
                                                                        new_child);
            mutator_overwrote(old_child);
            return new_node;
        }
        
        Node* _Nonnull clone_and_erase_child_containing_key(KEY key) const {
            assert(has_children());
            Node* new_node = clone_with_capacity(popcount(_bitmap));
            const Node* old_child = nullptr;
            bool did_erase = compressed_array_erase_for_index(new_node->_debug_capacity,
                                                              new_node->_bitmap,
                                                              new_node->_children,
                                                              get_index_for_key(key),
                                                              old_child);
            assert(did_erase);
            mutator_overwrote(old_child);
            --(new_node->_debug_count);
            return new_node;
        }
        
        
        
        
        [[nodiscard]] std::pair<Node* _Nonnull, bool> clone_and_insert_or_assign_key_value(KEY key, T value, T& victim) const {
            if (!prefix_covers_key(key)) {
                return {
                    merge_disjoint(this,
                                   make_with_key_value(key,
                                                       value)),
                    true
                };
            }
            int index = get_index_for_key(key);
            KEY select = bitmask_for_index<BITMAP>(index);
            int compressed_index = get_compressed_index_for_index(index);
            Node* _Nonnull new_node = clone_with_capacity(popcount(_bitmap | select));
            new_node->_debug_count = popcount(_bitmap | select);
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
                Node* _Nullable new_child = nullptr;
                if (_bitmap & select) {
                    const Node* _Nonnull child = _children[compressed_index];
                    std::tie(new_child, leaf_did_assign) = child->clone_and_insert_or_assign_key_value(key, value, victim);
                } else {
                    new_child = make_with_key_value(key, value);
                }
                Node const* _Nullable _ = nullptr;
                (void) compressed_array_insert_or_exchange_for_index(new_node->_debug_capacity,
                                                                     new_node->_bitmap,
                                                                     new_node->_children,
                                                                     index,
                                                                     new_child,
                                                                     _);
            }
            return { new_node, leaf_did_assign };
        }
        
        [[nodiscard]] std::pair<Node const* _Nullable, bool> clone_and_erase_key(KEY key, T& victim) const {
            // TODO: Do we handle all cases correctly?
            // - Replacing a count one node with nullptr
            // - Replacing a count two node with surviving child
            if (!prefix_covers_key(key) || !bitmap_covers_key(key))
                // Key not present
                return { this, false };
            int compressed_index = get_compressed_index_for_key(key);
            if (has_children()) {
                const Node* _Nonnull child = _children[compressed_index];
                assert(child);
                auto [new_child, did_erase] = child->clone_and_erase_key(key, victim);
                assert((new_child == child) == !did_erase);
                if (!did_erase)
                    return { this, false };
                return {
                    clone_and_assign_child(new_child),
                    true
                };
            } else {
                assert(has_values());
                // we already established that bitmap_covers_key(key)
                Node* _Nonnull new_node = clone();
                // TODO: we allocate enough for the clone then erase one
                int index = get_index_for_key(key);
                compressed_array_erase_for_index(new_node->_bitmap,
                                                 new_node->_values,
                                                 index,
                                                 victim);
                return { new_node, true };
            }
        }
        
        
        
        void parallel_for_each(auto&& action) const {
            if (has_children()) {
                int n = popcount(_bitmap);
                for (int i = 0; i != n; ++i)
                    _children[i]->parallel_for_each(action);
            } else {
                BITMAP b = _bitmap;
                for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                    int j = bit::ctz(b);
                    KEY key = _prefix | j;
                    action(key, _values[i]);
                }
            }
        }
        
        void for_each(auto&& action) const {
            if (has_children()) {
                int n = popcount(_bitmap);
                for (int i = 0; i != n; ++i)
                    _children[i]->for_each(action);
            } else {
                BITMAP b = _bitmap;
                for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                    int j = bit::ctz(b);
                    KEY key = _prefix | j;
                    action(key, _values[i]);
                }
            }
        }
        
        Task coroutine_parallel_for_each(auto&& action) const {
            if (has_children()) {
                int n = popcount(_bitmap);
                Coroutine::Nursery nursery;
                for (int i = 0; i != n; ++i)
                    co_await nursery.fork(_children[i]->coroutine_parallel_for_each(action));
                co_await nursery.join();
            } else {
                BITMAP b = _bitmap;
                for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                    int j = bit::ctz(b);
                    KEY key = _prefix | j;
                    action(key, _values[i]);
                }
            }
        }
        
        Task coroutine_parallel_for_each_coroutine(auto&& action) const {
            Coroutine::Nursery nursery;
            if (has_children()) {
                int n = popcount(_bitmap);
                for (int i = 0; i != n; ++i)
                    co_await nursery.fork(_children[i]->coroutine_parallel_for_each_coroutine(action));
            } else {
                BITMAP b = _bitmap;
                for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                    int j = bit::ctz(b);
                    KEY key = _prefix | j;
                    co_await nursery.fork(action(key, _values[i]));
                }
            }
            co_await nursery.join();
        }
        
    }; // Node
    
    template<typename T>
    void print(Node<T> const* _Nullable s) {
        if (!s) {
            printf("nullptr\n");
        }
        int count = popcount(s->_bitmap);
        printf("%llx:%d:", s->_prefix, s->_shift);
        print_binary(s->_bitmap);
        printf("(%d)\n", count);
        if (s->has_children()) {
            assert(count >= 2);
            for (int i = 0; i != count; ++i)
                print(s->_children[i]);
        }
    }
    
    
    
    template<typename T> auto
    merge(Node<T> const* _Nullable left, Node<T> const* _Nullable right) -> Node<T> const* _Nullable {
        
        if (!right)
            return left;
        
        if (!left)
            return right;
        
        // TODO: Non-terrible implementation
        
        abort();
        
      
        
        
    }
    
} // namespace wry::array_mapped_trie

// An array-mapped trie for fixed size integer keys.  The values will be stored
// in key order.  Most efficient for keys densely packed in a few subregions,
// i.e. the opposite of a good hash.
//
// The trie branches by a power of two each level.  Empty slots are compressed
// using a bitmapped array.  Each node knows its own prefix.  Singleton nodes
// can occur only at the leafs.
//
// TODO: it could make more sense to pack sparse levels into a btree style
// structure.  Note that many plausible distributions of keys will produce nodes
// that are almost full or almost empty.  We could be better served by other
// kinds of nodes: Node const* _Nullable children[64] ; N
//
// We have two parameters of interest, the key length and the bitmap length.
//
// The bitmap length is the branching factor.  The key is considered in
// blocks of that size.  The branching factor is going to control the
// performance of various operations on the data structure.
//
// When the bitmap and key lengths are equal, we can pack the shift into the
// low bits of the prefix.  But we can't always do this.
//
// We read the node metadata, and then index into the flexible array member.
// For lookup performance, we want the index to read memory that has already
// been loaded along with the metadata.  If we consider the block size to be
// 64 bytes, we have
//
//  0 __vtbl
//  8 gc_color
// 16 prefix_and_shift
// 24 bitmap
// 32 child[0]
// 40 child[1]
// 48 child[2]
// 56 child[3]
//
//


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

#endif /* array_mapped_trie_hpp */
