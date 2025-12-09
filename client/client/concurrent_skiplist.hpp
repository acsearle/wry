//
//  concurrent_skiplist.hpp
//  client
//
//  Created by Antony Searle on 23/11/2024.
//

#ifndef concurrent_skiplist_hpp
#define concurrent_skiplist_hpp


#include <cassert>

#include <random>

#include "garbage_collected.hpp"
#include "epoch_allocator.hpp"
#include "utility.hpp"

namespace wry {
    
    // Concurrent skiplist without erasure
        
    inline constinit thread_local std::ranlux24* _Nullable thread_local_random_number_generator = nullptr;
    
    template<typename Key, typename Compare = std::less<Key>, typename IntrusiveAllocator = EpochAllocated>
    struct ConcurrentSkiplistSet {
        
        struct Node;
        
        struct FrozenSizeAtomicSkips {
            size_t _size;
            Atomic<Node* _Nullable> _next[0] __counted_by(_size);
        };
        
        struct Node : IntrusiveAllocator {
            
            // size member is not essential to the major operations
            // - needed for memory debug
            // - needed for GC scanning
            
            Key _key;
            size_t _size;
            Atomic<Node* _Nullable> _next[0] __counted_by(_size);
            
            static void* _Nonnull operator new(size_t count, void* _Nonnull ptr) {
                return ptr;
            }
            
            explicit Node(size_t n, auto&&... args)
            : IntrusiveAllocator()
            , _key(FORWARD(args)...)
            , _size(n) {
            }
                        
            static Node* _Nonnull with_size_emplace(size_t n, auto&&... args) {
                size_t number_of_bytes = sizeof(Node) + sizeof(Atomic<Node*>) * n;
                void* _Nonnull raw = IntrusiveAllocator::operator new(number_of_bytes,
                                                                      std::align_val_t{alignof(Node)});
                std::memset(raw, 0, number_of_bytes);
                return new(raw) Node(n, FORWARD(args)...);
            }
            
            // TODO: restrict generator to a maximum of 1 + current size?
            //
            // TODO: tune shape of distribution
            // Quick experiment indicates a n^{-0.5} is better than n^{-0.25}
            static Node* _Nonnull with_random_size_emplace(auto&&... args) {
                // TODO: initialize this on thread
                if (!thread_local_random_number_generator) {
                    thread_local_random_number_generator = new std::ranlux24;
                }
                size_t n = 1 + __builtin_ctz((*thread_local_random_number_generator)());
                Node* a = with_size_emplace(n, std::forward<decltype(args)>(args)...);
                return a;
            }
            
            // This is called only when the allocator is GC
            virtual void _garbage_collected_scan() const /* override */ {
                garbage_collected_scan(_key);
                for (size_t i = 0; i != _size; ++i) {
                    garbage_collected_scan(_next[i].load(Ordering::ACQUIRE));
                }
            }
            
            
        }; // struct Node
        
        
        struct Head : IntrusiveAllocator {
            
            Atomic<size_t> _top;
            Atomic<Node* _Nullable> _next[0];
            
            static void* _Nonnull  operator new(size_t count, void* _Nonnull ptr) {
                return ptr;
            }
            
            Head() : _top(1) {}
            
            static Head* _Nonnull make() {
                size_t n = 33;
                size_t number_of_bytes = sizeof(Head) + sizeof(Atomic<Node*>) * n;
                void* _Nonnull raw = IntrusiveAllocator::operator new(number_of_bytes,
                                                                  std::align_val_t{alignof(Head)});
                std::memset(raw, 0, number_of_bytes);
                return new(raw) Head;
            }
            
