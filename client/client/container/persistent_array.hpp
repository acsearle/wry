//
//  persistent_array.hpp
//  client
//
//  Created by Antony Searle on 31/5/2026.
//

#ifndef persistent_array_hpp
#define persistent_array_hpp

#include <cstddef>
#include <memory>
#include <utility>

#include "assert.hpp"
#include "garbage_collected.hpp"
#include "utility.hpp"

namespace wry {

    // PersistentArray<T> -- an immutable, contiguous, garbage-collected array.
    //
    // This is the deliberately-dumb placeholder for a persistent sequence: it
    // stores its elements in a flexible array member and every operation
    // (push/pop at either end, split, cat) allocates a fresh node and copies,
    // so they are all O(n).  The point is to establish the USAGE of a
    // persistent deque/sequence cheaply; once the call sites tell us which
    // operations actually matter we can swap in an asymptotically better
    // structure (finger tree / order-statistic B+ / RRB) behind a similar
    // interface.
    //
    // Like PersistentStack, there is no wrapper object: a sequence IS a
    // (possibly null) `PersistentArray<T> const*` that the caller owns and
    // rebinds, null meaning empty.  A live node therefore always has
    // `_size >= 1`.  The static operations build new nodes and return new
    // tops; nothing mutates a published node.  The node is built mutably while
    // still unpublished, then reached through pointer-to-const -- the
    // setup-then-freeze discipline in miniature.
    //
    // T is assumed nothrow-constructible; the multi-step builds below are not
    // exception-safe for a throwing T (the realistic payloads -- Value,
    // pointers, trivials -- are fine).

    template<typename T>
    struct PersistentArray : GarbageCollected {

        std::size_t _size;
        T _elements[] __counted_by(_size);

        // Placement new, declared locally so it is not hidden by the inherited
        // GarbageCollected allocator (see the placement-new lookup gotcha).
        static void* _Nonnull operator new(std::size_t, void* _Nonnull ptr) {
            return ptr;
        }

        virtual void _garbage_collected_debug() const override {
            printf("%s\n", __PRETTY_FUNCTION__);
        }

        virtual void _garbage_collected_scan() const override {
            using wry::garbage_collected_scan;
            for (std::size_t i = 0; i != _size; ++i)
                garbage_collected_scan(_elements[i]);
        }

        ~PersistentArray() override {
            std::destroy_n(_elements, _size);
        }

        // -- queries --

        static auto is_empty(PersistentArray const* _Nullable a) -> bool {
            return !a;
        }

        static auto size(PersistentArray const* _Nullable a) -> std::size_t {
            return a ? a->_size : 0;
        }

        // Lookup by position.  Precondition: i < size.
        [[nodiscard]] static auto at(PersistentArray const* _Nonnull a, std::size_t i) -> T {
            assert(a);
            assert(i < a->_size);
            return a->_elements[i];
        }

        // Precondition: non-empty.
        [[nodiscard]] static auto front(PersistentArray const* _Nonnull a) -> T {
            return at(a, 0);
        }

        [[nodiscard]] static auto back(PersistentArray const* _Nonnull a) -> T {
            assert(a);
            return at(a, a->_size - 1);
        }

        // -- construction --

        // Build from a contiguous source; null/empty source yields the empty
        // sequence.
        [[nodiscard]] static auto
        from(T const* _Nullable first, std::size_t count) -> PersistentArray const* _Nullable {
            if (!count)
                return nullptr;
            PersistentArray* a = _allocate(count);
            std::uninitialized_copy_n(first, count, a->_elements);
            return a;
        }

        // -- deque --

        [[nodiscard]] static auto
        push_back(PersistentArray const* _Nullable a, T value) -> PersistentArray const* _Nonnull {
            std::size_t n = size(a);
            PersistentArray* r = _allocate(n + 1);
            if (n)
                std::uninitialized_copy_n(a->_elements, n, r->_elements);
            std::construct_at(r->_elements + n, std::move(value));
            return r;
        }

        [[nodiscard]] static auto
        push_front(PersistentArray const* _Nullable a, T value) -> PersistentArray const* _Nonnull {
            std::size_t n = size(a);
            PersistentArray* r = _allocate(n + 1);
            std::construct_at(r->_elements, std::move(value));
            if (n)
                std::uninitialized_copy_n(a->_elements, n, r->_elements + 1);
            return r;
        }

        // Precondition: non-empty.
        [[nodiscard]] static auto
        pop_back(PersistentArray const* _Nonnull a) -> PersistentArray const* _Nullable {
            assert(a && a->_size);
            std::size_t n = a->_size - 1;
            if (!n)
                return nullptr;
            PersistentArray* r = _allocate(n);
            std::uninitialized_copy_n(a->_elements, n, r->_elements);
            return r;
        }

        // Precondition: non-empty.
        [[nodiscard]] static auto
        pop_front(PersistentArray const* _Nonnull a) -> PersistentArray const* _Nullable {
            assert(a && a->_size);
            std::size_t n = a->_size - 1;
            if (!n)
                return nullptr;
            PersistentArray* r = _allocate(n);
            std::uninitialized_copy_n(a->_elements + 1, n, r->_elements);
            return r;
        }

        // -- split / cat --

        [[nodiscard]] static auto
        cat(PersistentArray const* _Nullable a, PersistentArray const* _Nullable b)
        -> PersistentArray const* _Nullable {
            std::size_t na = size(a);
            std::size_t nb = size(b);
            if (!na)
                return b;
            if (!nb)
                return a;
            PersistentArray* r = _allocate(na + nb);
            std::uninitialized_copy_n(a->_elements, na, r->_elements);
            std::uninitialized_copy_n(b->_elements, nb, r->_elements + na);
            return r;
        }

        // Split into [0, i) and [i, size).  Precondition: i <= size.  The
        // boundary cases share the input rather than copy.
        [[nodiscard]] static auto
        split(PersistentArray const* _Nullable a, std::size_t i)
        -> std::pair<PersistentArray const* _Nullable, PersistentArray const* _Nullable> {
            std::size_t n = size(a);
            assert(i <= n);
            if (i == 0)
                return { nullptr, a };
            if (i == n)
                return { a, nullptr };
            PersistentArray* l = _allocate(i);
            PersistentArray* r = _allocate(n - i);
            std::uninitialized_copy_n(a->_elements, i, l->_elements);
            std::uninitialized_copy_n(a->_elements + i, n - i, r->_elements);
            return { l, r };
        }

    private:

        explicit PersistentArray(std::size_t n) : _size(n) {}

        // Allocate a node sized for n elements via the GC allocator, then
        // placement-new the header.  The caller constructs the n elements.
        [[nodiscard]] static auto _allocate(std::size_t n) -> PersistentArray* _Nonnull {
            void* _Nonnull p = GarbageCollected::operator new(sizeof(PersistentArray) + n * sizeof(T));
            return new(p) PersistentArray(n);
        }

    };

} // namespace wry

#endif /* persistent_array_hpp */
