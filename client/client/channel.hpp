//
//  channel.hpp
//  client
//
//  Created by Antony Searle on 17/7/2025.
//

#ifndef channel_hpp
#define channel_hpp

#include <cstdio>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace wry {
    
    // Basic blocking multi-producer multi-consumer channel
    //
    // Cancelation wakes all waiters and prevents further waiting, but does not
    // interfere with push or try_pop.
    
    template<typename T>
    struct Channel {
        
        struct Result {
            enum Tag {
                VALUE = 0,
                EMPTY,
                TIMEOUT,
                CLOSED,
            } tag;
            union {
                char _dummy;
                T value;
            };
        };
        
        mutable std::mutex _mutex;
        std::condition_variable _condition_variable;
        std::queue<T> _queue;
        ptrdiff_t _waiting = 0;
        bool _is_canceled;
        
        bool was_empty() const {
            std::unique_lock lock{_mutex};
            return _queue.empty();
        }
        
        void cancel() {
            ptrdiff_t waiting;
            {
                std::unique_lock lock{_mutex};
                waiting = _waiting;
                _waiting = 0;
                _is_canceled = true;
            }
            _condition_variable.notify_all();
        }
        
        void push(T x) {
            ptrdiff_t waiting = 0;
            {
                std::unique_lock lock{_mutex};
                _queue.push(std::move(x));
                waiting = _waiting;
                if (_waiting)
                    --_waiting;
            }
            if (waiting) {
                _condition_variable.notify_one();
            }
        }
        
        bool try_pop(T& victim) {
            std::unique_lock lock{_mutex};
            bool result = !_queue.empty();
            if (result) {
                victim = std::move(_queue.front());
                _queue.pop();
            }
            return result;
        }
        
        // pop_wait is not fair; the longest-waiting thread is not necessarily
        // the one awoken by push, and the thread that is awoken does not
        // necessarily win the race to pop that (or any) element
        bool pop_wait(T& victim) {
            std::unique_lock lock{_mutex};
            for (;;) {
                if (!_queue.empty()) {
                    victim = std::move(_queue.front());
                    _queue.pop();
                    return true;
                }
                if (_is_canceled)
                    return false;
                ++_waiting;
                _condition_variable.wait(lock);
            }
        }
                
        bool pop_wait_until(T& victim, auto absolute_time) {
            std::unique_lock lock{_mutex};
            for (;;) {
                if (!_queue.empty()) {
                    victim = std::move(_queue.front());
                    _queue.pop();
                    return true;
                }
                if (_is_canceled)
                    return false;
                ++_waiting;
                std::cv_status result = _condition_variable.wait_until(lock, absolute_time);
                if (result == std::cv_status::timeout)
                    return false;
            }
        }

        
    }; // Channel
    
    
} // namespace wry
#endif /* channel_hpp */
