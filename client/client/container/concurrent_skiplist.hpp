//
//  concurrent_skiplist.hpp
//  client
//
//  Created by Antony Searle on 23/11/2024.
//

#ifndef concurrent_skiplist_hpp
#define concurrent_skiplist_hpp


#include <optional>
#include <tuple>

#include "assert.hpp"
#include "epoch_allocator.hpp"
#include "garbage_collected.hpp"
#include "key_service.hpp"
#include "utility.hpp"

namespace wry {

    // Concurrent skiplist
    //
    // size() and erase() lack a compelling use case at the moment are not
    // currently implemented.  Both would likely involve significant changes
    // and introduce per-operation overhead.
    // - size() would require maintaining an essentially separate concurrent
    //   counter.
    // - erase() would require upgrading each level into a Harris-Michael linked
    //   list, with a stolen pointer bit marking deletion, and every (mutating)
    //   traversal required to help in the cleanup.
    
    // References:
    // Tim Harris, "A Pragmatic Implementation of Non-Blocking Linked-Lists," DISC 2001
    // Maged Michael, "High Performance Dynamic Lock-Free Hash Tables and List-Based Sets," SPAA 2002
    // Fraser & Harris, "Concurrent Programming Without Locks," TOCS 2007

    namespace _skiplist_detail {

        // Constinit to a nonzero value.  Keeps the thread_local as cheap as possible.
        // We don't really care that the threads start out synchronized
        // TODO: On pool thread entry, replace value with something better
        constinit inline thread_local uint64_t prng_state = 0x9E3779B97F4A7C15ULL;

        inline uint64_t xorshift64() {
            uint64_t x = prng_state;
            x ^= x << 13;
            x ^= x >>  7;
            x ^= x << 17;
            prng_state = x;
            assert(x != 0);
            return x;
        }

        struct LoadNonatomic {
            auto operator()(auto const& x) const {
                return x.nonatomic_load();
            }
        };

        struct LoadAcquire {
            auto operator()(auto const& x) const {
                return x.load_acquire();
            }
        };

        template<typename Key, typename Compare, typename Discipline, typename Loader>
        struct BasicIterator;

        template<typename Key, typename Compare, typename Discipline>
        struct Node : Discipline::IntrusiveAllocator {

            static_assert(std::is_empty_v<Compare>);

            static constexpr size_t MAX_HEIGHT = 64;

            using Slot = Discipline::template AtomicSlot<Node const* _Nullable>;

            // Local placement-new: GarbageCollected's `operator new(size_t)`
            // would otherwise hide the global placement form via class-scope
            // name lookup, breaking `new(raw) Node` inside `with_height_emplace()`.
            static void* _Nonnull  operator new(size_t count, void* _Nonnull ptr) {
                return ptr;
            }

            Key _key;
            Atomic<size_t> _height;
            Slot _next[];

            explicit Node(size_t n, auto&&... args)
            : Discipline::IntrusiveAllocator()
            , _key(FORWARD(args)...)
            , _height(n) {
            }

            // The head node is a node
            // - whose _key field is default constructed and never accessed
            // - whose _next array has MAX_HEIGHT elements
            // - whose _height field is the atomically updated max node height
            static Node* _Nonnull make_head() {
                size_t number_of_bytes = sizeof(Node) + sizeof(*_next) * MAX_HEIGHT;
                void* _Nonnull raw = Discipline::IntrusiveAllocator::operator new(number_of_bytes,
                                                                                  std::align_val_t{alignof(Node)});
                std::memset(raw, 0, number_of_bytes);
                return new(raw) Node(1);
            }

            // Non-head nodes
            // - have an immutable _key field
            // - are ordered by Compare{} on _key
            // - have an immutable _height field
            // - have a _next array with _height elements
            static Node* _Nonnull with_height_emplace(size_t n, auto&&... args) {
                size_t number_of_bytes = sizeof(Node) + sizeof(*_next) * n;
                // Not checked; we accept crashing on OOM.
                void* _Nonnull raw = Discipline::IntrusiveAllocator::operator new(number_of_bytes,
                                                                                  std::align_val_t{alignof(Node)});
                std::memset(raw, 0, number_of_bytes);
                return new(raw) Node(n, FORWARD(args)...);
            }

