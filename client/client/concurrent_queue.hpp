//
//  concurrent_queue.hpp
//  client
//
//  Created by Antony Searle on 22/10/2025.
//

#ifndef concurrent_queue_hpp
#define concurrent_queue_hpp

#include <condition_variable>
#include <mutex>
#include <deque>

#include "garbage_collected.hpp"
#include "utility.hpp"

namespace wry {
    
    template<typename T>
    struct BlockingDeque {
        
        mutable std::mutex _mutex;
        std::condition_variable _condition_variable;
        std::deque<T> _deque;
        ptrdiff_t _waiting = 0;
        bool _is_canceled = false;
        
        void push_front(T item) {
            bool notify = false;
            {
                std::unique_lock lock{_mutex};
                _deque.push_front(item);
                if (_waiting) {
                    notify = true;
                    --_waiting;
                }
            }
            if (notify)
                _condition_variable.notify_one();
        }
        
        void push_back(T item) {
            bool notify = false;
            {
                std::unique_lock lock{_mutex};
                _deque.push_back(item);
                if (_waiting) {
                    notify = true;
                    --_waiting;
                }
            }
            if (notify)
                _condition_variable.notify_one();
        }
        
        [[nodiscard]] bool try_pop_front(T& item) {
            std::unique_lock lock{_mutex};
            bool result = !_deque.empty();
            if (result) {
                item = _deque.front();
                _deque.pop_front();
            }
            return result;
        }

        [[nodiscard]] bool try_pop_back(T& item) {
            std::unique_lock lock{_mutex};
            bool result = !_deque.empty();
            if (result) {
                item = _deque.back();
                _deque.pop_back();
            }
            return result;
        }

        // spurious wakes are permitted
        void wait_not_empty() {
            std::unique_lock lock{_mutex};
            if (_deque.empty() && !_is_canceled) {
                ++_waiting;
                _condition_variable.wait(lock);
            }
        }
        
        void cancel() {
            {
                std::unique_lock lock{_mutex};
                _is_canceled = true;
                _waiting = 0;
            }
            _condition_variable.notify_all();
        }
        
        [[nodiscard]] bool is_canceled() {
            std::unique_lock lock{_mutex};
            return _is_canceled;
        }
        
    };
    
    template<typename T>
    void garbage_collected_scan(BlockingDeque<T> const& x) {
        std::unique_lock guard{x._mutex};
        for (auto const& y : x._deque)
            garbage_collected_scan(y);
    }
        
    template<typename T>
    void garbage_collected_shade(BlockingDeque<T> const& a) {
        std::unique_lock lock{a._mutex};
        for (T const& b : a._deque)
            garbage_collected_shade(b);
    }
    
    
    
    // Michael-Scott queue
    
    template<typename T>
    struct ObstructionFreeQueue {
        
        struct Node : GarbageCollected {
            
            Atomic<Node*> _next{nullptr};
            
            // The garbage collector scans the payload concurrently with pop,
            // so it has to be constant
            T const _payload{};
            
            explicit Node(auto&&... args)
            : _next{nullptr}
            , _payload{FORWARD(args)...} {
            }
            
            virtual void _garbage_collected_scan() const override {
                garbage_collected_scan(_next.load(Ordering::ACQUIRE));
                garbage_collected_scan(_payload);
            }
            
        };
        
        alignas(64) Atomic<Node*> _head;
        alignas(64) Atomic<Node*> _tail;
        
        explicit ObstructionFreeQueue(Node* sentinel)
        : _head(sentinel)
        , _tail(sentinel) {
        }
        
        void push(T item) {
            Node* a = new Node{std::move(item)};
            Node* b = _tail.load(Ordering::ACQUIRE);
            for (;;) {
                assert(b);
                Node* c = b->_next.load(Ordering::ACQUIRE);
                if (!c) {
                    // _tail is up to date, race to install the next node
                    if (b->_next.compare_exchange_strong(c, a, Ordering::RELEASE, Ordering::ACQUIRE)) {
                        // race to advance the tail
                        if (_tail.compare_exchange_weak(b, c, Ordering::RELEASE, Ordering::RELAXED)) {
                            garbage_collected_shade(b);
                        }
                        return;
                    }
                } else {
                    // race to advance the tail
                    if (_tail.compare_exchange_strong(b, c, Ordering::RELEASE, Ordering::ACQUIRE)) {
                        garbage_collected_shade(b);
                        b = c;
                    }
                }
            }
        }
        
        bool try_pop(T& item) {
            Node* a = _head.load(Ordering::ACQUIRE);
            for (;;) {
                assert(a);
                Node* b = a->_next.load(Ordering::ACQUIRE);
                if (!b)
                    return false;
                // race to advance head
                if (_head.compare_exchange_strong(a, b, Ordering::RELEASE, Ordering::ACQUIRE)) {
                    garbage_collected_shade(a);
                    // We did advance head and thus claim the payload of the new
                    // head node.
                    item = b->_payload;
                    return true;
                }
            }
        }
                
        bool is_empty() const {
            Node* a = _head.load(Ordering::ACQUIRE);
            assert(a);
            Node* b = a->_next.load(Ordering::RELAXED);
            return !b;
        };
                
    };
    
    template<typename T>
    void garbage_collected_scan(ObstructionFreeQueue<T> const& x) {
        // _head can briefly overtake _tail so we need to make both strong
        garbage_collected_scan(x._head.load(Ordering::ACQUIRE));
        garbage_collected_scan(x._tail.load(Ordering::ACQUIRE));
    }
    
    

    
    
    
} // namespace wry

#endif /* concurrent_queue_hpp */
