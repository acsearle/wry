//
//  array_mapped_trie.hpp
//  client
//
//  Created by Antony Searle on 12/7/2025.
//

#ifndef array_mapped_trie_hpp
#define array_mapped_trie_hpp

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include "compressed_array.hpp"
#include "garbage_collected.hpp"
#include "variant.hpp"
#include "coroutine.hpp"
#include "bit.hpp"
#include "stdint.hpp"


namespace wry {

    // A radix trie over a fixed-width unsigned Word, branching SYMBOL_WIDTH
    // bits at a time.  Key-first to match the project's container convention.
    // Bitmap is derived (one presence bit per child slot), not a free knob;
    // SYMBOL_WIDTH defaults to 5 (32-way nodes).
    template<
    typename Word,
    typename T,
    typename Discipline,
    int SYMBOL_WIDTH = 5>
    struct ArrayMappedTrie : Discipline::IntrusiveAllocator {

        using Bitmap = unsigned_integer_of_bit_width_t<((std::size_t)1 << SYMBOL_WIDTH)>;

        // A set (empty value type) carries no per-leaf payload: the bitmap is
        // the membership, so leaves allocate zero value bytes and the value
        // operations below collapse to bitmap work.
        static constexpr bool _is_set = std::is_empty_v<T>;
        static constexpr size_t _leaf_item_bytes = _is_set ? 0 : sizeof(T);

        static constexpr size_t WORD_WIDTH = sizeof(Word) * CHAR_BIT;
        static constexpr size_t BITMAP_WIDTH = sizeof(Bitmap) * CHAR_BIT;

        static constexpr Word PREFIX_MASK = ~(Word)0 << SYMBOL_WIDTH;
        static constexpr Word INDEX_MASK = ~PREFIX_MASK;
        static constexpr int RADIX_LOG2 = SYMBOL_WIDTH; // bits consumed per level

        static_assert(BITMAP_WIDTH >= ((size_t)1 << SYMBOL_WIDTH));

        static void assert_valid_shift(int shift) {
            assert(0 <= shift); // non-negative
            assert(shift < WORD_WIDTH); // non-wrapping
            assert(!(shift % SYMBOL_WIDTH)); // a multiple of log2(M)
        }

        static void assert_valid_prefix_and_shift(Word prefix, int shift) {
            assert_valid_shift(shift);
            assert((prefix & ~(PREFIX_MASK << shift)) == 0);
        }

        static Word prefix_mask_for_shift(int shift) {
            return PREFIX_MASK << shift;
        }

        static Word prefix_from_key_and_shift(Word key, int shift) {
            assert_valid_shift(shift);
            return key & prefix_mask_for_shift(shift);
        }

        static int shift_from_keys(Word a, Word b) {
            Word d = a ^ b;
            assert(d);
            using bit::clz;
            // shift is the position of the most significant differing bit,
            // rounded down to a multiple of SYMBOL_WIDTH;
            // SYMBOL_WIDTH may not be a power of two
            int shift = ((unsigned)(WORD_WIDTH - 1 - clz(d)) / SYMBOL_WIDTH) * SYMBOL_WIDTH;
            assert_valid_shift(shift);
            assert(!(d & (PREFIX_MASK << shift))); // prefix is common
            assert((d >> shift) & INDEX_MASK);     // indices are disjoint
            return shift;
        }

        static void* _Nonnull operator new(std::size_t count, void* _Nonnull ptr) {
            return ptr;
        }

        Word _prefix;
        int _shift;
#ifndef NDEBUG
        size_t _debug_capacity;
        size_t _debug_count;
#endif
        Bitmap _bitmap; // bitmap of which items are present
        union {
            // compressed flexible member array of children or values
            ArrayMappedTrie const* _Nonnull _children[];
            T _values[];
        };

        Word get_prefix_mask() const {
            return PREFIX_MASK << _shift;
        }

        static bool prefixes_are_disjoint(ArrayMappedTrie const* _Nullable a,
                                          ArrayMappedTrie const* _Nullable b) {
            return (a->_prefix ^ b->_prefix) & (a->get_prefix_mask() & b->get_prefix_mask());
        }