            // The one place a key is built from caller arguments.  Two
            // conventions, told apart by the comparator:
            // - set: the arguments construct the Key.
            // - map (Compare is a ComparePair, which exposes mapped_type):
            //   std::map::try_emplace semantics -- keylike is the key, the
            //   remaining arguments construct the mapped value in place, and
            //   an absent value is value-initialized.  Piecewise, so a
            //   non-movable mapped type (Atomic) needs no move.
            static Node* _Nonnull with_random_height_emplace(auto&& keylike, auto&&... args) {
                size_t n = 1 + __builtin_ctzg(xorshift64());
                if constexpr (requires { typename Compare::mapped_type; })
                    return with_height_emplace(n, std::piecewise_construct,
                                               std::forward_as_tuple(FORWARD(keylike)),
                                               std::forward_as_tuple(FORWARD(args)...));
                else
                    return with_height_emplace(n, FORWARD(keylike), FORWARD(args)...);
            }

            // In GC mode (IntrusiveAllocator derives from GarbageCollected),
            // these methods' signatures match the base's pure virtuals and
            // implicitly override them — Node becomes a concrete GC type
            // with a vtable.  In bump mode the base has no matching
            // virtuals, these are just regular member functions, no vtable
            // is added, and the bodies are dead code.
            //
            // C++ forbids `requires` on virtual functions, so we can't
            // make the override conditional via constraints; the implicit-
            // override mechanism is what gives us "virtual only when the
            // base has the matching virtual."
            //
            // The level 0 linked list includes every skiplist node, so the
            // collector only needs to scan the key and trace _next[0]: an
            // upper slot only ever points at a node that is already in the
            // level 0 chain (_link_level links level 0 first), so upper
            // levels never introduce reachability.  The race between the
            // head's _height and the highest active index of _next is thus
            // irrelevant; the scan never loads _height.
            void _garbage_collected_scan() const {
                garbage_collected_scan(_key);
                garbage_collected_scan(_next[0].load_acquire());
            }

            void _garbage_collected_debug() const {
                printf("%s\n", __PRETTY_FUNCTION__);
            }

            // Forward declare substantial methods

            [[nodiscard]] std::pair<Node const* _Nullable, bool>
            _link_level(size_t i, Slot const* _Nonnull left, Node const* _Nullable expected, Node const* _Nonnull desired) const;

            template<typename Keylike, typename... Args>
            std::pair<Node const* _Nullable, bool>
            _try_emplace(size_t i, Slot const* _Nonnull left, Keylike&& keylike, Args&&... args) const;

            template<typename Keylike, typename... Args>
            std::pair<BasicIterator<Key, Compare, Discipline, LoadAcquire>, bool>
            try_emplace(Keylike&& keylike, Args&&... args) const;

            // Precondition: frozen
            [[nodiscard]] bool is_empty() const {
                return !_next[0].nonatomic_load();
            }

            // Precondition: frozen
            template<typename F>
            void for_each_child(Node const* _Nullable bound, F&& f) const {
                for (size_t i = _height.nonatomic_load(); i--;) {
                    Node const* _Nullable child = _next[i].nonatomic_load();
                    if (child != bound) {
                        assert(child);
                        assert(child->_height.nonatomic_load() == i + 1);
                        f(child, bound);
                        bound = child;
                    }
                }
            }

        }; // struct Node


        template<typename Key, typename Compare, typename Discipline, typename Loader>
        struct BasicIterator {

            using Node = Node<Key, Compare, Discipline>;

            // we can iterate across a live sequence but obviously that won't
            // be authoritative

            Node const* _Nullable current;

            bool operator==(const BasicIterator&) const = default;

            Key const& operator*() const {
                assert(current);
                return current->_key;
            }

            Key const* _Nonnull operator->() const {
                assert(current);
                return &(current->_key);
            }

