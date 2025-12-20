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
    typename KEY_TYPE = uint64_t,
    typename BITMAP_TYPE = uint32_t,
    int SYMBOL_WIDTH = 4>
    struct Node : GarbageCollected {
        
        
        static constexpr size_t KEY_WIDTH = sizeof(KEY_TYPE) * CHAR_BIT;
        static constexpr size_t BITMAP_WIDTH = sizeof(BITMAP_TYPE) * CHAR_BIT;
        
        static constexpr KEY_TYPE PREFIX_MASK = ~(KEY_TYPE)0 << SYMBOL_WIDTH;
        static constexpr KEY_TYPE INDEX_MASK = ~PREFIX_MASK;
        
        static_assert(BITMAP_WIDTH >= ((size_t)1 << SYMBOL_WIDTH));
                
        static void assert_valid_shift(int shift) {
            assert(0 <= shift); // non-negative
            assert(shift < KEY_WIDTH); // non-wrapping
            assert(!(shift % SYMBOL_WIDTH)); // a multiple of log2(M)
        }
        
        static void assert_valid_prefix_and_shift(KEY_TYPE prefix, int shift) {
            assert_valid_shift(shift);
            assert((prefix & ~(PREFIX_MASK << shift)) == 0);
        }
        
        static KEY_TYPE prefix_for_keylike_and_shift(KEY_TYPE keylike, int shift) {
            assert_valid_shift(shift);
            return keylike & (PREFIX_MASK << shift);
        }
        
        static int shift_for_keylike_difference(KEY_TYPE keylike_difference) {
            assert(keylike_difference != 0);
            using bit::clz;
            int shift = ((KEY_WIDTH - 1 - clz(keylike_difference)) / SYMBOL_WIDTH) * SYMBOL_WIDTH;
            assert_valid_shift(shift);
            assert((keylike_difference >> shift) && !((keylike_difference >> shift) >> SYMBOL_WIDTH));
            return shift;
        }
        
        // placement new
        static void* _Nonnull operator new(size_t count, void* _Nonnull ptr) {
            return ptr;
        }
        
        KEY_TYPE _prefix;
        int _shift;
        size_t _debug_capacity;
        size_t _debug_count;
        BITMAP_TYPE _bitmap; // bitmap of which items are present
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
        
        bool prefix_includes_key(KEY_TYPE key) const {
            return _prefix == (key & (~INDEX_MASK << _shift));
        }
        
        int get_index_for_key(KEY_TYPE key) const {
            assert(prefix_includes_key(key));
            return (int)((key >> _shift) & INDEX_MASK);
        }
        
        bool bitmap_includes_key(KEY_TYPE key) const {
            int index = get_index_for_key(key);
            return bitmap_get_for_index(_bitmap, index);
        }
        
        int get_compressed_index_for_index(int index) const {
            return compressed_array_get_compressed_index_for_index(_bitmap, index);
        }
        
        int get_compressed_index_for_key(KEY_TYPE key) const {
            int index = get_index_for_key(key);
            return get_compressed_index_for_index(index);
        }
        
       
        
        
        
        
        
        Node(KEY_TYPE prefix,
             int shift,
             size_t debug_capacity,
             size_t debug_count,
             BITMAP_TYPE bitmap)
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
        make(KEY_TYPE prefix,
             int shift,
             size_t capacity,
             size_t count,
             BITMAP_TYPE bitmap)
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
        make_singleton(KEY_TYPE key,
                       T value)
        {
            KEY_TYPE prefix = key & PREFIX_MASK;
            int shift = 0;
            size_t capacity = 1;
            size_t count = 1;
            KEY_TYPE index = key & INDEX_MASK;
            BITMAP_TYPE bitmap = (BITMAP_TYPE)1 << (BITMAP_TYPE)(index);
            Node* _Nonnull new_node = Node::make(prefix,
                                                 shift,
                                                 capacity,
                                                 count,
                                                 bitmap);
            new_node->_values[0] = std::move(value);
            return new_node;
        }
        
       

        bool contains(KEY_TYPE key) const {
            if (!prefix_includes_key(key)) {
                // prefix excludes key
                return false;
            }
            if (!bitmap_includes_key(key)) {
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
        
        [[nodiscard]] bool try_get(KEY_TYPE key, T& victim) const {
            if (!prefix_includes_key(key)) {
                return false;
            }
            if (!bitmap_includes_key(key)) {
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
            KEY_TYPE key = new_child->_prefix;
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
            KEY_TYPE key = new_child->_prefix;
            assert(prefix_includes_key(key));
            return compressed_array_exchange_for_index(_bitmap,
                                                       _children,
                                                       get_index_for_key(key),
                                                       new_child);
        }
        
        void insert_key_value(KEY_TYPE key, T value) {
            assert(has_values());
            assert(prefix_includes_key(key));
            ++_debug_count;
            compressed_array_insert_for_index(_debug_capacity,
                                              _bitmap,
                                              _values,
                                              get_index_for_key(key),
                                              value);
        }
        
        T exchange_key_value(KEY_TYPE key, T value) {
            assert(has_values());
            assert(prefix_includes_key(key));
            return compressed_array_insert_for_index(_bitmap,
                                                     _values,
                                                     get_index_for_key(key),
                                                     value);
        }
        
        
        
        
        
        KEY_TYPE prefix_mask() const {
            return PREFIX_MASK << _shift;
        }
        
        static bool prefixes_are_disjoint(Node const* _Nullable a,
                                          Node const* _Nullable b) {
            return (a->_prefix ^ b->_prefix) & (a->prefix_mask() & b->prefix_mask());
        }
        
        
        [[nodiscard]] static Node const* _Nullable merge(Node const* _Nullable a, Node const* _Nullable b) {
            
            if (!b)
                return a;
            
            if (!a)
                return b;
            
            if (prefixes_are_disjoint(a, b)) {
                // key sets are disjoint
                int shift = shift_for_keylike_difference(a->_prefix ^ b->_prefix);
                assert(shift > a->_shift);
                assert(shift > b->_shift);
                Node* c = make(prefix_for_keylike_and_shift(a->_prefix, shift),
                               shift,
                               2,
                               0,
                               0);
                c->insert_child(a);
                c->insert_child(b);
                return c;
            }
            
            // prefix is common (siblings)
            if ((a->_shift == 0) && (b->_shift == 0)) {
                assert(a->_prefix == b->_prefix);
                BITMAP_TYPE c_bitmap = a->_bitmap | b->_bitmap;
                int count = popcount(c_bitmap);
                Node* c = make(a->_prefix,
                               0,
                               count,
                               count,
                               c_bitmap);
                auto q = c->_values;
                while (c_bitmap) {
                    int i = bit::ctz(c_bitmap);
                    BITMAP_TYPE j = (BITMAP_TYPE)1 << i;
                    if (a->_bitmap & j) {
                        int k = popcount((j-1) & a->_bitmap);
                        *q++ = a->_values[k];
                    } else {
                        assert(b->_bitmap & j);
                        int k = popcount((j-1) & b->_bitmap);
                        *q++ = b->_values[k];
                    }
                    c_bitmap ^= j;
                }
                return c;
            }
            if (a->_shift == b->_shift) {
                assert(a->_prefix == b->_prefix);
                BITMAP_TYPE c_bitmap = a->_bitmap | b->_bitmap;
                int count = popcount(c_bitmap);
                Node* c = make(a->_prefix,
                               a->_shift,
                               count,
                               count,
                               c_bitmap);
                auto q = c->_children;
                while (c_bitmap) {
                    int i = bit::ctz(c_bitmap);
                    BITMAP_TYPE j = (BITMAP_TYPE)1 << i;
                    Node const* _Nullable new_child = nullptr;
                    if (a->_bitmap & j) {
                        int k = popcount((j-1) & a->_bitmap);
                        new_child = a->_children[k];
                    }
                    if (b->_bitmap & j) {
                        int k = popcount((j-1) & b->_bitmap);
                        new_child = (new_child
                                     ? merge(new_child, b->_children[k])
                                     : b->_children[k]);
                    }
                    *q++ = new_child;
                    c_bitmap ^= j;
                }
                return c;
            }
            // prefix is common but level is not
            if (a->_shift > b->_shift) {
                Node* _Nullable c = a->clone_with_capacity(popcount(a->_bitmap) + 1);
                auto i = (b->_prefix >> a->_shift) & INDEX_MASK;
                auto j = (BITMAP_TYPE)1 << i;
                if (a->_bitmap & j) {
                    int k = popcount((j-1) & a->_bitmap);
                    c->exchange_child(merge(a->_children[k], b));
                } else {
                    c->insert_child(b);
                }
                return c;
            }

            if (a->_shift < b->_shift) {
                Node* _Nullable c = b->clone_with_capacity(popcount(b->_bitmap) + 1);
                auto i = (a->_prefix >> b->_shift) & INDEX_MASK;
                auto j = (BITMAP_TYPE)1 << i;
                if (b->_bitmap & j) {
                    int k = popcount((j-1) & b->_bitmap);
                    c->exchange_child(merge(a, b->_children[k]));
                } else {
                    c->insert_child(a);
                }
                return c;
            }

            assert(false);

        }

        
        
        [[nodiscard]] Node* _Nonnull clone_and_insert_child(const Node* _Nonnull new_child) const {
            assert(has_children());
            KEY_TYPE key = new_child->_prefix;
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
            KEY_TYPE key = new_child->_prefix;
            assert(prefix_includes_key(key));
            Node* _Nonnull new_node = clone_with_capacity(popcount(_bitmap));
            Node const* old_child = compressed_array_exchange_for_index(new_node->_bitmap,
                                                                        new_node->_children,
                                                                        get_index_for_key(key),
                                                                        new_child);
            // mutator_overwrote(old_child);
            return new_node;
        }
        
        Node* _Nonnull clone_and_erase_child_containing_key(KEY_TYPE key) const {
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
        
        
        
        
        [[nodiscard]] std::pair<Node* _Nonnull, bool> clone_and_insert_or_assign_key_value(KEY_TYPE key, T value, T& victim) const {
            if (!prefix_includes_key(key)) {
                return {
                    merge_disjoint(this,
                                   make_singleton(key,
                                                       value)),
                    true
                };
            }
            int index = get_index_for_key(key);
            KEY_TYPE select = bitmask_for_index<BITMAP_TYPE>(index);
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
        
        [[nodiscard]] std::pair<Node const* _Nullable, bool> clone_and_erase_key(KEY_TYPE key, T& victim) const {
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
                BITMAP_TYPE b = _bitmap;
                for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                    int j = bit::ctz(b);
                    KEY_TYPE key = _prefix | j;
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
                BITMAP_TYPE b = _bitmap;
                for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                    int j = bit::ctz(b);
                    KEY_TYPE key = _prefix | j;
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
                BITMAP_TYPE b = _bitmap;
                for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                    int j = bit::ctz(b);
                    KEY_TYPE key = _prefix | j;
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
                BITMAP_TYPE b = _bitmap;
                for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                    int j = bit::ctz(b);
                    KEY_TYPE key = _prefix | j;
                    co_await nursery.fork(action(key, _values[i]));
                }
            }
            co_await nursery.join();
        }
        
      
        template<typename Action>
        static void for_each_mask(Node const* _Nullable node, KEY_TYPE key, KEY_TYPE mask, Action&& action) {
            if (!node)
                return;
            if ((node->_prefix ^ key) & (node->prefix_mask() & mask))
                return;
            BITMAP_TYPE a = node->_bitmap;
            KEY_TYPE m = (INDEX_MASK << node->_shift) & mask;
            for (; a; a &= (a-1)) {
                int i = bit::ctz(a);
                auto key2 = ((KEY_TYPE)i << node->_shift) | node->_prefix;
                if ((key2 ^ key) & m)
                    continue;
                BITMAP_TYPE j = (BITMAP_TYPE)1 << i;
                int k = popcount(node->_bitmap & (j-1));
                if (node->has_children()) {
                    for_each_mask(node->_children[k], key, mask, action);
                } else {
                    action(key2, node->_values[k]);
                }
            }
        }
        
        
        
        static std::pair<Node const* _Nullable, Node const* _Nullable>
        partition_mask(Node const* _Nullable node,
                                   KEY_TYPE key,
                                   KEY_TYPE mask) {
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
        
        
                
        
        
        [[nodiscard]] static Node* _Nullable make_leaf_with_leading_pairs(auto& first, auto last) {
            if (first == last)
                return nullptr;
            auto first2 = first;
            KEY_TYPE prefix = {};
            int shift = 0;
            size_t count;
            BITMAP_TYPE bitmap = {};
            KEY_TYPE key = first2->first & ~INDEX_MASK;
            prefix = key & ~INDEX_MASK;
            for (;;) {
                ++count;
                auto index = key & INDEX_MASK;
                auto mask = (BITMAP_TYPE)1 << (key & INDEX_MASK);
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
            KEY_TYPE prefix_difference = a->_prefix ^ b->_prefix;
            int shift = shift_for_keylike_difference(prefix_difference);
            Node* new_node = make(prefix_for_keylike_and_shift(a->_prefix, shift),
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
                KEY_TYPE prefix_mask = ~INDEX_MASK << _shift;
                for (int j = 0; j != count; ++j) {
                    const Node* child = _children[j];
                    assert(child->_shift < _shift);
                    if ((child->_prefix & prefix_mask) != _prefix) {
                        printf("%llx : %d\n", _prefix, _shift);
                        printf("%llx : %d\n", child->_prefix, child->_shift);
                    }
                    assert((child->_prefix & prefix_mask) == _prefix);
                    int child_index = get_index_for_key(child->_prefix);
                    KEY_TYPE select = (KEY_TYPE)1 << child_index;
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