        bool prefix_includes_key(Word key) const {
            return _prefix == (key & get_prefix_mask());
        }

        int get_index_for_key(Word key) const {
            assert(prefix_includes_key(key));
            return (int)((key >> _shift) & INDEX_MASK);
        }

        bool bitmap_includes_key(Word key) const {
            int index = get_index_for_key(key);
            return bitmap_get_for_index(_bitmap, index);
        }

        int get_compressed_index_for_index(int index) const {
            return compressed_array_get_compressed_index_for_index(_bitmap, index);
        }

        int get_compressed_index_for_key(Word key) const {
            int index = get_index_for_key(key);
            return get_compressed_index_for_index(index);
        }

        bool has_children() const {
            return _shift;
        }

        bool has_values() const {
            return !has_children();
        }









        ArrayMappedTrie(Word prefix,
                        int shift,
                        size_t debug_capacity,
                        size_t debug_count,
                        Bitmap bitmap)
        : _prefix(prefix)
        , _shift(shift)
#ifndef NDEBUG
        , _debug_capacity(debug_capacity)
        , _debug_count(debug_count)
#endif
        , _bitmap(bitmap) {
            using bit::popcount;
            assert(_debug_capacity >= popcount(_bitmap));
            assert(_debug_count >= popcount(_bitmap));
            assert(_debug_count <= _debug_capacity);
        }

        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
            printf("    _gray %04x\n", this->_gray.load_relaxed());
            printf("    _black %04x\n", this->_black);
            printf("    _count %u\n", this->_count.load_relaxed());
#ifndef NDEBUG
            printf("    _debug_allocation_gray %04x\n", this->_debug_allocation_gray);
            printf("    _debug_allocation_black %04x\n", this->_debug_allocation_black);
            printf("    _debug_allocation_epoch %d\n", this->_debug_allocation_epoch);
#endif
        }

        virtual void _garbage_collected_scan() const override {
            int compressed_size = std::popcount(_bitmap);
            if (has_children()) {
                assert(compressed_size <= _debug_capacity);
                for (int i = 0; i != compressed_size; ++i)
                    garbage_collected_scan(_children[i]);
            } else {
                if constexpr (!_is_set)
                    for (int i = 0; i != compressed_size; ++i)
                        garbage_collected_scan(_values[i]);
            }
        }


        [[nodiscard]] static ArrayMappedTrie* _Nonnull
        make(Word prefix,
             int shift,
             size_t capacity,
             size_t count,
             Bitmap bitmap)
        {
            size_t item_bytes = shift ? sizeof(const ArrayMappedTrie*) : _leaf_item_bytes;
            void* _Nonnull pointer = GarbageCollected::operator new(sizeof(ArrayMappedTrie) + (capacity * item_bytes));
            return new(pointer) ArrayMappedTrie(prefix,
                                                shift,
                                                capacity,
                                                count,
                                                bitmap);
        }

        [[nodiscard]] static ArrayMappedTrie* _Nonnull
        make_singleton(Word key,
                       T value)
        {
            Word prefix = key & PREFIX_MASK;
            int shift = 0;
            size_t capacity = 1;
            size_t count = 1;
            Word index = key & INDEX_MASK;
            Bitmap bitmap = (Bitmap)1 << (Bitmap)(index);
            ArrayMappedTrie* _Nonnull new_node = ArrayMappedTrie::make(prefix,
                                                                       shift,
                                                                       capacity,
                                                                       count,
                                                                       bitmap);
            if constexpr (!_is_set) new_node->_values[0] = std::move(value);
            return new_node;
        }