            BasicIterator& operator++() {
                assert(current);
                current = Loader{}(current->_next[0]);
                return *this;
            }

            BasicIterator operator++(int) {
                BasicIterator old{current};
                operator++();
                return old;
            }

        }; // struct BasicIterator


        // Cursor for use after a "freeze" point — i.e., once all writers
        // to this skiplist have stopped and happens-before with the
        // current thread has been established (e.g. via an epoch advance).
        // Each step is a plain pointer chase via nonatomic_load; no
        // atomic loads, no fences.  Misuse during the live phase is a
        // data race.
        template<typename Key, typename Compare, typename Discipline>
        struct FrozenCursor {

            using Node = Node<Key, Compare, Discipline>;

            Node const* _Nullable _pred;
            size_t _level;

            bool bottom() const {
                return _level == 0;
            }

            FrozenCursor down() const {
                assert(_level);
                return FrozenCursor { _pred, _level - 1 };
            }

            bool end() const {
                return _pred == nullptr;
            }

            FrozenCursor succ() const {
                assert(_pred);
                return { _pred->_next[_level].nonatomic_load(), _level };
            }

            Key const* _Nullable key() const {
                assert(_pred);
                Node const* a = _pred->_next[_level].nonatomic_load();
                return a ? &a->_key : nullptr;
            }

        };


        template<
            typename Loader = LoadAcquire,
            typename Key, typename Compare, typename Discipline,
            typename Query, typename Visit>
        [[nodiscard]]
        std::pair<Node<Key, Compare, Discipline> const* _Nullable, bool>
        locate(Node<Key, Compare, Discipline> const* _Nonnull head, Query const& query, Visit&& visit) {
            size_t i = head->_height.load_relaxed() - 1;
            assert((i + 1) > 0);
            auto slot = head->_next + i;
            for (;;) {
                auto candidate = Loader{}(*slot);
                if (!candidate || Compare{}(query, candidate->_key)) {
                    if (i == 0)
                        return { candidate, false };
                    --i;
                    --slot;
                } else if (Compare{}(candidate->_key, query)) {
                    visit(candidate);
                    slot = candidate->_next + i;
                } else {
                    visit(candidate);
                    return { candidate, true };
                }
            }
        }

    }

    template<typename Key, typename Compare, typename Discipline>
    struct ConcurrentSkiplistSet {

        using iterator = _skiplist_detail::BasicIterator<Key, Compare, Discipline, _skiplist_detail::LoadAcquire>;

        using Node = _skiplist_detail::Node<Key, Compare, Discipline>;
        using Loader = _skiplist_detail::LoadAcquire;

        Node const* _Nonnull _head;

        ConcurrentSkiplistSet()
        : _head(Node::make_head()) {
        }

        [[nodiscard]] iterator begin() const {
            return iterator{ Loader{}(_head->_next[0]) };
        }

        [[nodiscard]] iterator end() const {
            return iterator{nullptr};
        }

        // find(query, visit): visit(Key const&) is called for every node the
        // search ENTERS on its way down (moves right into), the exact match
        // included; nothing is visited on a miss beyond the entered nodes.
        // The rank lookup over the ready set is built on this.
        template<typename Query, typename Visit>
        [[nodiscard]] iterator find(Query const& query, Visit&& visit) const {
            assert(_head);
            auto [candidate, exact] = _skiplist_detail::locate<Loader>(_head, query, [&visit](Node const* node) {
                visit(std::as_const(node->_key));
            });
            return iterator{exact ? candidate : nullptr};
        }

        template<typename Query>
        [[nodiscard]] iterator find(Query const& query) const {
            return find(query, [](Key const&) {});
        }

        template<typename Query>
        [[nodiscard]] iterator lower_bound(Query const& query) const {
            assert(_head);
            auto [candidate, _] = _skiplist_detail::locate<Loader>(_head, query, [](auto&&) {});
            return iterator{candidate};
        }

