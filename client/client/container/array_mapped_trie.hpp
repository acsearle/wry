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
#include "bit.hpp"


namespace wry::array_mapped_trie {
    
    using bit::popcount;
    using bit::ctz;
   
    
    using Coroutine::Task;
    
    template<
    typename T,
    typename Key = uint64_t,
    typename Bitmap = uint32_t,
    int SYMBOL_WIDTH = 4>
    struct Node : GarbageCollected {

        static constexpr size_t KEY_WIDTH = sizeof(Key) * CHAR_BIT;
        static constexpr size_t BITMAP_WIDTH = sizeof(Bitmap) * CHAR_BIT;
        
        static constexpr Key PREFIX_MASK = ~(Key)0 << SYMBOL_WIDTH;
        static constexpr Key INDEX_MASK = ~PREFIX_MASK;
        
        static_assert(BITMAP_WIDTH >= ((size_t)1 << SYMBOL_WIDTH));
                
        static void assert_valid_shift(int shift) {
            assert(0 <= shift); // non-negative
            assert(shift < KEY_WIDTH); // non-wrapping
            assert(!(shift % SYMBOL_WIDTH)); // a multiple of log2(M)
        }
        
        static void assert_valid_prefix_and_shift(Key prefix, int shift) {
            assert_valid_shift(shift);
            assert((prefix & ~(PREFIX_MASK << shift)) == 0);
        }
        
        static Key prefix_mask_for_shift(int shift) {
            return PREFIX_MASK << shift;
        }
        
        static Key prefix_from_key_and_shift(Key key, int shift) {
            assert_valid_shift(shift);
            return key & prefix_mask_for_shift(shift);
        }
        
        static int shift_from_keys(Key a, Key b) {
            Key d = a ^ b;
            assert(d);
            using bit::clz;
            // shift is the position of the most significant differing bit,
            // rounded down to a multiple of SYMBOL_WIDTH;
            // SYMBOL_WIDTH may not be a power of two
            int shift = ((unsigned)(KEY_WIDTH - 1 - clz(d)) / SYMBOL_WIDTH) * SYMBOL_WIDTH;
            assert_valid_shift(shift);
            assert(!(d & (PREFIX_MASK << shift))); // prefix is common
            assert((d >> shift) & INDEX_MASK);     // indices are disjoint
            return shift;
        }
        
        static void* _Nonnull operator new(std::size_t count, void* _Nonnull ptr) {
            return ptr;
        }
                
        Key _prefix;
        int _shift;
        size_t _debug_capacity;
        size_t _debug_count;
        Bitmap _bitmap; // bitmap of which items are present
        union {
            // compressed flexible member array of children or values
            Node const* _Nonnull _children[0] __counted_by(_debug_count);
            T _values[0] __counted_by(_debug_count);
        };
                        
        Key get_prefix_mask() const {
            return PREFIX_MASK << _shift;
        }
        
        static bool prefixes_are_disjoint(Node const* _Nullable a,
                                          Node const* _Nullable b) {
            return (a->_prefix ^ b->_prefix) & (a->get_prefix_mask() & b->get_prefix_mask());
        }
        
        bool prefix_includes_key(Key key) const {
            return _prefix == (key & get_prefix_mask());
        }
        
        int get_index_for_key(Key key) const {
            assert(prefix_includes_key(key));
            return (int)((key >> _shift) & INDEX_MASK);
        }
        
        bool bitmap_includes_key(Key key) const {
            int index = get_index_for_key(key);
            return bitmap_get_for_index(_bitmap, index);
        }
        
        int get_compressed_index_for_index(int index) const {
            return compressed_array_get_compressed_index_for_index(_bitmap, index);
        }
        
        int get_compressed_index_for_key(Key key) const {
            int index = get_index_for_key(key);
            return get_compressed_index_for_index(index);
        }
        
        bool has_children() const {
            return _shift;
        }
        
        bool has_values() const {
            return !has_children();
        }



       
        
        
        
        
        
