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
        
        std::mutex _mutex;
        std::condition_variable _condition_variable;
        std::queue<T> _queue;
        ptrdiff_t _waiting = 0;
        
        bool was_empty() const {
            bool result;
            {
                std::unique_lock lock{_mutex};
                result = _queue.empty();
            }
            return result;
        }
        
        void push(T x) {
            ptrdiff_t waiting = 0;
            {
                std::unique_lock lock{_mutex};
                _queue.push(std::move(x));
                waiting = _waiting;
            }
            if (waiting) {
                _condition_variable.notify_all();
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
        
        void pop_wait(T& victim) {
            std::unique_lock lock{_mutex};
            for (;;) {
                if (_queue.empty()) {
                    ++_waiting;
                    _condition_variable.wait(lock);
                    --_waiting;
                } else {
                    victim = std::move(_queue.front());
                    _queue.pop();
                    return;
                }
            }
        }
        
        void hack_wait_until(auto absolute_time) {
            std::unique_lock lock{_mutex};
            while (_queue.empty()) {
                ++_waiting;
                auto t0 = std::chrono::high_resolution_clock::now();
                printf("hack_wait_nonempty: waiting\n");
                std::cv_status result = _condition_variable.wait_until(lock, std::move(absolute_time));
                auto t1 = std::chrono::high_resolution_clock::now();
                --_waiting;
                printf("hack_wait_nonempty: waited %.3gs\n", std::chrono::nanoseconds{t1 - t0}.count() * 1e-9);
                if (result == std::cv_status::timeout) {
                    return;
                }
            }
        }
        
        bool pop_wait_until(T& victim, auto absolute_time) {
            std::unique_lock lock{_mutex};
            for (;;) {
                if (_queue.empty()) {
                    ++_waiting;
                    auto t0 = std::chrono::high_resolution_clock::now();
                    std::cv_status result = _condition_variable.wait_until(lock, absolute_time);
                    auto t1 = std::chrono::high_resolution_clock::now();
                    --_waiting;
                    printf("%s: waited %.3gs\n", __PRETTY_FUNCTION__, std::chrono::nanoseconds{t1 - t0}.count() * 1e-9);
                    if (result == std::cv_status::timeout)
                        return false;
                } else {
                    victim = std::move(_queue.front());
                    _queue.pop();
                    return true;
                }
            }
        }
        
    }; // Channel
    
    
} // namespace wry
#endif /* channel_hpp */