        // const: the set is a handle to a shared concurrent structure, in the
        // same sense that Atomic's operations are const -- constness of the
        // handle restricts nothing.  The read-only statement is
        // FrozenSkiplistSet, which has no try_emplace.
        template<typename Keylike, typename... Args>
        std::pair<iterator, bool> try_emplace(Keylike&& keylike, Args&&... args) const {
            assert(_head);
            return _head->try_emplace(FORWARD(keylike), FORWARD(args)...);

        }

    }; // ConcurrentSkiplistSet<Key, Compare, Discipline>



    template<typename Key, typename Compare, typename Discipline>
    struct FrozenSkiplistSet {

        using Node = _skiplist_detail::Node<Key, Compare, Discipline>;
        using Loader = _skiplist_detail::LoadNonatomic;
        using iterator = _skiplist_detail::BasicIterator<Key, Compare, Discipline, _skiplist_detail::LoadNonatomic>;

        Node const* _Nonnull _head;

        FrozenSkiplistSet()
        : _head(Node::make_head()) {
        }

        explicit FrozenSkiplistSet(Node const* _Nonnull head)
        : _head(head) {
        }

        // See ConcurrentSkiplistSet::find(query, visit).
        template<typename Query, typename Visit>
        [[nodiscard]] iterator find(Query const& query, Visit&& visit) const {
            assert(_head);
            auto [candidate, exact] = _skiplist_detail::locate<Loader>(_head, query, [&visit](Node const* node) {
                visit(std::as_const(node->_key));
            });
            return iterator{exact ? candidate : nullptr};
        }

        template<typename Query>
        [[nodiscard]] iterator find(Query const& query) const {
            return find(query, [](Key const&) {});
        }

        template<typename Query>
        [[nodiscard]] iterator lower_bound(Query const& query) const {
            assert(_head);
            auto [candidate, _] = _skiplist_detail::locate<Loader>(_head, query, [](auto&&) {});
            return iterator{candidate};
        }

        [[nodiscard]] bool is_empty() const {
            return _head->is_empty();
        }

        [[nodiscard]] iterator begin() const {
            return iterator{ Loader{}(_head->_next[0]) };
        }

        [[nodiscard]] iterator end() const {
            return iterator{nullptr};
        }

        using FrozenCursor = _skiplist_detail::FrozenCursor<Key, Compare, Discipline>;
        FrozenCursor make_cursor() const {
            return FrozenCursor{
                _head,
                _head->_height.nonatomic_load() - 1,
            };
        }

    }; // FrozenSkiplistSet<Key, Compare, Discipline>

    template<typename Key, typename Compare, typename Discipline>
    FrozenSkiplistSet<Key, Compare, Discipline> freeze(ConcurrentSkiplistSet<Key, Compare, Discipline> const& other) {
        return FrozenSkiplistSet{ other._head };
    }




    template<typename Key, typename Compare, typename Discipline>
    [[nodiscard]] std::pair<_skiplist_detail::Node<Key, Compare, Discipline> const* _Nullable, bool>
    _skiplist_detail::Node<Key, Compare, Discipline>
    ::_link_level(size_t i,
                  Node::Slot const* _Nonnull left,
                  Node const* _Nullable expected,
                  Node const* _Nonnull desired) const
    {
        // STYLE: GOTO considered helpful for concurrent code that
        // handles failure by restarting from an earlier step.
        // Nested loops introduce a lot of indentation noise and their
        // multiple conditional breaks and continues require careful
        // reasoning to work out where control flow actually ends up. /rant
    alpha:
        assert(left && desired);
        assert(!expected || Compare{}(desired->_key, expected->_key));
        // desired is thread-private here; the publishing CAS below
        // carries release ordering, so this preceding store can be
        // non-atomic.
        desired->_next[i].nonatomic_store(expected);
        if (left->compare_exchange_strong_release_acquire(expected, desired))
            return { desired, true };
    beta:
        if (!expected || Compare{}(desired->_key, expected->_key))
            goto alpha;
        if (!Compare{}(expected->_key, desired->_key))
            return std::pair(expected, false);
        left = expected->_next + i;
        expected = left->load_acquire();
        goto beta;
    }
    
