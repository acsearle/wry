//
//  ConcurrentSkiplist.hpp
//  client
//
//  Created by Antony Searle on 23/11/2024.
//

#ifndef ConcurrentSkiplist_hpp
#define ConcurrentSkiplist_hpp


#include <cassert>

#include <random>

#include "atomic.hpp"
#include "object.hpp"

namespace wry {
    
    namespace _concurrent_skiplist {
        
        inline thread_local std::ranlux24* thread_local_random_number_generator = nullptr;
        
        template<typename Key, typename Compare = std::less<>>
        struct concurrent_skiplist {
            
            struct _node_t {
                Key _key;
                size_t _size;
                mutable Atomic<_node_t*> _next[0];
                
                explicit _node_t(size_t n, auto&&... args)
                : _key(std::forward<decltype(args)>(args)...)
                , _size(n) {
                }
                
                static _node_t* with_size_emplace(size_t n, auto&&... args) {
                    void* raw = calloc(sizeof(_node_t) + sizeof(std::atomic<_node_t*>) * n, 1);
                    return new(raw) _node_t(n, std::forward<decltype(args)>(args)...);
                }
                
                static _node_t* with_random_size_emplace(auto&&... args) {
                    size_t n = 1 + __builtin_ctz( (*thread_local_random_number_generator)() );
                    _node_t* a = with_size_emplace(n, std::forward<decltype(args)>(args)...);
                    return a;
                }
                
            };
            
            
            struct _head_t : gc::Object {
                
                mutable Atomic<size_t> _top;
                mutable Atomic<_node_t*> _next[0];
                
                _head_t() : _top(1) {}
                
                static _head_t* make() {
                    size_t n = 33;
                    void* raw = calloc(sizeof(_head_t) + sizeof(std::atomic<_node_t*>) * n, 1);
                    return new(raw) _head_t;
                    
                }
                
            };
            
            struct iterator {
                
                // we can iterate across a live sequence but obviously that won't
                // be authoritative
                
                const _node_t* current;
                
                bool operator==(const iterator&) const = default;
                
                const Key& operator*() const {
                    assert(current);
                    return current->_key;
                }
                
                iterator& operator++() {
                    assert(current);
                    // acquire to iterate a live sequence, relaxed to iterate a frozen
                    // sequence
                    current = current->_next[0].load(std::memory_order_acquire);
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
            
            const _head_t* _head;
            
            
            concurrent_skiplist()
            : _head(_head_t::make()) {
            }
            
            iterator begin() const {
                return iterator{_head->_next[0].load(std::memory_order_acquire)};
            }
            
            iterator end() const {
                return iterator{nullptr};
            }
            
            template<typename Query>
            iterator find(const Query& query) {
                std::size_t i = _head->_top.load(std::memory_order_relaxed) - 1;
                std::atomic<_node_t*>* left = _head->_next + i;
                for (;;) {
                    const _node_t* candidate = left->load(std::memory_order_acquire);
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
            
            static std::pair<_node_t*, bool> _link_level(std::size_t i, std::atomic<_node_t*>* left, _node_t* expected, _node_t* desired) {
            alpha:
                assert(left && desired);
                assert(!expected || (desired->_key < expected->_key));
                desired->_next[i].store(expected, std::memory_order_release);
                if (left->compare_exchange_strong(expected,
                                                  desired,
                                                  std::memory_order_release,
                                                  std::memory_order_acquire))
                    return {desired, true};
            beta:
                if (!expected || (Compare()(desired->_key, expected->_key)))
                    goto alpha;
                if (!(Compare()(expected->_key, desired->_key)))
                    return std::pair(expected, false);
                left = expected->_next + i;
                expected = left->load(std::memory_order_acquire);
                goto beta;
            }
            
            static std::pair<_node_t*, bool> _emplace(size_t i, std::atomic<_node_t*>* left, const auto& query, auto&&... args) {
            alpha:
                _node_t* candidate = left->load(std::memory_order_acquire);
                if (!candidate || Compare()(query, candidate->_key))
                    goto beta;
                if (!(Compare()(candidate->_key, query)))
                    return std::pair(candidate, false);
                left = candidate->_next + i;
                goto alpha;
            beta:
                assert(!candidate || Compare()(query, candidate->_key));
                if (i == 0) {
                    _node_t* p = _node_t::with_random_size_emplace(query, std::forward<decltype(args)>(args)...);
                    auto result = _link_level(0, left, candidate, p);
                    if (!result.second)
                        free(p);
                    return result;
                } else {
                    auto result = _emplace(i - 1, left - 1, query, std::forward<decltype(args)>(args)...);
                    if (result.second && (i < result.first->_size)) {
                        result = _link_level(i, left, candidate, result.first);
                        assert(result.second);
                    }
                    return result;
                }
            }
            
            std::pair<iterator, bool> emplace(const auto& query, auto&&... args) {
                assert(_head);
                size_t i = _head->_top.load(std::memory_order_relaxed);
                assert(i > 0);
                auto result = _emplace(i - 1, _head->_next + (i - 1), query, std::forward<decltype(args)>(args)...);
                if (result.second && result.first->_size > i) {
                    __atomic_fetch_max((size_t*)&(_head->_top), result.first->_size, __ATOMIC_RELAXED);
                    while (i < result.first->_size) {
                        _link_level(i, _head->_next + i, nullptr, result.first);
                        ++i;
                    }
                }
                return {iterator{result.first}, result.second};
            }
            
        }; // concurrent_skiplist<Key, Compare>
        
        
        template<typename Key, typename T, typename Compare = std::less<>>
        struct concurrent_skiplist_map {
            
            using P = std::pair<Key, T>;
            
            struct ComparePair {
                
                static decltype(auto) key_if_pair(auto&& query) {
                    if constexpr (std::is_same_v<std::decay_t<decltype(query)>, P>) {
                        return forward_like<decltype(query)>(query.first);
                    } else {
                        return std::forward<decltype(query)>(query);
                    }
                }
                
                bool operator()(auto&& a, auto&& b) const {
                    return Compare()(key_if_pair(std::forward<decltype(a)>(a)),
                                     key_if_pair(std::forward<decltype(b)>(b)));
                }
                
            };
            
            using S = concurrent_skiplist<P, ComparePair>;
            using iterator = S::iterator;
            
            S _set;
            
            iterator begin() const {
                return _set.begin();
            }
            
            iterator end() const {
                return _set.end();
            }
            
            iterator find(auto&& query) const {
                return _set.find(std::forward<decltype(query)>(query));
            }
            
            std::pair<iterator, bool> emplace(auto&&... args) {
                return _set.emplace(std::forward<decltype(args)>(args)...);
            }
            
            const T& operator[](auto&& query) const {
                return _set.emplace(std::forward<decltype(query)>(query)).first->second;
            }
            
            T& operator[](auto&& query) {
                return _set.emplace(std::forward<decltype(query)>(query)).first->second;
            }
            
        }; // struct concurrent_skiplist_map
        
    } // namespace _concurrent_skiplist
    
} // namespace wry



#endif /* ConcurrentSkiplist_hpp */