        Node(Key prefix,
             int shift,
             size_t debug_capacity,
             size_t debug_count,
             Bitmap bitmap)
        : _prefix(prefix)
        , _shift(shift)
        , _debug_capacity(debug_capacity)
        , _debug_count(debug_count)
        , _bitmap(bitmap) {
            using bit::popcount;
            assert(_debug_capacity >= popcount(_bitmap));
            assert(_debug_count >= popcount(_bitmap));
            assert(_debug_count <= _debug_capacity);
        }
        
        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }
        
        virtual void _garbage_collected_scan() const override {
            int compressed_size = popcount(_bitmap);
            if (has_children()) {
                assert(compressed_size <= _debug_capacity);
                for (int i = 0; i != compressed_size; ++i)
                    garbage_collected_scan(_children[i]);
            } else {
                for (int i = 0; i != compressed_size; ++i)
                    garbage_collected_scan(_values[i]);
            }
        }
        
        
        [[nodiscard]] static Node* _Nonnull
        make(Key prefix,
             int shift,
             size_t capacity,
             size_t count,
             Bitmap bitmap)
        {
            size_t item_bytes = shift ? sizeof(const Node*) : sizeof(T);
            void* _Nonnull pointer = GarbageCollected::operator new(sizeof(Node) + (capacity * item_bytes));
            return new(pointer) Node(prefix,
                                     shift,
                                     capacity,
                                     count,
                                     bitmap);
        }
        
        [[nodiscard]] static Node* _Nonnull
        make_singleton(Key key,
                       T value)
        {
            Key prefix = key & PREFIX_MASK;
            int shift = 0;
            size_t capacity = 1;
            size_t count = 1;
            Key index = key & INDEX_MASK;
            Bitmap bitmap = (Bitmap)1 << (Bitmap)(index);
            Node* _Nonnull new_node = Node::make(prefix,
                                                 shift,
                                                 capacity,
                                                 count,
                                                 bitmap);
            new_node->_values[0] = std::move(value);
            return new_node;
        }
        
       

        [[nodiscard]] bool contains(Key key) const {
            if (!prefix_includes_key(key)) {
                return false; // key is excluded by the prefix
            }
            if (!bitmap_includes_key(key)) {
                return false; // key is excluded by the bitmap
            }
            if (!has_children()) {
                return true;  // bitmap is authoritative for leaves
            }
            return _children[get_compressed_index_for_key(key)]->contains(key);
        }
        
        [[nodiscard]] bool try_get(Key key, T& victim) const {
            if (!prefix_includes_key(key)) {
                return false; // key is excluded by the prefix
            }
            if (!bitmap_includes_key(key)) {
                return false; // key is excluded by the bitmap
            }
            int compressed_index = get_compressed_index_for_key(key);
            if (!has_children()) {
                victim = _values[compressed_index];
                return true; // bitmap is authoritative for leaves
            }
            return _children[compressed_index]->try_get(key, victim);
        }
        
        [[nodiscard]] bool contains_any(Key key, Key mask) const {
            if ((_prefix ^ key) & get_prefix_mask() & mask) {
                return false; // masked key is excluded by the masked prefix
            }
            abort();
            // TODO: bit hacking
        }
                
        
        // Merge is fundamental
        