    template<typename Key, typename Compare, typename Discipline>
    template<typename Keylike, typename... Args>
    std::pair<typename _skiplist_detail::Node<Key, Compare, Discipline> const* _Nullable, bool>
    _skiplist_detail::Node<Key, Compare, Discipline>
    ::_try_emplace(size_t i,
                   Node::Slot const* _Nonnull left,
                   Keylike&& keylike,
                   Args&&... args) const
    {
    alpha:
        Node const* _Nullable candidate = left->load_acquire();
        if (!candidate || Compare{}(keylike, candidate->_key))
            goto beta;
        if (!Compare{}(candidate->_key, keylike))
            return {candidate, false};
        left = candidate->_next + i;
        goto alpha;
    beta:
        assert(!candidate || Compare{}(keylike, candidate->_key));
        if (i == 0) {
            return _link_level(0, left, candidate,
                               Node::with_random_height_emplace(FORWARD(keylike),
                                                              FORWARD(args)...));
            // If _link_level fails, we are relying on the Node we just
            // created being cleaned up eventually by IntrusiveAllocator.
            // Doing nothing is correct for EpochAllocator and
            // GarbageCollected.
        } else {
            auto result = _try_emplace(i - 1, left - 1, FORWARD(keylike), FORWARD(args)...);
            if (result.second && (i < result.first->_height.nonatomic_load())) {
                result = _link_level(i, left, candidate, result.first);
                assert(result.second);
            }
            return result;
        }
    }
    
    
    // `try_emplace` is winner-takes-all on the value; intended for sets of
    // strongly ordered values or for atomic-headed coordination patterns
    // For other concurrent map use, prefer a different primitive that can
    // delegate the decision of what ends up in the structure.
    // `java.util.concurrentConcurrentHashMap.compute` is an example
    template<typename Key, typename Compare, typename Discipline>
    template<typename Keylike, typename... Args>
    std::pair<typename _skiplist_detail::BasicIterator<Key, Compare, Discipline, _skiplist_detail::LoadAcquire>, bool>
    _skiplist_detail::Node<Key, Compare, Discipline>
    ::try_emplace(Keylike&& keylike, Args&&... args) const
    {
        size_t i = _height.load_relaxed();
        assert(i > 0);
        auto result = _try_emplace(i - 1, _next + (i - 1), FORWARD(keylike), FORWARD(args)...);
        if (result.second && result.first->_height.nonatomic_load() > i) {
            _height.fetch_max_relaxed(result.first->_height.nonatomic_load());
            while (i < result.first->_height.nonatomic_load()) {
                auto [discovered, wrote] = _link_level(i, _next + i, nullptr, result.first);
                // _link_level only returns wrote=false when an equivalent key is
                // already in the level-i chain.  By the skiplist invariant, any
                // level-i node is also in level 0; we just successfully inserted
                // at level 0 with a unique key, so no equivalent key exists at
                // any level.  Hence wrote should always be true here.
                assert(wrote);
                ++i;
            }
        }
        return { BasicIterator<Key, Compare, Discipline, _skiplist_detail::LoadAcquire>{ result.first }, result.second };
    }
    

    template<typename Key, typename H, typename Discipline>
    void garbage_collected_scan(ConcurrentSkiplistSet<Key, H, Discipline> const& self) {
        garbage_collected_scan(self._head);
    }


    template<typename P, typename H>
    struct ComparePair : H {

        using key_type = typename P::first_type;
        using mapped_type = typename P::second_type;

        // This helper allows us to overload the function object to
        // provide ordering for combinations of (Key, T) and Key
        //
        // TODO: Unify with KeyService
        static decltype(auto) key_if_pair(auto&& keylike) {
            if constexpr (std::is_same_v<std::decay_t<decltype(keylike)>, P>) {
                return std::forward_like<decltype(keylike)>(keylike.first);
            } else {
                return FORWARD(keylike);
            }
        }

        bool operator()(auto&& a, auto&& b) const {
            return H{}(key_if_pair(FORWARD(a)),
                       key_if_pair(FORWARD(b)));
        }

    };