            // This is called only when the allocator is GC
            virtual void _garbage_collected_scan() const {
                for (size_t i = 0; i != 33; ++i) {
                    garbage_collected_scan(_next[i].load(Ordering::ACQUIRE));
                }
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
                current = current->_next[0].load(Ordering::ACQUIRE);
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

        struct FrozenNexts {
            size_t _size;
            Node const* _Nullable _next[0];
        };

        struct FrozenCursor {
            FrozenNexts const* _Nullable _pointer;
            size_t _level;
            
            bool bottom() const {
                return _level == 0;
            }
            
            FrozenCursor down() const {
                assert(_level);
                return FrozenCursor {
                    _pointer,
                    _level - 1,
                };
            }
            
            bool end() const {
                return _pointer == nullptr;
            }
            
            FrozenCursor right() const {
                assert(_pointer);
                Node const* a = _pointer->_next[_level];
                size_t const* b = a ? &a->_size : nullptr;
                assert(!b || *b >= _level);
                return FrozenCursor{
                    (FrozenNexts const*)b,
                    _level,
                };
            }
            
            Key const* _Nullable key() const {
                Node const* a = _pointer->_next[_level];
                return a ? &(a->_key) : nullptr;
            }
            
        };
                
        Head* _Nonnull _head;
        
        FrozenCursor make_cursor() const {
            return FrozenCursor{
                (FrozenNexts const*)(&_head->_top),
                _head->_top.load(Ordering::RELAXED) - 1,
            };
        }

        
        ConcurrentSkiplistSet()
        : _head(Head::make()) {
        }
        
        [[nodiscard]] iterator begin() const {
            return iterator{
                _head->_next[0].load(Ordering::ACQUIRE)
            };
        }
        
        [[nodiscard]] iterator end() const {
            return iterator{nullptr};
        }
        
        template<typename Query> [[nodiscard]] auto
        find(const Query& query) const -> iterator
        {
            size_t i = _head->_top.load(Ordering::RELAXED) - 1;
            Atomic<Node*> const* _Nonnull left = _head->_next + i;
            for (;;) {
                Node* candidate = left->load(Ordering::ACQUIRE);
                if (!candidate || Compare()(query, candidate->_key)) {
                    if (i == 0)
                        return iterator{nullptr};
                    --i;
                    --left;
                } else if (Compare()(candidate->_key, query)) {
                    left = candidate->_next + i;
                } else {
                    return iterator{candidate};
                }
            }
        }
        
        [[nodiscard]] static auto
        _link_level(size_t i, Atomic<Node*>* _Nonnull left,
                    Node* _Nullable expected, Node* _Nonnull desired)
        -> std::pair<Node* _Nullable, bool>
        {
        alpha:
            // TODO: this is the only place we need to implement a write barrier
            assert(left && desired);
            assert(!expected || (Compare()(desired->_key, expected->_key)));
            desired->_next[i].store(expected, Ordering::RELEASE);
            if (left->compare_exchange_strong(expected,
                                              desired,
                                              Ordering::RELEASE,
                                              Ordering::ACQUIRE))
                return { desired, true };
        beta:
            if (!expected || (Compare()(desired->_key, expected->_key)))
                goto alpha;
            if (!(Compare()(expected->_key, desired->_key)))
                return std::pair(expected, false);
            left = expected->_next + i;
            expected = left->load(Ordering::ACQUIRE);
            goto beta;
        }
        
        static std::pair<Node* _Nullable, bool> _try_emplace(size_t i,
                                                             Atomic<Node*>* _Nonnull left,
                                                             auto&& keylike,
                                                             auto&&... args) {
        alpha:
            Node* _Nullable candidate = left->load(Ordering::ACQUIRE);
            if (!candidate || Compare()(keylike, candidate->_key))
                goto beta;
            if (!(Compare()(candidate->_key, keylike)))
                return {candidate, false};
            left = candidate->_next + i;
            goto alpha;
        beta:
            assert(!candidate || Compare()(keylike, candidate->_key));
            if (i == 0) {
                Node* _Nonnull p = Node::with_random_size_emplace(FORWARD(keylike), FORWARD(args)...);
                auto result = _link_level(0, left, candidate, p);
                if (!result.second)
                    delete p; // <-- Uses custom operator delete
                return result;
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
            size_t i = _head->_top.load(Ordering::RELAXED);
            assert(i > 0);
            auto result = _try_emplace(i - 1, _head->_next + (i - 1), FORWARD(keylike), FORWARD(args)...);
            if (result.second && result.first->_size > i) {
                _head->_top.fetch_max(result.first->_size, Ordering::RELAXED);
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
    
    
    template<typename Key, typename T, typename Compare = std::less<Key>>
    struct ConcurrentSkiplistMap {
        
        using P = std::pair<Key, T>;
        
        struct ComparePair {
            
            static decltype(auto) key_if_pair(auto&& keylike) {
                if constexpr (std::is_same_v<std::decay_t<decltype(keylike)>, P>) {
                    return std::forward_like<decltype(keylike)>(keylike.first);
                } else {
                    return FORWARD(keylike);
                }
            }
            
            bool operator()(auto&& a, auto&& b) const {
                return Compare()(key_if_pair(FORWARD(a)),
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