        template<typename F>
        [[nodiscard]] static Node const* _Nullable
        merge(Node const* _Nullable a,
              Node const* _Nullable b,
              F&& resolver)
        {
            
            if (!b)
                return a; // b is empty
            
            if (!a)
                return b; // a is empty
            
            int c_shift{};
            if (prefixes_are_disjoint(a, b)) {
                c_shift = shift_from_keys(a->_prefix, b->_prefix);
                assert(c_shift > a->_shift);
                assert(c_shift > b->_shift);
            } else {
                c_shift = std::max(a->_shift, b->_shift);
            }
            Key c_prefix = prefix_from_key_and_shift(a->_prefix, c_shift);

            Bitmap a_bitmap{};
            void const* a_array{};
            if (a->_shift == c_shift) {
                a_bitmap = a->_bitmap;
                a_array = a->_children;
            } else {
                // By mocking up a trivial c-level array we can uniformly handle all cases
                a_bitmap = (Key)1 << ((a->_prefix >> c_shift) & INDEX_MASK);
                a_array = &a;
            }
            
            Bitmap b_bitmap{};
            void const* b_array{};
            if (b->_shift == c_shift) {
                b_bitmap = b->_bitmap;
                b_array = b->_children;
            } else {
                b_bitmap = (Key)1 << ((b->_prefix >> c_shift) & INDEX_MASK);
                b_array = &b;
            }

            Bitmap c_bitmap = a_bitmap | b_bitmap;
            using bit::popcount;
            size_t c_count = popcount(c_bitmap);
            
            Node* c = make(c_prefix,
                           c_shift,
                           c_count,
                           c_count,
                           c_bitmap);
            if (c_shift) {
                merge_compressed_arrays(a_bitmap,
                                        b_bitmap,
                                        (Node const* _Nonnull const* _Nonnull)a_array,
                                        (Node const* _Nonnull const* _Nonnull)b_array,
                                        c->_children,
                                        [&resolver](Node const* _Nonnull a,
                                                    Node const* _Nonnull b) {
                    return merge(a, b, resolver);
                });
            } else {
                merge_compressed_arrays(a_bitmap,
                                        b_bitmap,
                                        (T const* _Nonnull)a_array,
                                        (T const* _Nonnull)b_array,
                                        c->_values,
                                        resolver);
            }
            return c;
        } // merge(a, b, f)
        
        // Default merge is tiebroken left
        [[nodiscard]] static Node const* _Nullable
        merge(Node const* _Nullable a,
              Node const* _Nullable b) {
            return merge(a, b, [](T left, T) { return left; });
        }
        
                
        [[nodiscard]] static Node const* _Nonnull
        insert(Node const* _Nullable node,
               Key key, T value) {
            // TODO: Defer make_singleton until is confirmed to be necessary
            return merge(make_singleton(key, value), node);
        }
        
        
        // Erase all elements that match "key", under an optional mask
        
        [[nodiscard]] static Node const* _Nullable
        erase(Node const* _Nullable a,
              Key key,
              Key mask = ~(Key)0) {
            
            if (!a)
                // nothing to erase
                return nullptr;

            auto pm = a->get_prefix_mask();
            if ((a->_prefix ^ key) & mask & pm)
                // node doesn't cover any possible keys; nothing is erased
                return a;
            // prefix is compatible with key, under masking
            
            if (!(~pm & mask))
                // the mask ignores all non-prefix bits, so everything is erased
                return nullptr;
            // we have to descend to resolve erasure
            
            // TODO: We may be able to exclude some children purely by their
            // indices at this level
            
            // allocate a new branch with worst-case capacity
            Node* c = make(a->_prefix,
                           a->_shift,
                           a->_debug_count,
                           0,
                           0);
            
            if (a->has_children()) {
                auto first = a->_children;
                auto last = first + popcount(a->_bitmap);
                auto d_first = c->_children;
                // We can do this in parallel
                for (; first != last; ++first, ++d_first)
                    *d_first = erase(*first, key, mask);
                first = d_first = c->_children;
                Bitmap a_bitmap = {};
                Bitmap c_bitmap = {};
                while (a_bitmap) {
                    Bitmap next = a_bitmap & (a_bitmap - 1);
                    Bitmap select = a_bitmap ^ next;
                    if (*first) {
                        c_bitmap |= select;
                        if (d_first != first) {
                            *d_first = *first;
                        }
                        ++d_first;
                    }
                    ++first;
                    a_bitmap = next;
                }
                c->_debug_count = popcount(c_bitmap);
                c->_bitmap = c_bitmap;
            } else {
                Bitmap a_bitmap = a->_bitmap;
                auto a_values = a->_values;
                Bitmap c_bitmap = {};
                auto c_values = c->_values;
                Key im = mask & INDEX_MASK;
                // Compact while filtering because
                // - T has no null value
                // - We don't want to parallelize this simple bounded loop
                while (a_bitmap) {
                    Bitmap next = a_bitmap & (a_bitmap - 1);
                    Bitmap select = a_bitmap ^ next;
                    int index = ctz(a_bitmap);
                    assert((Bitmap(1) << index) == select);
                    if (((Key)index ^ key) & mask) {
                        // no match; no erase
                        c_bitmap |= select;
                        *c_values++ = *a_values;
                    } else {
                        // match; erase
                    }
                    ++a_values;
                }
            }
        }
        
        
        
        
        
        
        
