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

#include "atomic.hpp"
#include "garbage_collected.hpp"
#include "utility.hpp"

namespace wry {
        
    namespace concurrent_skiplist {
        
        inline constinit thread_local std::ranlux24* thread_local_random_number_generator = nullptr;
        
        template<typename Key, typename Compare = std::less<Key>>
        struct ConcurrentSkiplistSet {
            
            struct Node : GarbageCollected {
                
                // TODO: do we need the size member?
                
                Key _key;
                size_t _size;
                mutable Atomic<Node*> _next[0];
                
                static void* operator new(size_t count, void* ptr) {
                    return ptr;
                }
                
                explicit Node(size_t n, auto&&... args)
                : _key(FORWARD(args)...)
                , _size(n) {
                }
                
                static Node* with_size_emplace(size_t n, auto&&... args) {
                    void* raw = GarbageCollected::operator new(sizeof(Node) + sizeof(Atomic<Node*>) * n);
                    return new(raw) Node(n, FORWARD(args)...);
                }
                
                // TODO: restrict generator to a maximum of 1 + current size?
                //
                // TODO: tune shape of distribution
                // Quick experiment indicates a n^{-0.5} is better than n^{-0.25}
                static Node* with_random_size_emplace(auto&&... args) {
                    // TODO: initialize this on thread
                    if (!thread_local_random_number_generator) {
                        thread_local_random_number_generator = new std::ranlux24;
                    }
                    size_t n = 1 + __builtin_ctz((*thread_local_random_number_generator)());
                    Node* a = with_size_emplace(n, std::forward<decltype(args)>(args)...);
                    return a;
                }
                
                // SAFETY: the skiplist is an ephemeral object and may not
                // live across handshakes.  It does not own its fields.
                //
                // TODO: Consider arena allocation rather than involving the
                // garbage collector
                virtual void _garbage_collected_enumerate_fields(TraceContext* context) const override {
                }

            }; // struct Node
            
            
            struct Head : GarbageCollected {
                
                mutable Atomic<size_t> _top;
                mutable Atomic<Node*> _next[0];
                
                static void* operator new(size_t count, void* ptr) {
                    return ptr;
                }
                
                Head() : _top(1) {}
                
                static Head* make() {
                    size_t n = 33;
                    void* raw =  GarbageCollected::operator new(sizeof(Head) + sizeof(Atomic<Node*>) * n);
                    return new(raw) Head;
                }
                
                virtual void _garbage_collected_enumerate_fields(TraceContext* context) const override {
                    
                }

            };
            
            struct iterator {
                
                // we can iterate across a live sequence but obviously that won't
                // be authoritative
                
                Node* current;
                
                bool operator==(const iterator&) const = default;
                
                Key& operator*() const {
                    assert(current);
                    return current->_key;
                }
                
                Key* operator->() const {
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
            
            const Head* _head;
            
            ConcurrentSkiplistSet()
            : _head(Head::make()) {
            }
            
            iterator begin() const {
                return iterator{
                    _head->_next[0].load(Ordering::ACQUIRE)
                };
            }
            
            iterator end() const {
                return iterator{nullptr};
            }
            
            template<typename Query>
            iterator find(const Query& query) const {
                size_t i = _head->_top.load(Ordering::RELAXED) - 1;
                Atomic<Node*>* left = _head->_next + i;
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
            
            static std::pair<Node*, bool> _link_level(size_t i, Atomic<Node*>* left, Node* expected, Node* desired) {
            alpha:
                assert(left && desired);
                assert(!expected || (Compare()(desired->_key, expected->_key)));
                desired->_next[i].store(expected, Ordering::RELEASE);
                if (left->compare_exchange_strong(expected,
                                                  desired,
                                                  Ordering::RELEASE,
                                                  Ordering::ACQUIRE))
                    return {desired, true};
            beta:
                if (!expected || (Compare()(desired->_key, expected->_key)))
                    goto alpha;
                if (!(Compare()(expected->_key, desired->_key)))
                    return std::pair(expected, false);
                left = expected->_next + i;
                expected = left->load(Ordering::ACQUIRE);
                goto beta;
            }
            
            static std::pair<Node*, bool> _try_emplace(size_t i, Atomic<Node*>* left, auto&& keylike, auto&&... args) {
            alpha:
                Node* candidate = left->load(Ordering::ACQUIRE);
                if (!candidate || Compare()(keylike, candidate->_key))
                    goto beta;
                if (!(Compare()(candidate->_key, keylike)))
                    return std::pair(candidate, false);
                left = candidate->_next + i;
                goto alpha;
            beta:
                assert(!candidate || Compare()(keylike, candidate->_key));
                if (i == 0) {
                    Node* p = Node::with_random_size_emplace(FORWARD(keylike), FORWARD(args)...);
                    auto result = _link_level(0, left, candidate, p);
                    if (!result.second)
                        free(p);
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
                        _link_level(i, _head->_next + i, nullptr, result.first);
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
                        return forward_like<decltype(keylike)>(keylike.first);
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
            
            iterator begin() const {
                return _set.begin();
            }
            
            iterator end() const {
                return _set.end();
            }
            
            iterator find(auto&& keylike) const {
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
        
    } // namespace concurrent_skiplist
    
} // namespace wry



#endif /* concurrent_skiplist_hpp */