        [[nodiscard]] bool contains(Word key) const {
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

        [[nodiscard]] bool try_get(Word key, T& victim) const {
            if (!prefix_includes_key(key)) {
                return false; // key is excluded by the prefix
            }
            if (!bitmap_includes_key(key)) {
                return false; // key is excluded by the bitmap
            }
            int compressed_index = get_compressed_index_for_key(key);
            if (!has_children()) {
                if constexpr (!_is_set) victim = _values[compressed_index];
                return true; // bitmap is authoritative for leaves
            }
            return _children[compressed_index]->try_get(key, victim);
        }

        [[nodiscard]] bool contains_any(Word key, Word mask) const {
            if ((_prefix ^ key) & get_prefix_mask() & mask) {
                return false; // masked key is excluded by the masked prefix
            }
            abort();
            // TODO: bit hacking
        }


        // Merge is fundamental
        // Naming: computed merge

        template<typename F>
        [[nodiscard]] static ArrayMappedTrie const* _Nullable
        merge(ArrayMappedTrie const* _Nullable a,
              ArrayMappedTrie const* _Nullable b,
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
            Word c_prefix = prefix_from_key_and_shift(a->_prefix, c_shift);

            Bitmap a_bitmap{};
            void const* a_array{};
            if (a->_shift == c_shift) {
                a_bitmap = a->_bitmap;
                a_array = a->_children;
            } else {
                // By mocking up a trivial c-level array we can uniformly handle all cases
                a_bitmap = (Word)1 << ((a->_prefix >> c_shift) & INDEX_MASK);
                a_array = &a;
            }

            Bitmap b_bitmap{};
            void const* b_array{};
            if (b->_shift == c_shift) {
                b_bitmap = b->_bitmap;
                b_array = b->_children;
            } else {
                b_bitmap = (Word)1 << ((b->_prefix >> c_shift) & INDEX_MASK);
                b_array = &b;
            }

            Bitmap c_bitmap = a_bitmap | b_bitmap;
            using bit::popcount;
            size_t c_count = popcount(c_bitmap);

            ArrayMappedTrie* c = make(c_prefix,
                                      c_shift,
                                      c_count,
                                      c_count,
                                      c_bitmap);
            if (c_shift) {
                merge_compressed_arrays(a_bitmap,
                                        b_bitmap,
                                        (ArrayMappedTrie const* _Nonnull const* _Nonnull)a_array,
                                        (ArrayMappedTrie const* _Nonnull const* _Nonnull)b_array,
                                        c->_children,
                                        [&resolver](ArrayMappedTrie const* _Nonnull a,
                                                    ArrayMappedTrie const* _Nonnull b) {
                    return merge(a, b, resolver);
                });
            } else if constexpr (!_is_set) {
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
        [[nodiscard]] static ArrayMappedTrie const* _Nullable
        merge(ArrayMappedTrie const* _Nullable a,
              ArrayMappedTrie const* _Nullable b) {
            return merge(a, b, [](T left, T) { return left; });
        }


        [[nodiscard]] static ArrayMappedTrie const* _Nonnull
        insert(ArrayMappedTrie const* _Nullable ArrayMappedTrie,
               Word key, T value) {
            // TODO: Defer make_singleton until is confirmed to be necessary
            return merge(make_singleton(key, value), ArrayMappedTrie);
        }








        // The other fundamental operation is to selectively erase elements


        static std::pair<ArrayMappedTrie const* _Nullable, ArrayMappedTrie const* _Nullable>
        partition_mask(ArrayMappedTrie const* _Nullable node,
                       Word key,
                       Word mask) {
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
            std::pair<ArrayMappedTrie const* _Nullable, ArrayMappedTrie const* _Nullable> result{};

            assert(node->has_children());
            auto n = __builtin_popcountg(node->_bitmap);
            auto p = node->_children;
            for (; n--; ++p) {
                auto [a, b] = partition_mask(*p, key, mask);
                if (a) {
                    if (result.first) {
                        result.first = merge(result.first, a);
                    } else {
                        result.first = a;
                    }
                }
                if (b) {
                    if (result.second) {
                        result.second = merge(result.second, b);
                    } else {
                        result.second = b;
                    }
                }
            }
            return result;

        }








        [[nodiscard]] ArrayMappedTrie* _Nonnull clone_and_insert_child(const ArrayMappedTrie* _Nonnull new_child) const {
            assert(has_children());
            Word key = new_child->_prefix;
            assert(prefix_includes_key(key));
            ArrayMappedTrie* _Nonnull new_node = clone_with_capacity(popcount(_bitmap) + 1);
            ArrayMappedTrie const* _Nullable _ = nullptr;
#ifndef NDEBUG
            ++(new_node->_debug_count);
#endif
            compressed_array_insert_for_index(new_node->_debug_capacity,
                                              new_node->_bitmap,
                                              new_node->_children,
                                              get_index_for_key(key),
                                              new_child);
            return new_node;
        }

        [[nodiscard]] ArrayMappedTrie* _Nonnull clone_and_assign_child(ArrayMappedTrie const* _Nonnull new_child) const {
            assert(has_children());
            Word key = new_child->_prefix;
            assert(prefix_includes_key(key));
            ArrayMappedTrie* _Nonnull new_node = clone_with_capacity(std::popcount(_bitmap));
            (void) compressed_array_exchange_for_index(new_node->_bitmap,
                                                       new_node->_children,
                                                       get_index_for_key(key),
                                                       new_child);
            return new_node;
        }

        [[nodiscard]] ArrayMappedTrie* _Nonnull clone_and_erase_child_containing_key(Word key) const {
            assert(has_children());
            ArrayMappedTrie* new_node = clone_with_capacity(popcount(_bitmap));
            [[maybe_unused]] ArrayMappedTrie const* _ = nullptr;
            bool did_erase = compressed_array_erase_for_index(new_node->_bitmap,
                                                              new_node->_children,
                                                              get_index_for_key(key),
                                                              _);
            assert(did_erase);
#ifndef NDEBUG
            --(new_node->_debug_count);
#endif
            return new_node;
        }




        [[nodiscard]] std::pair<ArrayMappedTrie* _Nonnull, bool> clone_and_insert_or_assign_key_value(Word key, T value, T& victim) const {
            if (!prefix_includes_key(key)) {
                return {
                    merge_disjoint(this,
                                   make_singleton(key,
                                                  value)),
                    true
                };
            }
            int index = get_index_for_key(key);
            Word select = bitmask_for_index<Bitmap>(index);
            int compressed_index = get_compressed_index_for_index(index);
            ArrayMappedTrie* _Nonnull new_node = clone_with_capacity(std::popcount(_bitmap | select));
#ifndef NDEBUG
            new_node->_debug_count = std::popcount(_bitmap | select);
#endif
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
                ArrayMappedTrie* _Nullable new_child = nullptr;
                if (_bitmap & select) {
                    const ArrayMappedTrie* _Nonnull child = _children[compressed_index];
                    std::tie(new_child, leaf_did_assign) = child->clone_and_insert_or_assign_key_value(key, value, victim);
                } else {
                    new_child = make_singleton(key, value);
                }
                ArrayMappedTrie const* _Nullable _ = nullptr;
                (void) compressed_array_insert_or_exchange_for_index(new_node->_debug_capacity,
                                                                     new_node->_bitmap,
                                                                     new_node->_children,
                                                                     index,
                                                                     new_child,
                                                                     _);
            }
            return { new_node, leaf_did_assign };
        }

        [[nodiscard]] std::pair<ArrayMappedTrie const* _Nullable, bool> clone_and_erase_key(Word key, T& victim) const {
            // TODO: Do we handle all cases correctly?
            // - Replacing a count one ArrayMappedTrie with nullptr
            // - Replacing a count two ArrayMappedTrie with surviving child
            if (!prefix_includes_key(key) || !bitmap_includes_key(key))
                // Word not present
                return { this, false };
            int compressed_index = get_compressed_index_for_key(key);
            if (has_children()) {
                const ArrayMappedTrie* _Nonnull child = _children[compressed_index];
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
                ArrayMappedTrie* _Nonnull new_node = clone();
                // TODO: we allocate enough for the clone then erase one
                int index = get_index_for_key(key);
                compressed_array_erase_for_index(new_node->_bitmap,
                                                 new_node->_values,
                                                 index,
                                                 victim);
                return { new_node, true };
            }
        }

        void for_each(auto&& action) const {
            if (has_children()) {
                int n = std::popcount(_bitmap);
                for (int i = 0; i != n; ++i)
                    _children[i]->for_each(action);
            } else {
                Bitmap b = _bitmap;
                for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                    int j = bit::ctz(b);
                    Word key = _prefix | j;
                    if constexpr (_is_set) action(key, T{}); else action(key, _values[i]);
                }
            }
        }

        Coroutine::Task coroutine_parallel_for_each(auto&& action) const {
            if (has_children()) {
                int n = std::popcount(_bitmap);
                Coroutine::Nursery nursery;
                for (int i = 0; i != n; ++i)
                    co_await nursery.fork(_children[i]->coroutine_parallel_for_each(action));
                co_await nursery.join();
            } else {
                Bitmap b = _bitmap;
                for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                    int j = bit::ctz(b);
                    Word key = _prefix | j;
                    if constexpr (_is_set) action(key, T{}); else action(key, _values[i]);
                }
            }
        }