        // The other fundamental operation is to selectively erase elements

        
        static std::pair<Node const* _Nullable, Node const* _Nullable>
        partition_mask(Node const* _Nullable node,
                       Key key,
                       Key mask) {
            if (!node)
                return {nullptr, nullptr};
            
            auto mask2 = PREFIX_MASK << node->_shift;
            if ((node->_prefix ^ key) & mask2 & mask) {
                // The prefix is incompatible
                return { nullptr, node };
            }
            
            if (!(~mask2 & mask)) {
                // The prefix is compatible and no other bits are considered
                return { node, nullptr };
            }
            
            // Build the output:
            std::pair<Node const* _Nullable, Node const* _Nullable> result{};
            
            assert(node->has_children());
            auto n = __builtin_popcountg(node->_bitmap);
            auto p = node->_children;
            for (; n--; ++p) {
                auto [a, b] = partition_mask(*p, key, mask);
                if (a) {
                    if (result.first) {
                        mutator_overwrote(result.first);
                        result.first = merge(result.first, a);
                    } else {
                        result.first = a;
                    }
                }
                if (b) {
                    if (result.second) {
                        mutator_overwrote(result.second);
                        result.second = merge(result.second, b);
                    } else {
                        result.second = b;
                    }
                }
            }
            return result;
            
        }
        
        
        
        
        
        
        
        
        [[nodiscard]] Node* _Nonnull clone_and_insert_child(const Node* _Nonnull new_child) const {
            assert(has_children());
            Key key = new_child->_prefix;
            assert(prefix_includes_key(key));
            Node* _Nonnull new_node = clone_with_capacity(popcount(_bitmap) + 1);
            Node const* _Nullable _ = nullptr;
            ++(new_node->_debug_count);
            compressed_array_insert_for_index(new_node->_debug_capacity,
                                              new_node->_bitmap,
                                              new_node->_children,
                                              get_index_for_key(key),
                                              new_child);
            return new_node;
        }
        
        [[nodiscard]] Node* _Nonnull clone_and_assign_child(Node const* _Nonnull new_child) const {
            assert(has_children());
            Key key = new_child->_prefix;
            assert(prefix_includes_key(key));
            Node* _Nonnull new_node = clone_with_capacity(popcount(_bitmap));
            // Node const* old_child =
            (void) compressed_array_exchange_for_index(new_node->_bitmap,
                                                       new_node->_children,
                                                       get_index_for_key(key),
                                                       new_child);
            // mutator_overwrote(old_child);
            return new_node;
        }
        