    template<typename Key, typename Value, typename Compare, typename Discipline>
    using ConcurrentSkiplistMap
    = ConcurrentSkiplistSet<
        std::pair<Key, Value>,
        ComparePair<std::pair<Key, Value>, Compare>,
        Discipline>;

    template<typename Key, typename Value, typename Compare, typename Discipline>
    using FrozenSkiplistMap
    = FrozenSkiplistSet<
        std::pair<Key, Value>,
        ComparePair<std::pair<Key, Value>, Compare>,
        Discipline>;



    // ---- Frozen-cursor frame partition --------------------------------------
    //
    // Given a FrozenCursor `c` covering an AMT frame [frame_lo, frame_lo +
    // n_slots*2^shift) of a *frozen* skiplist, assign to each non-empty child
    // slot a covering cursor (high level where possible), so the child's
    // sub-recursion continues from there rather than re-seeking from the head.
    // `code_of(key) -> uint64_t` projects a skiplist key to its AMT code.
    //
    // Express lanes hop over lower-level nodes, so a level-L `succ()` cannot by
    // itself prove a child empty; we descend exactly where a sub-range has no
    // representative at the current level.  Each child of [lo, hi) is assigned at
    // most once: the recursion splits into [lo, b_kc) (finer level), the child kc
    // holding the first representative, and [b_kc1, hi) (same level).
    // Ranges are carried in __uint128_t so the top frame [0, 2^64) and its child
    // boundaries (e.g. 16 << 60 == 2^64) do not overflow; codes are uint64.
    template<typename Cur, typename CodeOf>
    void skiplist_partition_assign(Cur c, __uint128_t lo, __uint128_t hi,
                                   __uint128_t frame_lo, int shift,
                                   std::optional<Cur>* _Nonnull result, CodeOf code_of) {
        const __uint128_t BEYOND = ((__uint128_t)1 << 64); // > any uint64 code
        auto codeof = [&](const Cur& x) -> __uint128_t {
            auto* k = x.key();
            return k ? (__uint128_t)code_of(*k) : BEYOND;
        };
        if (lo >= hi)
            return;
        while (codeof(c) < lo)
            c = c.succ();
        __uint128_t k = codeof(c);
        if (k >= hi) {
            // No representative at this level in [lo, hi); descend to find any
            // lower-level mods (none if we are already at the bottom).
            if (!c.bottom())
                skiplist_partition_assign(c.down(), lo, hi, frame_lo, shift, result, code_of);
            return;
        }
        int kc = (int)(uint64_t)((k - frame_lo) >> shift);
        __uint128_t b_kc  = frame_lo + ((__uint128_t)kc << shift);
        __uint128_t b_kc1 = b_kc + ((__uint128_t)1 << shift);
        // children strictly before kc may still hold lower-level-only mods
        if (!c.bottom() && b_kc > lo)
            skiplist_partition_assign(c.down(), lo, b_kc, frame_lo, shift, result, code_of);
        // child kc has at least one mod; hand it a covering cursor
        if (!result[kc])
            result[kc] = c;
        // advance past child kc at this level and continue
        Cur cc = c;
        while (codeof(cc) < b_kc1)
            cc = cc.succ();
        skiplist_partition_assign(cc, b_kc1, hi, frame_lo, shift, result, code_of);
    }

    template<typename Cur, typename CodeOf>
    void skiplist_partition_frame(Cur entry, uint64_t frame_lo, int shift, int n_slots,
                                  std::optional<Cur>* _Nonnull result, CodeOf code_of) {
        __uint128_t lo = frame_lo;
        __uint128_t hi = lo + ((__uint128_t)n_slots << shift);
        skiplist_partition_assign(entry, lo, hi, lo, shift, result, code_of);
    }

    template<typename Key, typename Compare, typename Discipline>
    void garbage_collected_scan(FrozenSkiplistSet<Key, Compare, Discipline> const& x) {
        garbage_collected_scan(x._head);
    }

} // namespace wry



#endif /* concurrent_skiplist_hpp */