        Coroutine::Task coroutine_parallel_for_each_coroutine(auto&& action) const {
            Coroutine::Nursery nursery;
            if (has_children()) {
                int n = popcount(_bitmap);
                for (int i = 0; i != n; ++i)
                    co_await nursery.fork(_children[i]->coroutine_parallel_for_each_coroutine(action));
            } else {
                Bitmap b = _bitmap;
                for (int i = 0; b != 0; ++i, (b &= (b-1))) {
                    int j = bit::ctz(b);
                    Word key = _prefix | j;
                    if constexpr (_is_set) co_await nursery.fork(action(key, T{}));
                    else co_await nursery.fork(action(key, _values[i]));
                }
            }
            co_await nursery.join();
        }


        template<typename Action>
        static void for_each_mask(ArrayMappedTrie const* _Nullable node, Word key, Word mask, Action&& action) {
            if (!node)
                return;
            if ((node->_prefix ^ key) & (node->get_prefix_mask() & mask))
                return;
            Bitmap a = node->_bitmap;
            Word m = (INDEX_MASK << node->_shift) & mask;
            for (; a; a &= (a-1)) {
                int i = bit::ctz(a);
                auto key2 = ((Word)i << node->_shift) | node->_prefix;
                if ((key2 ^ key) & m)
                    continue;
                Bitmap j = (Bitmap)1 << i;
                int k = std::popcount(node->_bitmap & (j-1));
                if (node->has_children()) {
                    for_each_mask(node->_children[k], key, mask, action);
                } else {
                    if constexpr (_is_set) action(key2, T{}); else action(key2, node->_values[k]);
                }
            }
        }







