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
    
    // Concurrent skiplist without erasure
        
    
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
        return x;
    }
    
    template<typename Key, typename H = DefaultKeyService<Key>, typename IntrusiveAllocator = EpochAllocated>
    struct ConcurrentSkiplistSet {

        struct Node;

        // The slot type for in-skiplist edges to Nodes.  We dispatch on
        // IntrusiveAllocator (complete at this point) rather than going
        // through NextSlot (which would invoke slot_for<Node*> and need
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
            
            // size member is not essential to the major operations
            // - needed for memory debug
            // - needed for GC scanning
            //   - which is itself not needed for ephemeral operation
            
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
                void* _Nonnull raw = IntrusiveAllocator::operator new(number_of_bytes,
                                                                      std::align_val_t{alignof(Node)});
                std::memset(raw, 0, number_of_bytes);
                return new(raw) Node(n, FORWARD(args)...);
            }
            
            static Node* _Nonnull with_random_size_emplace(auto&&... args) {
                size_t n = 1 + __builtin_ctzll(_skiplist_xorshift64());
                Node* a = with_size_emplace(n, std::forward<decltype(args)>(args)...);
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
            void _garbage_collected_scan() const {
                garbage_collected_scan(_key);
                for (size_t i = 0; i != _size; ++i) {
                    garbage_collected_scan(_next[i].load_acquire());
                }
            }

            void _garbage_collected_debug() const {
                printf("%s\n", __PRETTY_FUNCTION__);
            }

        }; // struct Node
        
        
        struct Head : IntrusiveAllocator {
            
            Atomic<size_t> _top;
            NextSlot _next[];

            static void* _Nonnull  operator new(size_t count, void* _Nonnull ptr) {
                return ptr;
            }

            Head() : _top(1) {}

            static Head* _Nonnull make() {
                size_t n = 33;
                size_t number_of_bytes = sizeof(Head) + sizeof(NextSlot) * n;
                void* _Nonnull raw = IntrusiveAllocator::operator new(number_of_bytes,
                                                                  std::align_val_t{alignof(Head)});
                std::memset(raw, 0, number_of_bytes);
                return new(raw) Head;
            }
            
            // Implicit-override pattern, see Node above.
            void _garbage_collected_scan() const {
                for (size_t i = 0; i != 33; ++i) {
                    garbage_collected_scan(_next[i].load_acquire());
                }
            }

            void _garbage_collected_debug() const {
                printf("%s\n", __PRETTY_FUNCTION__);
            }

        };
        
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
            
            operator bool() const {
                return current;
            }
            
        }; // struct iterator

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
        : _head(Head::make()) {
        }
        
        [[nodiscard]] iterator begin() const {
            return iterator{
                _head->_next[0].load_acquire()
            };
        }
        
        [[nodiscard]] iterator end() const {
            return iterator{nullptr};
        }
        
        template<typename Query> [[nodiscard]] auto
        find(const Query& query) const -> iterator
        {
            size_t i = _head->_top.load_relaxed() - 1;
            NextSlot const* _Nonnull left = _head->_next + i;
            for (;;) {
                Node* candidate = left->load_acquire();
                if (!candidate || H{}.compare(query, candidate->_key)) {
                    if (i == 0)
                        return iterator{nullptr};
                    --i;
                    --left;
                } else if (H{}.compare(candidate->_key, query)) {
                    left = candidate->_next + i;
                } else {
                    return iterator{candidate};
                }
            }
        }
        
        [[nodiscard]] static auto
        _link_level(size_t i, NextSlot* _Nonnull left,
                    Node* _Nullable expected, Node* _Nonnull desired)
        -> std::pair<Node* _Nullable, bool>
        {
        alpha:
            assert(left && desired);
            assert(!expected || (H{}.compare(desired->_key, expected->_key)));
            // desired is thread-private here; the publishing CAS below
            // carries release ordering, so this preceding store can be
            // non-atomic.
            desired->_next[i].nonatomic_store(expected);
            if (left->compare_exchange_strong_release_acquire(expected, desired))
                return { desired, true };
        beta:
            if (!expected || (H{}.compare(desired->_key, expected->_key)))
                goto alpha;
            if (!(H{}.compare(expected->_key, desired->_key)))
                return std::pair(expected, false);
            left = expected->_next + i;
            expected = left->load_acquire();
            goto beta;
        }
        
        static std::pair<Node* _Nullable, bool> _try_emplace(size_t i,
                                                             NextSlot* _Nonnull left,
                                                             auto&& keylike,
                                                             auto&&... args) {
        alpha:
            Node* _Nullable candidate = left->load_acquire();
            if (!candidate || H{}.compare(keylike, candidate->_key))
                goto beta;
            if (!(H{}.compare(candidate->_key, keylike)))
                return {candidate, false};
            left = candidate->_next + i;
            goto alpha;
        beta:
            assert(!candidate || H{}.compare(keylike, candidate->_key));
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
        
        std::pair<iterator, bool> try_emplace(auto&& keylike, auto&&... args) {
            assert(_head);
            size_t i = _head->_top.load_relaxed();
            assert(i > 0);
            auto result = _try_emplace(i - 1, _head->_next + (i - 1), FORWARD(keylike), FORWARD(args)...);
            if (result.second && result.first->_size > i) {
                _head->_top.fetch_max_relaxed(result.first->_size);
                while (i < result.first->_size) {
                    auto [discovered, wrote] = _link_level(i, _head->_next + i, nullptr, result.first);
                    assert(wrote);
                    // TODO: Handle the failure of _link_level.  What does it
                    // mean?
                    ++i;
                }
            }
            return { iterator{ result.first }, result.second };
        }
        
        // TODO: emplace keeps an existing value.  What do we want for
        // pairs?
        
    }; // concurrent_skiplist<Key, Compare>


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


    template<typename Key, typename T, typename H = DefaultKeyService<Key>>
    struct ConcurrentSkiplistMap {
        
        using P = std::pair<Key, T>;
        
        struct ComparePair : H {
            
            static decltype(auto) key_if_pair(auto&& keylike) {
                if constexpr (std::is_same_v<std::decay_t<decltype(keylike)>, P>) {
                    return std::forward_like<decltype(keylike)>(keylike.first);
                } else {
                    return FORWARD(keylike);
                }
            }
            
            bool compare(auto&& a, auto&& b) const {
                return H{}.compare(key_if_pair(FORWARD(a)),
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

        
        /*
         const T& operator[](auto&& keylike) const {
         return _set.try_emplace(FORWARD(keylike), T{}).first->second;
         }
         
         T& operator[](auto&& keylike) {
         return _set.try_emplace(FORWARD(keylike), T{}).first->second;
         }
         */
        
    }; // struct concurrent_skiplist_map
    
    
} // namespace wry



#endif /* concurrent_skiplist_hpp */
