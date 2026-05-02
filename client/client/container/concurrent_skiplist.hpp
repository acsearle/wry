//
//  concurrent_skiplist.hpp
//  client
//
//  Created by Antony Searle on 23/11/2024.
//

#ifndef concurrent_skiplist_hpp
#define concurrent_skiplist_hpp


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
    
    // Constinit-ialize to a nonzero value.  Keeps the thread_local as cheap as possible.
    // We don't really care that the threads start out synchronized
    // TODO: On pool thread entry, replace value with something better
    constinit inline thread_local uint64_t _skiplist_prng_state = 0x9E3779B97F4A7C15ULL;
    
    inline uint64_t _skiplist_xorshift64() {
        uint64_t x = _skiplist_prng_state;
        x ^= x << 13;
        x ^= x >>  7;
        x ^= x << 17;
        _skiplist_prng_state = x;
        assert(x != 0);
        return x;
    }
    
    template<typename Key, typename Compare = DefaultKeyService<Key>, typename IntrusiveAllocator = EpochAllocated>
    struct ConcurrentSkiplistSet {

        struct Node;

        // The slot type for in-skiplist edges to Nodes.  We dispatch on
        // IntrusiveAllocator (complete at this point) rather than going
        // through Slot<Node*> (which would invoke slot_for<Node*> and need
        // Node complete — Node is mid-definition where _next[] is
        // declared).  GarbageCollectedSlot's class body must be parsable
        // even with Node forward-only, which is why its static_assert
        // lives in the ctor.
        using NextSlot = std::conditional_t<
            std::is_base_of_v<GarbageCollected, IntrusiveAllocator>,
            GarbageCollectedSlot<Node*>,
            Atomic<Node*>
        >;

        struct Node : IntrusiveAllocator {
            
            // Local placement-new: GarbageCollected's `operator new(size_t)`
            // would otherwise hide the global placement form via class-scope
            // name lookup, breaking `new(raw) Node` inside `with_size_emplace()`.
            static void* _Nonnull  operator new(size_t count, void* _Nonnull ptr) {
                return ptr;
            }
            
            Key _key;
            size_t _size;
            NextSlot _next[] __counted_by(_size);

            explicit Node(size_t n, auto&&... args)
            : IntrusiveAllocator()
            , _key(FORWARD(args)...)
            , _size(n) {
            }

            static Node* _Nonnull with_size_emplace(size_t n, auto&&... args) {
                size_t number_of_bytes = sizeof(Node) + sizeof(NextSlot) * n;
                // Not checked; we accept crashing on OOM.
                void* _Nonnull raw = IntrusiveAllocator::operator new(number_of_bytes,
                                                                      std::align_val_t{alignof(Node)});
                std::memset(raw, 0, number_of_bytes);
                return new(raw) Node(n, FORWARD(args)...);
            }
            
            static Node* _Nonnull with_random_size_emplace(auto&&... args) {
                size_t n = 1 + __builtin_ctzll(_skiplist_xorshift64());
                Node* a = with_size_emplace(n, FORWARD(args)...);
                return a;
            }
            
            // In GC mode (IntrusiveAllocator derives from GarbageCollected),
            // these methods' signatures match the base's pure virtuals and
            // implicitly override them — Node becomes a concrete GC type
            // with a vtable.  In bump mode the base has no matching
            // virtuals, these are just regular member functions, no vtable
            // is added, and the bodies are dead code (the bump-mode ADL
            // overload of garbage_collected_scan(ConcurrentSkiplistSet) is
            // a no-op so they're never called).
            //
            // C++ forbids `requires` on virtual functions, so we can't
            // make the override conditional via constraints; the implicit-
            // override mechanism is what gives us "virtual only when the
            // base has the matching virtual."
            // Scan only _next[0]; the level-0 chain reaches every node, so
            // levels 1.._size-1 are redundant for reachability.  See Head's
            // _garbage_collected_scan for the longer argument.
            void _garbage_collected_scan() const {
                garbage_collected_scan(_key);
                garbage_collected_scan(_next[0].load_acquire());
            }

            void _garbage_collected_debug() const {
                printf("%s\n", __PRETTY_FUNCTION__);
            }

        }; // struct Node
        
        struct iterator {
            
            // we can iterate across a live sequence but obviously that won't
            // be authoritative
            
            Node* _Nullable current;
            
            bool operator==(const iterator&) const = default;
            
            Key& operator*() const {
                assert(current);
                return current->_key;
            }
            
            Key* _Nonnull operator->() const {
                assert(current);
                return &(current->_key);
            }
            
            iterator& operator++() {
                assert(current);
                // acquire to iterate a live sequence, relaxed to iterate a frozen
                // sequence
                current = current->_next[0].load_acquire();
                return *this;
            }
            
            iterator operator++(int) {
                iterator old{current};
                operator++();
                return old;
            }
            
        }; // struct iterator
        
        
        struct Head : IntrusiveAllocator {
            
            static constexpr size_t HEAD_LEVELS = 64;
            
            // Local placement-new: GarbageCollected's `operator new(size_t)`
            // would otherwise hide the global placement form via class-scope
            // name lookup, breaking `new(raw) Head` inside `make()`.
            static void* _Nonnull  operator new(size_t count, void* _Nonnull ptr) {
                return ptr;
            }

            Compare _compare;
            Atomic<size_t> _top;
            NextSlot _next[] __counted_by(HEAD_LEVELS);
            
            explicit Head(Compare comp) : _compare(std::move(comp)), _top(1) {}

            static Head* _Nonnull make(Compare comp) {
                size_t n = HEAD_LEVELS;
                size_t number_of_bytes = sizeof(Head) + sizeof(NextSlot) * n;
                // Not checked; we accept crashing on OOM.
                void* _Nonnull raw = IntrusiveAllocator::operator new(number_of_bytes,
                                                                  std::align_val_t{alignof(Head)});
                std::memset(raw, 0, number_of_bytes);
                return new(raw) Head(std::move(comp));
            }
            
            // Implicit-override pattern, see Node above.
            //
            // We scan only _next[0] (the level-0 chain head), not the full
            // level array.  By the skiplist invariant, every node in the
            // structure is in the level-0 chain; levels 1..HEAD_LEVELS-1 are
            // search-acceleration pointers that don't introduce reachability
            // — every node they reach is also reachable via level 0.  Tracing
            // from _next[0] reaches every node in O(N) hops.  Skipping the
            // upper slots saves both per-scan work (no HEAD_LEVELS-slot loop) and any
            // ordering tangle around _top: we never load _top in the scan.
            //
            // _compare is scanned in case the user's comparator carries GC
            // references; for the common stateless case its scan is a no-op.
            void _garbage_collected_scan() const {
                garbage_collected_scan(_compare);
                garbage_collected_scan(_next[0].load_acquire());
            }

            void _garbage_collected_debug() const {
                printf("%s\n", __PRETTY_FUNCTION__);
            }
            
            // Forward declare substantial methods
            
            template<typename Query> [[nodiscard]] iterator
            find(const Query& query) const;
            
            [[nodiscard]] std::pair<Node* _Nullable, bool>
            _link_level(size_t i, NextSlot* _Nonnull left, Node* _Nullable expected, Node* _Nonnull desired);

            template<typename Keylike, typename... Args>
            std::pair<Node* _Nullable, bool>
            _try_emplace(size_t i, NextSlot* _Nonnull left, Keylike&& keylike, Args&&... args);
            
            template<typename Keylike, typename... Args>
            std::pair<iterator, bool>
            try_emplace(Keylike&& keylike, Args&&... args);

        };
        
        // Cursor for use after a "freeze" point — i.e., once all writers
        // to this skiplist have stopped and happens-before with the
        // current thread has been established (e.g. via an epoch advance).
        // Each step is a plain pointer chase via nonatomic_load; no
        // atomic loads, no fences.  Misuse during the live phase is a
        // data race.
        //
        // _next points into either Head's or a Node's _next[] array — the
        // arrays have the same element type, so the cursor is unaware
        // which kind of object it currently sits in.
        struct FrozenCursor {
            NextSlot const* _Nullable _next;
            size_t _level;

            bool bottom() const {
                return _level == 0;
            }

            FrozenCursor down() const {
                assert(_level);
                return FrozenCursor { _next, _level - 1 };
            }

            bool end() const {
                return _next == nullptr;
            }

            FrozenCursor right() const {
                assert(_next);
                Node const* a = _next[_level].nonatomic_load();
                return FrozenCursor{
                    a ? &a->_next[0] : nullptr,
                    _level,
                };
            }

            Key const* _Nullable key() const {
                assert(_next);
                Node const* a = _next[_level].nonatomic_load();
                return a ? &a->_key : nullptr;
            }

        };

        Head* _Nonnull _head;

        FrozenCursor make_cursor() const {
            return FrozenCursor{
                &_head->_next[0],
                _head->_top.load_relaxed() - 1,
            };
        }

        ConcurrentSkiplistSet()
        : ConcurrentSkiplistSet(Compare{}) {
        }

        explicit ConcurrentSkiplistSet(Compare comp)
        : _head(Head::make(std::move(comp))) {
        }
        
        [[nodiscard]] iterator begin() const {
            return iterator{
                _head->_next[0].load_acquire()
            };
        }
        
        [[nodiscard]] iterator end() const {
            return iterator{nullptr};
        }
        
        template<typename Query>
        [[nodiscard]] iterator find(Query const& query) const {
            assert(_head);
            return _head->find(query);
        }
        
        template<typename Keylike, typename... Args>
        std::pair<iterator, bool> try_emplace(Keylike&& keylike, Args&&... args) {
            assert(_head);
            return _head->try_emplace(FORWARD(keylike), FORWARD(args)...);
            
        }
        
        
    }; // ConcurrentSkiplistSet<Key, Compare, IntrusiveAllocator>

    
    template<typename Key, typename Compare, typename IntrusiveAllocator>
    template<typename Query>
    [[nodiscard]] ConcurrentSkiplistSet<Key, Compare, IntrusiveAllocator>::iterator
    ConcurrentSkiplistSet<Key, Compare, IntrusiveAllocator>::Head
    ::find(Query const& query) const
    {
        size_t i = _top.load_relaxed() - 1;
        assert((i + 1) > 0);
        NextSlot const* _Nonnull left = _next + i;
        for (;;) {
            Node* candidate = left->load_acquire();
            if (!candidate || _compare(query, candidate->_key)) {
                if (i == 0)
                    return iterator{nullptr};
                --i;
                --left;
            } else if (_compare(candidate->_key, query)) {
                left = candidate->_next + i;
            } else {
                return iterator{candidate};
            }
        }
    }
    
    template<typename Key, typename Compare, typename IntrusiveAllocator>
    [[nodiscard]] std::pair<typename ConcurrentSkiplistSet<Key, Compare, IntrusiveAllocator>::Node* _Nullable, bool>
    ConcurrentSkiplistSet<Key, Compare, IntrusiveAllocator>::Head
    ::_link_level(size_t i, NextSlot* _Nonnull left,
                  Node* _Nullable expected, Node* _Nonnull desired)
    {
        // STYLE: GOTO considered helpful for concurrent code that
        // handles failure by restarting from an earlier step.
        // Nested loops introduce a lot of indentation noise and their
        // multiple conditional breaks and continues require careful
        // reasoning to work out where control flow actually ends up. /rant
    alpha:
        assert(left && desired);
        assert(!expected || (std::as_const(_compare)(desired->_key, expected->_key)));
        // desired is thread-private here; the publishing CAS below
        // carries release ordering, so this preceding store can be
        // non-atomic.
        desired->_next[i].nonatomic_store(expected);
        if (left->compare_exchange_strong_release_acquire(expected, desired))
            return { desired, true };
    beta:
        if (!expected || (std::as_const(_compare)(desired->_key, expected->_key)))
            goto alpha;
        if (!(std::as_const(_compare)(expected->_key, desired->_key)))
            return std::pair(expected, false);
        left = expected->_next + i;
        expected = left->load_acquire();
        goto beta;
    }
    
    template<typename Key, typename Compare, typename IntrusiveAllocator>
    template<typename Keylike, typename... Args>
    std::pair<typename ConcurrentSkiplistSet<Key, Compare, IntrusiveAllocator>::Node* _Nullable, bool>
    ConcurrentSkiplistSet<Key, Compare, IntrusiveAllocator>::Head
    ::_try_emplace(size_t i,
                   NextSlot* _Nonnull left,
                   Keylike&& keylike,
                   Args&&... args) {
    alpha:
        Node* _Nullable candidate = left->load_acquire();
        if (!candidate || std::as_const(_compare)(keylike, candidate->_key))
            goto beta;
        if (!(std::as_const(_compare)(candidate->_key, keylike)))
            return {candidate, false};
        left = candidate->_next + i;
        goto alpha;
    beta:
        assert(!candidate || std::as_const(_compare)(keylike, candidate->_key));
        if (i == 0) {
            return _link_level(0, left, candidate,
                               Node::with_random_size_emplace(FORWARD(keylike),
                                                              FORWARD(args)...));
            // If _link_level fails, we are relying on the Node we just
            // created being cleaned up eventually by IntrusiveAllocator.
            // Doing nothing is correct for EpochAllocator and
            // GarbageCollected.
        } else {
            auto result = _try_emplace(i - 1, left - 1, FORWARD(keylike), FORWARD(args)...);
            if (result.second && (i < result.first->_size)) {
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
    template<typename Key, typename Compare, typename IntrusiveAllocator>
    template<typename Keylike, typename... Args>
    std::pair<typename ConcurrentSkiplistSet<Key, Compare, IntrusiveAllocator>::iterator, bool>
    ConcurrentSkiplistSet<Key, Compare, IntrusiveAllocator>::Head
    ::try_emplace(Keylike&& keylike, Args&&... args) {
        size_t i = _top.load_relaxed();
        assert(i > 0);
        auto result = _try_emplace(i - 1, _next + (i - 1), FORWARD(keylike), FORWARD(args)...);
        if (result.second && result.first->_size > i) {
            _top.fetch_max_relaxed(result.first->_size);
            while (i < result.first->_size) {
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
        return { iterator{ result.first }, result.second };
    }
    

    // ADL hook for GC parents that hold a ConcurrentSkiplistSet member.
    //
    // GC mode: recurse into _head; the spine is then traced via the
    // virtual overrides on Head and each Node.
    template<typename Key, typename H, typename Allocator>
    requires (std::is_base_of_v<GarbageCollected, Allocator>)
    void garbage_collected_scan(ConcurrentSkiplistSet<Key, H, Allocator> const& self) {
        garbage_collected_scan(self._head);
    }

    // Bump mode: no-op.  GC payloads inside epoch-allocated nodes must
    // already be reachable to the collector via some independent path
    // (otherwise they'd already have been collected), so there's no need
    // to trace through the bump skiplist.
    template<typename Key, typename H, typename Allocator>
    requires (std::is_base_of_v<BumpAllocated, Allocator>)
    void garbage_collected_scan(ConcurrentSkiplistSet<Key, H, Allocator> const&) {
        // no-op
    }


    // ConcurrentSkiplistMap by wrapping a ConcurrentSkiplistSet.
    //
    // In the absence of a compelling use case, no way is provided to mutate an
    // existing element yet.  In general, it would require T to be atomic.
    
    
    template<typename Key, typename T, typename H = DefaultKeyService<Key>>
    struct ConcurrentSkiplistMap {
        
        using P = std::pair<Key, T>;
        
        struct ComparePair : H {
            
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
        
        using S = ConcurrentSkiplistSet<P, ComparePair>;
        using iterator = S::iterator;
        
        S _set;
        
        [[nodiscard]] iterator begin() const {
            return _set.begin();
        }
        
        [[nodiscard]] iterator end() const {
            return _set.end();
        }
        
        [[nodiscard]] iterator find(auto&& keylike) const {
            return _set.find(FORWARD(keylike));
        }
        
        std::pair<iterator, bool> try_emplace(auto&& keylike, auto&&... args) {
            return _set.try_emplace(FORWARD(keylike), FORWARD(args)...);
        }
        
        using Cursor = S::FrozenCursor;
        Cursor make_cursor() const {
            return _set.make_cursor();
        }
        
    }; // struct concurrent_skiplist_map
    
    
} // namespace wry



#endif /* concurrent_skiplist_hpp */