        // ---- Parallel rebuild (Stage 1) ----------------------------------
        //
        // Rebuild `source` (which may be null) by applying, for each modifier
        // key, the change produced by `combine`.  `mods` is sorted ascending by
        // key (.first) with no duplicate keys; [i, j) is the slice this call
        // owns.
        //
        //   combine(const T* old_or_null, const Action&) -> std::optional<T>
        //     old_or_null : existing value at this key in `source`, or null
        //     returns      : the new value, or nullopt to leave the key absent
        //
        // Parallel over the trie via a Nursery; a subtree with no modifier key
        // in its range is returned by pointer, unchanged (the dominant saving).
        // Frozen-phase only: `source` must stay immutable for the call.

        template<typename Action, typename Combine>
        [[nodiscard]] static const ArrayMappedTrie* _Nullable
        rebuild_serial(const ArrayMappedTrie* _Nullable source,
                       const std::vector<std::pair<Word, Action>>& mods,
                       size_t i, size_t j,
                       const Combine& combine) {
            // Apply each modifier in turn.  Uses try_get + insert/erase, which
            // are correct for arbitrary keys (insert/merge handle disjoint
            // prefixes), so this is a valid base case for any node -- including
            // a leaf that only covers part of the modifier range.
            const ArrayMappedTrie* acc = source;
            for (; i != j; ++i) {
                Word key = mods[i].first;
                T old;
                bool has = acc && acc->try_get(key, old);
                std::optional<T> next = combine(has ? &old : nullptr, mods[i].second);
                if (next) {
                    acc = insert(acc, key, std::move(*next));
                } else if (has) {
                    T victim;
                    acc = acc->clone_and_erase_key(key, victim).first;
                }
            }
            return acc;
        }