        Node* _Nonnull clone_and_erase_child_containing_key(Key key) const {
            assert(has_children());
            Node* new_node = clone_with_capacity(popcount(_bitmap));
            const Node* old_child = nullptr;
            bool did_erase = compressed_array_erase_for_index(new_node->_debug_capacity,
                                                              new_node->_bitmap,
                                                              new_node->_children,
                                                              get_index_for_key(key),
                                                              old_child);
            assert(did_erase);
            // mutator_overwrote(old_child);
            --(new_node->_debug_count);
            return new_node;
        }
        
        
        
        
        [[nodiscard]] std::pair<Node* _Nonnull, bool> clone_and_insert_or_assign_key_value(Key key, T value, T& victim) const {
            if (!prefix_includes_key(key)) {
                return {
                    merge_disjoint(this,
                                   make_singleton(key,
                                                       value)),
                    true
                };
            }
            int index = get_index_for_key(key);
            Key select = bitmask_for_index<Bitmap>(index);
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
                    new_child = make_singleton(key, value);
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
        
        [[nodiscard]] std::pair<Node const* _Nullable, bool> clone_and_erase_key(Key key, T& victim) const {
            // TODO: Do we handle all cases correctly?
            // - Replacing a count one node with nullptr
            // - Replacing a count two node with surviving child
            if (!prefix_includes_key(key) || !bitmap_includes_key(key))
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
                // we already established that bitmap_includes_key(key)
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
                Bitmap b = _bitmap;
                for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                    int j = bit::ctz(b);
                    Key key = _prefix | j;
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
                Bitmap b = _bitmap;
                for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                    int j = bit::ctz(b);
                    Key key = _prefix | j;
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
                Bitmap b = _bitmap;
                for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                    int j = bit::ctz(b);
                    Key key = _prefix | j;
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
                Bitmap b = _bitmap;
                for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                    int j = bit::ctz(b);
                    Key key = _prefix | j;
                    co_await nursery.fork(action(key, _values[i]));
                }
            }
            co_await nursery.join();
        }
        
      
        template<typename Action>
        static void for_each_mask(Node const* _Nullable node, Key key, Key mask, Action&& action) {
            if (!node)
                return;
            if ((node->_prefix ^ key) & (node->get_prefix_mask() & mask))
                return;
            Bitmap a = node->_bitmap;
            Key m = (INDEX_MASK << node->_shift) & mask;
            for (; a; a &= (a-1)) {
                int i = bit::ctz(a);
                auto key2 = ((Key)i << node->_shift) | node->_prefix;
                if ((key2 ^ key) & m)
                    continue;
                Bitmap j = (Bitmap)1 << i;
                int k = popcount(node->_bitmap & (j-1));
                if (node->has_children()) {
                    for_each_mask(node->_children[k], key, mask, action);
                } else {
                    action(key2, node->_values[k]);
                }
            }
        }
        
        
        
        
                
        
        
        [[nodiscard]] static Node* _Nullable make_leaf_with_leading_pairs(auto& first, auto last) {
            if (first == last)
                return nullptr;
            auto first2 = first;
            Key prefix = {};
            int shift = 0;
            size_t count;
            Bitmap bitmap = {};
            Key key = first2->first & ~INDEX_MASK;
            prefix = key & ~INDEX_MASK;
            for (;;) {
                ++count;
                auto index = key & INDEX_MASK;
                auto mask = (Bitmap)1 << (key & INDEX_MASK);
                assert(mask > bitmap); // i.e. sorted
                bitmap |= mask;
                ++first2;
                if (first2 == last)
                    break;
                key = first2->first;
                if ((prefix ^ key) & ~INDEX_MASK)
                    break;
            }
            assert(popcount(bitmap) == count);
            auto result = make(prefix, shift, count, bitmap);
            auto d_first = result->_values;
            for (;first != first2; ++first, ++d_first) {
                d_first = first->second;
            }
            return result;
        }
        
        [[nodiscard]] static Node* _Nullable make_with_pairs(auto first, auto last) {
            std::vector<Node* const> leafs;
            while (first != last)
                leafs.push_back(make_leaf_with_leading_pairs(first, last));
            
        }
        
        // Merge two disjoint nodes by making them the children of a higher
        // level node
        [[nodiscard]] static Node* _Nonnull merge_disjoint(Node const* _Nonnull a, Node const* _Nonnull b) {
            assert(a && b);
            assert((a->_prefix ^ b->_prefix) & PREFIX_MASK);
            int shift = shift_from_keys(a->_prefix, b->_prefix);
            assert(shift > a->_shift);
            assert(shift > b->_shift);
            Node* new_node = make(prefix_from_key_and_shift(a->_prefix, shift),
                                  shift,
                                  /* capacity */ 2,
                                  /* count */ 0,
                                  /* bitmap */ 0);
            new_node->insert_child(a);
            new_node->insert_child(b);
            return new_node;
        }
        
        
        void _assert_invariant_shallow() const {
            assert(_bitmap);
            int count = popcount(_bitmap);
            assert(count > 0);
            assert(count <= _debug_capacity);
            assert(count == _debug_count);
            if (has_children()) {
                Key get_prefix_mask = ~INDEX_MASK << _shift;
                for (int j = 0; j != count; ++j) {
                    const Node* child = _children[j];
                    assert(child->_shift < _shift);
                    if ((child->_prefix & get_prefix_mask) != _prefix) {
                        printf("%llx : %d\n", _prefix, _shift);
                        printf("%llx : %d\n", child->_prefix, child->_shift);
                    }
                    assert((child->_prefix & get_prefix_mask) == _prefix);
                    int child_index = get_index_for_key(child->_prefix);
                    Key select = (Key)1 << child_index;
                    assert(_bitmap & select);
                    if (popcount(_bitmap & (select - 1)) != j) {
                        printf("%llx : %d\n", _prefix, _shift);
                        printf("%llx : %d\n", child->_prefix, child->_shift);
                        printf("j: %d\n", j);
                        printf("child_index : %d\n", child_index);
                        printf("bitmap : %llx (%d)\n", _bitmap, popcount(_bitmap));
                        printf("popcount: %d\n", popcount(_bitmap & (select - 1)));
                    }
                    assert(popcount(_bitmap & (select - 1)) == j);
                }
            }
        }
        
        
        
        // TODO: Mutation must not escape the context (whatever that means),
        // making this way of constructing new objects quite brittle.
        //
        // However, stitching new objects out of parts of existing arrays is
        // also rather horrible!
        
        
        // Make a mutable clone.
        
        [[nodiscard]] Node* _Nonnull clone_with_capacity(size_t capacity) const {
            int count = popcount(_bitmap);
            assert((int)capacity >= count);
            Node* _Nonnull node = make(_prefix, _shift, (uint32_t)capacity, count, _bitmap);
            size_t item_size = has_children() ? sizeof(const Node*) : sizeof(T);
            memcpy(node->_children, _children, count * item_size);
            return node;
        }
        
        [[nodiscard]] Node* _Nonnull clone() const {
            return clone_with_capacity(popcount(_bitmap));
        }
        
        // Modify mutable; must be before publication
        
        void insert_child(Node const* _Nonnull new_child) {
            assert(has_children());
            Key key = new_child->_prefix;
            assert(prefix_includes_key(key));
            ++_debug_count;
            compressed_array_insert_for_index(_debug_capacity,
                                              _bitmap,
                                              _children,
                                              get_index_for_key(key),
                                              new_child);
        }
        
        Node const* _Nonnull exchange_child(Node const* _Nonnull new_child) {
            assert(has_children());
            Key key = new_child->_prefix;
            assert(prefix_includes_key(key));
            return compressed_array_exchange_for_index(_bitmap,
                                                       _children,
                                                       get_index_for_key(key),
                                                       new_child);
        }
        
        void insert_key_value(Key key, T value) {
            assert(has_values());
            assert(prefix_includes_key(key));
            ++_debug_count;
            compressed_array_insert_for_index(_debug_capacity,
                                              _bitmap,
                                              _values,
                                              get_index_for_key(key),
                                              value);
        }
        
        T exchange_key_value(Key key, T value) {
            assert(has_values());
            assert(prefix_includes_key(key));
            return compressed_array_insert_for_index(_bitmap,
                                                     _values,
                                                     get_index_for_key(key),
                                                     value);
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
    
    
    
    
} // namespace wry::array_mapped_trie


#endif /* array_mapped_trie_hpp */