        template<typename Action, typename Combine>
        [[nodiscard]] static Coroutine::Future<const ArrayMappedTrie*>
        _rebuild_inrange(const ArrayMappedTrie* source, // internal; mods in range; a<b
                         const std::vector<std::pair<Word, Action>>& mods,
                         size_t a, size_t b,
                         const Combine& combine) {
            const int sh = source->_shift;
            const Word prefix = source->_prefix;
            const int nchild = std::popcount(source->_bitmap);
            auto index_of = [sh](Word key) -> int {
                return (int)((key >> sh) & INDEX_MASK);
            };

            // Merge-walk the source's children (in index order) against runs of
            // modifier keys grouped by their index at this level.  Deriving the
            // index straight from the key avoids any (c+1)<<sh overflow.
            struct Work { const ArrayMappedTrie* child; size_t lo, hi; };
            std::vector<Work> work;
            size_t p = a;
            int kc = 0;
            while (p < b || kc < nchild) {
                int mod_index = (p < b) ? index_of(mods[p].first) : (1 << SYMBOL_WIDTH);
                int child_index = (kc < nchild)
                    ? index_of(source->_children[kc]->_prefix) : (1 << SYMBOL_WIDTH);
                int c = mod_index < child_index ? mod_index : child_index;
                size_t q = p;
                while (q < b && index_of(mods[q].first) == c)
                    ++q;
                const ArrayMappedTrie* child = nullptr;
                if (child_index == c) {
                    child = source->_children[kc];
                    ++kc;
                }
                work.push_back(Work{child, p, q});
                p = q;
            }

            std::vector<const ArrayMappedTrie*> outs(work.size(), nullptr);
            {
                Coroutine::Nursery nursery;
                for (size_t t = 0; t != work.size(); ++t)
                    co_await nursery.fork(outs[t],
                        coroutine_parallel_rebuild(work[t].child, mods,
                                                   work[t].lo, work[t].hi, combine));
                co_await nursery.join();
            }

            // Assemble the surviving disjoint children (already in index order),
            // collapsing to honour the ">= 2 children" invariant.
            int nz = 0;
            const ArrayMappedTrie* only = nullptr;
            for (const ArrayMappedTrie* c : outs)
                if (c) { ++nz; only = c; }
            if (nz == 0)
                co_return nullptr;
            if (nz == 1)
                co_return only;
            ArrayMappedTrie* node = make(prefix, sh, nz, 0, 0);
            for (const ArrayMappedTrie* c : outs)
                if (c) node->insert_child(c);
            co_return node;
        }

        template<typename Action, typename Combine>
        [[nodiscard]] static Coroutine::Future<const ArrayMappedTrie*>
        coroutine_parallel_rebuild(const ArrayMappedTrie* _Nullable source,
                                   const std::vector<std::pair<Word, Action>>& mods,
                                   size_t i, size_t j,
                                   const Combine& combine) {
            if (i == j)
                co_return source;                              // share, no mods
            if (!source || source->has_values() || (j - i) < 2)
                co_return rebuild_serial(source, mods, i, j, combine);

            const int sh = source->_shift;
            const Word prefix = source->_prefix;
            auto lb = [&](size_t lo, size_t hi, Word bound) -> size_t {
                return (size_t)(std::lower_bound(
                    mods.begin() + lo, mods.begin() + hi, bound,
                    [](const std::pair<Word, Action>& e, Word v) { return e.first < v; })
                    - mods.begin());
            };

            // Split mods into the part inside this node's prefix range [a, b)
            // and the disjoint parts to either side.
            size_t a = lb(i, j, prefix);
            size_t b;
            if ((size_t)sh + (size_t)SYMBOL_WIDTH >= WORD_WIDTH) {
                b = j;                                         // node reaches the top of the word
            } else {
                Word upper = prefix + ((Word)1 << (sh + SYMBOL_WIDTH));
                b = (upper <= prefix) ? j : lb(a, j, upper);   // upper<=prefix => it wrapped
            }

            const ArrayMappedTrie* inside =
                (a == b) ? source
                         : co_await _rebuild_inrange(source, mods, a, b, combine);

            const ArrayMappedTrie* result = inside;
            if (a > i)
                result = merge(rebuild_serial(nullptr, mods, i, a, combine), result);
            if (j > b)
                result = merge(result, rebuild_serial(nullptr, mods, b, j, combine));
            co_return result;
        }


        // Merge two disjoint ArrayMappedTries by making them the children of a higher
        // level ArrayMappedTrie
        [[nodiscard]] static ArrayMappedTrie* _Nonnull merge_disjoint(ArrayMappedTrie const* _Nonnull a, ArrayMappedTrie const* _Nonnull b) {
            assert(a && b);
            assert((a->_prefix ^ b->_prefix) & PREFIX_MASK);
            int shift = shift_from_keys(a->_prefix, b->_prefix);
            assert(shift > a->_shift);
            assert(shift > b->_shift);
            ArrayMappedTrie* new_node = make(prefix_from_key_and_shift(a->_prefix, shift),
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
                Word get_prefix_mask = ~INDEX_MASK << _shift;
                for (int j = 0; j != count; ++j) {
                    const ArrayMappedTrie* child = _children[j];
                    assert(child->_shift < _shift);
                    if ((child->_prefix & get_prefix_mask) != _prefix) {
                        printf("%llx : %d\n", _prefix, _shift);
                        printf("%llx : %d\n", child->_prefix, child->_shift);
                    }
                    assert((child->_prefix & get_prefix_mask) == _prefix);
                    int child_index = get_index_for_key(child->_prefix);
                    Word select = (Word)1 << child_index;
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

        [[nodiscard]] ArrayMappedTrie* _Nonnull clone_with_capacity(size_t capacity) const {
            int count = std::popcount(_bitmap);
            assert((int)capacity >= count);
            ArrayMappedTrie* _Nonnull node = make(_prefix, _shift, (uint32_t)capacity, count, _bitmap);
            size_t item_size = has_children() ? sizeof(const ArrayMappedTrie*) : _leaf_item_bytes;
            memcpy(node->_children, _children, count * item_size);
            return node;
        }

        [[nodiscard]] ArrayMappedTrie* _Nonnull clone() const {
            return clone_with_capacity(std::popcount(_bitmap));
        }

        // Modify mutable; must be before publication

        void insert_child(ArrayMappedTrie const* _Nonnull new_child) {
            assert(has_children());
            Word key = new_child->_prefix;
            assert(prefix_includes_key(key));
#ifndef NDEBUG
            ++_debug_count;
#endif
            compressed_array_insert_for_index(_debug_capacity,
                                              _bitmap,
                                              _children,
                                              get_index_for_key(key),
                                              new_child);
        }

        ArrayMappedTrie const* _Nonnull exchange_child(ArrayMappedTrie const* _Nonnull new_child) {
            assert(has_children());
            Word key = new_child->_prefix;
            assert(prefix_includes_key(key));
            return compressed_array_exchange_for_index(_bitmap,
                                                       _children,
                                                       get_index_for_key(key),
                                                       new_child);
        }



    }; // ArrayMappedTrie

    template<typename Word, typename T, typename Discipline, int SYMBOL_WIDTH>
    void print(ArrayMappedTrie<Word, T, Discipline, SYMBOL_WIDTH> const* _Nullable s) {
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

} // namespace wry


#endif /* array_mapped_trie_hpp */
